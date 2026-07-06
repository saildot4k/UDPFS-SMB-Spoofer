/*
 * UDPFS iomanX - iomanX Device Wrapper for UDPFS
 *
 * Thin wrapper that translates iomanX device operations to core UDPFS protocol calls.
 * Manages file descriptor table mapping iomanX file pointers to server handles.
 */

#include <errno.h>
#include <intrman.h>
#include <iomanX.h>
#include <io_common.h>
#include <sifcmd.h>
#include <sifman.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <thbase.h>
#include <ps2smb.h>

#include "../include/main.h"
#include "udpfs_core.h"


#define UDPFS_MAX_HANDLES 8

#define SMS_SIF_CMD_IOP_NOTIFY 18
#define SMS_SIF_CMD_SMB_CONNECT 0

#define SMS_SMB_IOCTL_LOGIN  0x00000000
#define SMS_SMB_IOCTL_LOGOUT 0x00000001
#define SMS_SMB_IOCTL_MOUNT  0x00000002
#define SMS_SMB_IOCTL_UMOUNT 0x00000003
#define SMS_SMB_IOCTL_SENUM  0x00000004
#define SMS_SMB_IOCTL_ECHO   0x00000005
#define SMS_SMB_IOCTL_STOPC  0x00000006
#define SMS_SMB_IOCTL_SETCP  0x00000007

#define SMS_SMB_SENUM_SIZE 8096
#define SMS_ROOT_HANDLE ((void *)(uintptr_t)0x534d4252)
#define SMS_ROOT_DONE_HANDLE ((void *)(uintptr_t)0x534d4244)
#define SMS_ROOT_FILE_HANDLE ((void *)(uintptr_t)0x534d4246)

#ifndef UDPFS_IOMAN_DEVICE_NAME
#define UDPFS_IOMAN_DEVICE_NAME "udpfs"
#endif

/* Per-handle state for file descriptor mapping */
typedef struct {
    int32_t server_handle;  /* Server-side handle, -1 = free */
    int     is_dir;         /* 1 if directory, 0 if file */
} udpfs_fd_t;

typedef struct {
    char m_Name[13];
    unsigned char m_Pad;
    unsigned short m_Type;
    char *m_pRemark;
} sms_share_info_t;

typedef struct {
    int m_Unit __attribute__((packed));
    char m_Path[512];
} sms_mount_info_t;

typedef struct {
    int m_Unit __attribute__((packed));
    sms_share_info_t *m_pInfo __attribute__((packed));
} sms_senum_info_t;

typedef struct {
    char m_ServerIP[16];
    char m_ServerName[16];
    char m_ClientName[16];
    char m_UserName[32];
    char m_Password[64];
    char m_fAsync;
} sms_login_info_t;

static udpfs_fd_t g_fds[UDPFS_MAX_HANDLES];
static int g_udpfs_initialized = 0;
static char g_smb_share_name[256] = "UDPFS";
static unsigned char g_sms_senum_buf[SMS_SMB_SENUM_SIZE] __attribute__((aligned(64)));

extern int udpfs_network_init(void);

static int smb_is_root_path(const char *path)
{
    return path == NULL || path[0] == 0 || (path[0] == '/' && path[1] == 0) || (path[0] == '\\' && path[1] == 0);
}

static void smb_fill_root_stat(iox_stat_t *stat)
{
    if (stat == NULL)
        return;

    memset(stat, 0, sizeof(iox_stat_t));
    stat->mode = FIO_S_IFDIR | FIO_S_IRUSR | FIO_S_IXUSR | FIO_S_IRGRP | FIO_S_IXGRP | FIO_S_IROTH | FIO_S_IXOTH;
    stat->attr = FIO_SO_IFDIR;
}

static int udpfs_ensure_connected(void)
{
    if (udpfs_core_is_connected()) {
        g_udpfs_initialized = 1;
        return 0;
    }

    if (g_udpfs_initialized) {
        udpfs_core_exit();
        g_udpfs_initialized = 0;
    }

    if (udpfs_network_init() < 0)
        return -EIO;

    if (udpfs_core_init() < 0)
        return -EIO;

    g_udpfs_initialized = 1;
    return 0;
}

static void smb_set_share_name(const char *name)
{
    if (name == NULL || name[0] == 0)
        return;

    memset(g_smb_share_name, 0, sizeof(g_smb_share_name));
    strncpy(g_smb_share_name, name, sizeof(g_smb_share_name) - 1);
}

