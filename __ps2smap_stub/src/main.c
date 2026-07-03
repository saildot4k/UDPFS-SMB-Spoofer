#include <irx.h>
#include <loadcore.h>

IRX_ID("TCP/IP Stack", 2, 3);

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return MODULE_RESIDENT_END;
}
