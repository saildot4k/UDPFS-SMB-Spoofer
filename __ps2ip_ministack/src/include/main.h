#ifndef MAIN_H
#define MAIN_H

#include <thbase.h>
#include "mprintf.h"

#define MODNAME "ps2ip"

struct SmapDriverData
{
    volatile u8 *smap_regbase;
    volatile u8 *emac3_regbase;
    unsigned int TxBufferSpaceAvailable;
    unsigned char NumPacketsInTx;
    unsigned char TxBDIndex;
    unsigned char TxDNVBDIndex;
    unsigned char RxBDIndex;
    int Dev9IntrEventFlag;
    int IntrHandlerThreadID;
    unsigned char SmapDriverStarted;
    unsigned char SmapIsInitialized;
    unsigned char NetDevStopFlag;
    unsigned char EnableLinkCheckTimer;
    unsigned char LinkStatus;
    unsigned char LinkMode;
    iop_sys_clock_t LinkCheckTimer;
    int NetIFID;
};

#define SMAP_EVENT_START      0x01
#define SMAP_EVENT_INTR       0x04
#define SMAP_EVENT_LINK_CHECK 0x10

int smap_init(int argc, char *argv[]);
int SMAPGetMACAddress(u8 *buffer);

#endif