static void smb_dma_send_ee(const void *buf, int size, void *ee_addr)
{
    SifDmaTransfer_t dmat;
    int oldstate, id;

    if (buf == NULL || ee_addr == NULL || size <= 0)
        return;

    dmat.dest = ee_addr;
    dmat.size = size;
    dmat.src  = (void *)buf;
    dmat.attr = 0;

    id = 0;
    while (!id) {
        CpuSuspendIntr(&oldstate);
        id = sceSifSetDma(&dmat, 1);
        CpuResumeIntr(oldstate);
    }

    while (sceSifDmaStat(id) >= 0)
        ;
}

static int smb_fake_get_share_list(const smbGetShareList_in_t *req)
{
    ShareEntry_t entry;

    if (req == NULL)
        return 1;
    if (req->maxent <= 0)
        return 0;

    memset(&entry, 0, sizeof(entry));
    strncpy(entry.ShareName, g_smb_share_name, sizeof(entry.ShareName) - 1);
    strncpy(entry.ShareComment, "UDPFS SMB spoofer", sizeof(entry.ShareComment) - 1);

    smb_dma_send_ee(&entry, sizeof(entry), req->EE_addr);
    return 1;
}

static void sms_send_login_success(void)
{
    int cmd[7] __attribute__((aligned(64)));
    int id;

    memset(cmd, 0, sizeof(cmd));
    cmd[3] = SMS_SIF_CMD_SMB_CONNECT;
    cmd[4] = 0; /* SMB unit */
    cmd[5] = 0; /* SMB error */
    cmd[6] = 0; /* server error */

    id = sceSifSendCmd(SMS_SIF_CMD_IOP_NOTIFY, cmd, sizeof(cmd), NULL, NULL, 0);
    if (id != 0) {
        while (sceSifDmaStat(id) >= 0)
            DelayThread(100);
    }
}

static void sms_notify_login_success(void)
{
    sms_send_login_success();
    M_PRINTF("smb ioctl: LOGIN SIF success sent\n");
}

static void sms_login_notify_thread(void *arg)
{
    int i;

    (void)arg;

    DelayThread(100000);
    sms_send_login_success();
    M_PRINTF("smb ioctl: LOGIN SIF success sent\n");

    for (i = 0; i < 19; i++) {
        DelayThread(250000);
        sms_send_login_success();
    }

    M_PRINTF("smb ioctl: LOGIN SIF success burst complete\n");
    ExitDeleteThread();
}

static void sms_start_login_notify_thread(void)
{
    iop_thread_t thread;
    int tid;

    memset(&thread, 0, sizeof(thread));
    thread.attr = TH_C;
    thread.thread = sms_login_notify_thread;
    thread.stacksize = 2048;
    thread.priority = 64;

    tid = CreateThread(&thread);
    if (tid <= 0 || StartThread(tid, NULL) < 0) {
        M_PRINTF("smb ioctl: LOGIN notifier fallback\n");
        sms_notify_login_success();
    } else {
        M_PRINTF("smb ioctl: LOGIN notifier armed\n");
    }
}

static int sms_fake_share_enum(const sms_senum_info_t *req)
{
    sms_share_info_t *entry;
    char *remark;
    uintptr_t ee_base;
    int remark_off;

    if (req == NULL || req->m_pInfo == NULL)
        return 1;

    memset(g_sms_senum_buf, 0, sizeof(g_sms_senum_buf));

    entry = (sms_share_info_t *)g_sms_senum_buf;
    remark_off = sizeof(sms_share_info_t);
    remark = (char *)&g_sms_senum_buf[remark_off];
    ee_base = (uintptr_t)req->m_pInfo;

    strncpy(entry->m_Name, g_smb_share_name, sizeof(entry->m_Name) - 1);
    entry->m_Type = 0;
    entry->m_pRemark = (char *)(ee_base + remark_off);
    strncpy(remark, "UDPFS SMB spoofer", SMS_SMB_SENUM_SIZE - remark_off - 1);

    smb_dma_send_ee(g_sms_senum_buf, sizeof(g_sms_senum_buf), req->m_pInfo);
    return 1;
}

