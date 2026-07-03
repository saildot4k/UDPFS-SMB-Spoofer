#include <irx.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <thbase.h>

IRX_ID("SMSUTILS", 1, 0);

#define SMSUTILS_RPC_ID 0x6D737573

extern struct irx_export_table _exp_smsutils;

static SifRpcDataQueue_t g_rpc_queue;
static SifRpcServerData_t g_rpc_server;
static int g_rpc_result __attribute__((aligned(64)));
static unsigned char g_rpc_buffer[64] __attribute__((aligned(64)));

void mips_memcpy(void *dst, const void *src, unsigned size)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (size--)
        *d++ = *s++;
}

void mips_memset(void *dst, int value, unsigned size)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)value;

    while (size--)
        *d++ = v;
}

void mips_ee_printf(const char *fmt, ...)
{
    (void)fmt;
}

static void *smsutils_rpc_handler(int fno, void *buffer, int size)
{
    (void)buffer;
    (void)size;

    g_rpc_result = (fno == 0) ? (2 * 1024 * 1024) : 0;
    return &g_rpc_result;
}

static void smsutils_rpc_thread(void *arg)
{
    (void)arg;

    sceSifInitRpc(0);
    sceSifSetRpcQueue(&g_rpc_queue, GetThreadId());
    sceSifRegisterRpc(&g_rpc_server, SMSUTILS_RPC_ID, smsutils_rpc_handler, g_rpc_buffer, NULL, NULL, &g_rpc_queue);
    sceSifRpcLoop(&g_rpc_queue);
}

int _start(int argc, char *argv[])
{
    iop_thread_t thread;
    int thid;

    (void)argc;
    (void)argv;

    RegisterLibraryEntries(&_exp_smsutils);

    thread.attr = TH_C;
    thread.option = 0;
    thread.thread = smsutils_rpc_thread;
    thread.stacksize = 0x1000;
    thread.priority = 0x27;

    thid = CreateThread(&thread);
    if (thid > 0)
        StartThread(thid, NULL);

    return MODULE_RESIDENT_END;
}
