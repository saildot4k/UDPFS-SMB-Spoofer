#include <irx.h>
#include <ioman.h>
#include <loadcore.h>
#include <stdio.h>
#include <string.h>
#include <sysclib.h>

#include "main.h"
#include "ministack_eth.h"
#include "ministack_ip.h"
#include "smap.h"
#include "udptty.h"

IRX_ID("SMAP_driver", 0x2, 0x1A);

extern struct irx_export_table _exp_smap;
extern struct irx_export_table _exp_mstack;

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static uint32_t parse_ip(const char *sIP)
{
    int cp = 0;
    uint32_t part[4] = {0, 0, 0, 0};

    while (*sIP != 0) {
        if (*sIP == '.') {
            cp++;
            if (cp >= 4)
                return 0;
        } else if (*sIP >= '0' && *sIP <= '9') {
            part[cp] = (part[cp] * 10) + (*sIP - '0');
            if (part[cp] > 255)
                return 0;
        } else {
            return 0;
        }
        sIP++;
    }

    if (cp != 3)
        return 0;

    return IP_ADDR((uint8_t)part[0], (uint8_t)part[1], (uint8_t)part[2], (uint8_t)part[3]);
}

static uint32_t parse_first_ip(const char *data, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        char ipbuf[16];
        int j = 0;

        if (!is_digit(data[i]))
            continue;

        while (i + j < len && j < (int)sizeof(ipbuf) - 1) {
            char c = data[i + j];
            if (!is_digit(c) && c != '.')
                break;
            ipbuf[j++] = c;
        }
        ipbuf[j] = 0;

        if (j >= 7) {
            uint32_t ip = parse_ip(ipbuf);
            if (ip != 0)
                return ip;
        }
    }

    return 0;
}

static int load_ipconfig_path(const char *path)
{
    char buf[192];
    int fd;
    int len;
    uint32_t ip;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    len = read(fd, buf, sizeof(buf));
    close(fd);

    if (len <= 0)
        return 0;

    ip = parse_first_ip(buf, len);
    if (ip == 0)
        return 0;

    ms_ip_set_ip(ip);
    return 1;
}

static int load_ipconfig_near_module(const char *module_path)
{
    char path[128];
    const char name[] = "IPCONFIG.DAT";
    int last_sep = -1;
    int i;
    int out = 0;

    if (module_path == NULL)
        return 0;

    for (i = 0; module_path[i] != 0 && i < 100; i++) {
        char c = module_path[i];
        if (c == ':' || c == '/' || c == '\\')
            last_sep = i;
    }

    if (last_sep < 0)
        return 0;

    for (i = 0; i <= last_sep && out < (int)sizeof(path) - 1; i++)
        path[out++] = module_path[i];

    for (i = 0; name[i] != 0 && out < (int)sizeof(path) - 1; i++)
        path[out++] = name[i];
    path[out] = 0;

    return load_ipconfig_path(path);
}

static void load_ipconfig(int argc, char *argv[])
{
    if (argc > 0 && load_ipconfig_near_module(argv[0]))
        return;

    load_ipconfig_path("IPCONFIG.DAT");
}

int _start(int argc, char *argv[])
{
    int i;

    if (smap_init(argc, argv) < 0)
        return MODULE_NO_RESIDENT_END;

    if (RegisterLibraryEntries(&_exp_smap) != 0)
        return MODULE_NO_RESIDENT_END;

    smap_register_rx_callback(handle_rx_eth, 44);

    load_ipconfig(argc, argv);

    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "ip=", 3)) {
            uint32_t ip = parse_ip(&argv[i][3]);
            if (ip != 0)
                ms_ip_set_ip(ip);
        }
    }

    udptty_init();

    if (RegisterLibraryEntries(&_exp_mstack) != 0)
        return MODULE_NO_RESIDENT_END;

    return MODULE_RESIDENT_END;
}