static int sms_ioctl(unsigned long cmd, void *data)
{
    switch (cmd) {
        case SMS_SMB_IOCTL_LOGIN:
            if (data == NULL || ((const sms_login_info_t *)data)->m_fAsync) {
                M_PRINTF("smb ioctl: LOGIN async -> notify success\n");
                sms_start_login_notify_thread();
                return -EINPROGRESS;
            }

            M_PRINTF("smb ioctl: LOGIN sync -> unit 0\n");
            return 0;

        case SMS_SMB_IOCTL_LOGOUT:
            M_PRINTF("smb ioctl: LOGOUT -> success\n");
            return 0;

        case SMS_SMB_IOCTL_MOUNT:
            if (data != NULL) {
                const sms_mount_info_t *info = (const sms_mount_info_t *)data;
                if (info->m_Path[0] != 0)
                    smb_set_share_name(info->m_Path);
            }
            M_PRINTF("smb ioctl: MOUNT -> unit 1\n");
            return 1;

        case SMS_SMB_IOCTL_UMOUNT:
            M_PRINTF("smb ioctl: UMOUNT -> success\n");
            return 0;

        case SMS_SMB_IOCTL_SENUM:
            M_PRINTF("smb ioctl: SENUM -> 1 share\n");
            return sms_fake_share_enum((const sms_senum_info_t *)data);

        case SMS_SMB_IOCTL_ECHO:
            M_PRINTF("smb ioctl: ECHO -> success\n");
            return 0;

        case SMS_SMB_IOCTL_STOPC:
            M_PRINTF("smb ioctl: STOPC -> success\n");
            return 0;

        case SMS_SMB_IOCTL_SETCP:
            M_PRINTF("smb ioctl: SETCP -> success\n");
            return 0;
    }

    M_PRINTF("smb ioctl: unhandled cmd=0x%08x, faking success\n", (unsigned int)cmd);
    return 0;
}


/*
 * Helper: allocate a local file descriptor slot
 */
static int _alloc_fd(void)
{
    int i;
    for (i = 0; i < UDPFS_MAX_HANDLES; i++) {
        if (g_fds[i].server_handle < 0)
            return i;
    }
    return -1;
}

/*
 * Helper: free a local file descriptor slot
 */
static void _free_fd(int idx)
{
    if (idx >= 0 && idx < UDPFS_MAX_HANDLES)
        g_fds[idx].server_handle = -1;
}

/*
 * Helper: validate file descriptor and extract server handle.
 * Returns 0 on success, negative errno on error.
 */
static int _validate_fd(iop_file_t *f, int *fd_idx_out, int32_t *handle_out)
{
    int fd_idx = (int)(uintptr_t)f->privdata;
    if (!udpfs_core_is_connected())
        return -EIO;
    if (fd_idx < 0 || fd_idx >= UDPFS_MAX_HANDLES)
        return -EBADF;
    if (g_fds[fd_idx].server_handle < 0)
        return -EBADF;
    *fd_idx_out = fd_idx;
    *handle_out = g_fds[fd_idx].server_handle;
    return 0;
}


/*
 * iomanX device operations
 */

static int udpfs_init_dev(iop_device_t *d)
{
    int i;

    M_DEBUG("%s()\n", __FUNCTION__);
    (void)d;

    /* Initialize FD table */
    for (i = 0; i < UDPFS_MAX_HANDLES; i++)
        g_fds[i].server_handle = -1;

    g_udpfs_initialized = 0;

    M_DEBUG("udpfs: smb compatibility device ready\n");
    return 0;
}

static int udpfs_deinit_dev(iop_device_t *d)
{
    M_DEBUG("%s()\n", __FUNCTION__);
    (void)d;

    if (g_udpfs_initialized) {
        udpfs_core_exit();
        g_udpfs_initialized = 0;
    }

    return 0;
}

static int udpfs_format(iop_file_t *f, const char *unk1, const char *unk2, void *unk3, int unk4)
{
    return -EIO;
}

static int udpfs_open(iop_file_t *f, const char *name, int flags, int mode)
{
    int fd_idx, ret;
    int32_t server_handle;
    const char *log_name = name != NULL ? name : "";

    M_PRINTF("smb open: unit=%d name='%s' flags=0x%x\n", f->unit, log_name, flags);

    if (f->unit == 0 && smb_is_root_path(log_name)) {
        M_PRINTF("smb open: root -> success\n");
        f->privdata = SMS_ROOT_FILE_HANDLE;
        return 0;
    }

    ret = udpfs_ensure_connected();
    if (ret < 0)
        return ret;

    /* Allocate local fd */
    fd_idx = _alloc_fd();
    if (fd_idx < 0)
        return -EMFILE;

    /* Call core open */
    ret = udpfs_core_open(name, flags, mode, 0, &server_handle);
    if (ret < 0) {
        _free_fd(fd_idx);
        return ret;
    }

    /* Store handle and metadata */
    g_fds[fd_idx].server_handle = server_handle;
    g_fds[fd_idx].is_dir = 0;
    f->privdata = (void *)(uintptr_t)fd_idx;

    return 0;
}

static int udpfs_close(iop_file_t *f)
{
    int fd_idx = (int)(uintptr_t)f->privdata;

    M_DEBUG("%s(fd=%d)\n", __FUNCTION__, fd_idx);

    if (f->privdata == SMS_ROOT_FILE_HANDLE) {
        M_PRINTF("smb close: root -> success\n");
        f->privdata = NULL;
        return 0;
    }

    if (fd_idx < 0 || fd_idx >= UDPFS_MAX_HANDLES)
        return -EBADF;
    if (g_fds[fd_idx].server_handle < 0)
        return -EBADF;

    /* Call core close */
    udpfs_core_close(g_fds[fd_idx].server_handle);

    _free_fd(fd_idx);
    return 0;
}

static int udpfs_read(iop_file_t *f, void *buffer, int size)
{
    int fd_idx;
    int32_t server_handle;
    int ret;

    if (f->privdata == SMS_ROOT_FILE_HANDLE) {
        M_PRINTF("smb read: root -> eof\n");
        return 0;
    }

    ret = _validate_fd(f, &fd_idx, &server_handle);
    if (ret < 0)
        return ret;

    M_DEBUG("%s(fd=%d, %d bytes)\n", __FUNCTION__, fd_idx, size);

    /* Delegate to core */
    return udpfs_core_read(server_handle, buffer, size);
}

static int udpfs_write(iop_file_t *f, void *buffer, int size)
{
    int fd_idx;
    int32_t server_handle;
    int ret;

    ret = _validate_fd(f, &fd_idx, &server_handle);
    if (ret < 0)
        return ret;

    M_DEBUG("%s(fd=%d, %d bytes)\n", __FUNCTION__, fd_idx, size);

    /* Delegate to core */
    return udpfs_core_write(server_handle, buffer, size);
}

static s64 udpfs_lseek64(iop_file_t *f, s64 offset, int whence)
{
    int fd_idx;
    int32_t server_handle;
    int ret;

    if (f->privdata == SMS_ROOT_FILE_HANDLE)
        return 0;

    ret = _validate_fd(f, &fd_idx, &server_handle);
    if (ret < 0)
        return ret;

    M_DEBUG("%s(fd=%d, offset=0x%x%08x, whence=%d)\n", __FUNCTION__, fd_idx,
        (unsigned int)(offset >> 32), (unsigned int)offset, whence);

    /* Delegate to core */
    return udpfs_core_lseek(server_handle, offset, whence);
}

static int udpfs_lseek(iop_file_t *f, int offset, int whence)
{
    M_DEBUG("%s(%d, %d)\n", __FUNCTION__, offset, whence);
    return (int)udpfs_lseek64(f, (s64)offset, whence);
}

static int udpfs_ioctl(iop_file_t *f, int cmd, void *data)
{
    M_PRINTF("smb ioctl: unit=%d cmd=0x%08x\n", f->unit, cmd);

    if (f->privdata == SMS_ROOT_HANDLE || f->privdata == SMS_ROOT_DONE_HANDLE)
        return sms_ioctl((unsigned long)cmd, data);

    if (f->privdata == SMS_ROOT_FILE_HANDLE)
        return sms_ioctl((unsigned long)cmd, data);

    return -EIO;
}

static int udpfs_remove(iop_file_t *f, const char *name)
{
    int ret;

    (void)f;
    M_DEBUG("%s(%s)\n", __FUNCTION__, name);

    ret = udpfs_ensure_connected();
    if (ret < 0)
        return ret;

    return udpfs_core_remove(name);
}

static int udpfs_mkdir(iop_file_t *f, const char *path, int mode)
{
    int ret;

    (void)f;
    M_DEBUG("%s(%s, 0x%x)\n", __FUNCTION__, path, mode);

    ret = udpfs_ensure_connected();
    if (ret < 0)
        return ret;

    return udpfs_core_mkdir(path, mode);
}

static int udpfs_rmdir(iop_file_t *f, const char *path)
{
    int ret;

    (void)f;
    M_DEBUG("%s(%s)\n", __FUNCTION__, path);

    ret = udpfs_ensure_connected();
    if (ret < 0)
        return ret;

    return udpfs_core_rmdir(path);
}

static int udpfs_dopen(iop_file_t *f, const char *path)
{
    int fd_idx, ret;
    int32_t server_handle;
    const char *log_path = path != NULL ? path : "";

    M_PRINTF("smb dopen: unit=%d path='%s'\n", f->unit, log_path);

    if (f->unit == 0 && smb_is_root_path(log_path)) {
        M_PRINTF("smb dopen: root -> success\n");
        f->privdata = SMS_ROOT_HANDLE;
        return 0;
    }

    path = log_path;

    ret = udpfs_ensure_connected();
    if (ret < 0)
        return ret;

    /* Allocate local fd */
    fd_idx = _alloc_fd();
    if (fd_idx < 0)
        return -EMFILE;

    /* Call core open for directory */
    ret = udpfs_core_open(path, 0, 0, 1, &server_handle);
    if (ret < 0) {
        _free_fd(fd_idx);
        return ret;
    }

    /* Store handle and metadata */
    g_fds[fd_idx].server_handle = server_handle;
    g_fds[fd_idx].is_dir = 1;
    f->privdata = (void *)(uintptr_t)fd_idx;

    return 0;
}

static int udpfs_dclose(iop_file_t *f)
{
    M_DEBUG("%s()\n", __FUNCTION__);

    if (f->privdata == SMS_ROOT_HANDLE || f->privdata == SMS_ROOT_DONE_HANDLE) {
        f->privdata = NULL;
        return 0;
    }

    return udpfs_close(f);
}

static int udpfs_dread(iop_file_t *f, iox_dirent_t *dirent)
{
    int fd_idx;
    int32_t server_handle;
    iox_stat_t stat;
    char name[256];
    int ret;

    if (f->privdata == SMS_ROOT_HANDLE) {
        memset(dirent, 0, sizeof(iox_dirent_t));
        dirent->stat.mode = FIO_S_IFDIR | FIO_S_IRUSR | FIO_S_IXUSR | FIO_S_IRGRP | FIO_S_IXGRP | FIO_S_IROTH | FIO_S_IXOTH;
        strncpy(dirent->name, g_smb_share_name, sizeof(dirent->name) - 1);
        f->privdata = SMS_ROOT_DONE_HANDLE;
        return 1;
    }

    if (f->privdata == SMS_ROOT_DONE_HANDLE)
        return 0;

    ret = _validate_fd(f, &fd_idx, &server_handle);
    if (ret < 0)
        return ret;

    M_DEBUG("%s(fd=%d)\n", __FUNCTION__, fd_idx);

    /* Call core dread */
    ret = udpfs_core_dread(server_handle, &stat, name, sizeof(name));
    if (ret <= 0)
        return ret;  /* 0 = end of dir, negative = error */

    /* Populate dirent from stat and name */
    memset(dirent, 0, sizeof(iox_dirent_t));
    memcpy(&dirent->stat, &stat, sizeof(iox_stat_t));
    strncpy(dirent->name, name, 255);
    dirent->name[255] = '\0';

    return 1;
}

static int udpfs_getstat(iop_file_t *f, const char *name, iox_stat_t *stat)
{
    int ret;
    const char *log_name = name != NULL ? name : "";

    (void)f;
    M_PRINTF("smb getstat: name='%s'\n", log_name);

    if (f->unit == 0 && smb_is_root_path(log_name)) {
        smb_fill_root_stat(stat);
        M_PRINTF("smb getstat: root -> dir\n");
        return 0;
    }

    ret = udpfs_ensure_connected();
    if (ret < 0)
        return ret;

    return udpfs_core_getstat(name, stat);
}

static int udpfs_chstat(iop_file_t *f, const char *name, iox_stat_t *stat, unsigned int mask)
{
    return -EIO;
}

static int udpfs_rename(iop_file_t *f, const char *old, const char *new_name)
{
    return -EIO;
}

static int udpfs_chdir(iop_file_t *f, const char *name)
{
    return -EIO;
}

static int udpfs_sync(iop_file_t *f, const char *dev, int flag)
{
    return -EIO;
}

static int udpfs_mount(iop_file_t *f, const char *fsname, const char *devname, int flag, void *arg, int arglen)
{
    return -EIO;
}

static int udpfs_umount(iop_file_t *f, const char *fsname)
{
    return -EIO;
}

static int udpfs_devctl(iop_file_t *f, const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen)
{
    (void)f;
    (void)name;
    (void)arg;
    (void)arglen;

    M_PRINTF("smb devctl: name='%s' cmd=0x%08x\n", name != NULL ? name : "", cmd);

    switch (cmd) {
        case SMB_DEVCTL_GETPASSWORDHASHES:
            M_PRINTF("smb devctl: GETPASSWORDHASHES -> success\n");
            if (buf != NULL && buflen > 0)
                memset(buf, 0, buflen);
            return 0;

        case SMB_DEVCTL_LOGON:
            M_PRINTF("smb devctl: LOGON -> success\n");
            return 0;

        case SMB_DEVCTL_LOGOFF:
            M_PRINTF("smb devctl: LOGOFF -> success\n");
            return 0;

        case SMB_DEVCTL_GETSHARELIST:
            M_PRINTF("smb devctl: GETSHARELIST -> 1 share\n");
            return smb_fake_get_share_list((const smbGetShareList_in_t *)arg);

        case SMB_DEVCTL_OPENSHARE:
            if (arg != NULL && arglen >= sizeof(smbOpenShare_in_t))
                smb_set_share_name(((const smbOpenShare_in_t *)arg)->ShareName);
            M_PRINTF("smb devctl: OPENSHARE -> success\n");
            return 0;

        case SMB_DEVCTL_CLOSESHARE:
            M_PRINTF("smb devctl: CLOSESHARE -> success\n");
            return 0;

        case SMB_DEVCTL_ECHO:
            M_PRINTF("smb devctl: ECHO -> success\n");
            return 0;

        case SMB_DEVCTL_QUERYDISKINFO:
            M_PRINTF("smb devctl: QUERYDISKINFO -> success\n");
            if (buf != NULL && buflen >= sizeof(smbQueryDiskInfo_out_t)) {
                smbQueryDiskInfo_out_t *info = (smbQueryDiskInfo_out_t *)buf;
                info->TotalUnits = 0x100000;
                info->BlocksPerUnit = 1;
                info->BlockSize = 512;
                info->FreeUnits = 0x100000;
            }
            return 0;
    }

    M_PRINTF("smb devctl: unhandled cmd=0x%08x, faking success\n", cmd);
    return 0;
}

static int udpfs_symlink(iop_file_t *f, const char *old, const char *new_name)
{
    return -EIO;
}

static int udpfs_readlink(iop_file_t *f, const char *path, char *buf, unsigned int buflen)
{
    return -EIO;
}

static int udpfs_ioctl2(iop_file_t *f, int cmd, void *data, unsigned int datalen, void *rdata, unsigned int rdatalen)
{
    if (cmd == 0x80) {
        int fd_idx = (int)(uintptr_t)f->privdata;
        if (fd_idx >= 0 && fd_idx < UDPFS_MAX_HANDLES)
            return g_fds[fd_idx].server_handle;
        return -EBADF;
    }
    return -EIO;
}


/*
 * Device ops table
 */
static iop_device_ops_t udpfs_device_ops = {
    udpfs_init_dev,
    udpfs_deinit_dev,
    udpfs_format,
    udpfs_open,
    udpfs_close,
    udpfs_read,
    udpfs_write,
    udpfs_lseek,
    udpfs_ioctl,
    udpfs_remove,
    udpfs_mkdir,
    udpfs_rmdir,
    udpfs_dopen,
    udpfs_dclose,
    udpfs_dread,
    udpfs_getstat,
    udpfs_chstat,
    /* Extended ops */
    udpfs_rename,
    udpfs_chdir,
    udpfs_sync,
    udpfs_mount,
    udpfs_umount,
    udpfs_lseek64,
    udpfs_devctl,
    udpfs_symlink,
    udpfs_readlink,
    udpfs_ioctl2
};

static const char udpfs_name[] = UDPFS_IOMAN_DEVICE_NAME;
static iop_device_t udpfs_device = {
    udpfs_name,
    IOP_DT_FSEXT | IOP_DT_FS,
    1,
    udpfs_name,
    &udpfs_device_ops
};


/*
 * Initialize UDPFS as iomanX device
 */
int udpfs_init(void)
{
    int ret;

    M_DEBUG("UDPFS over UDPRDMA by Maximus32\n");

    /* Register iomanX device */
    DelDrv(udpfs_device.name);
    ret = AddDrv(&udpfs_device);
    if (ret != 0)
        return ret;

    M_PRINTF("smb: compatibility device registered\n");
    sms_start_login_notify_thread();
    return 0;
}
