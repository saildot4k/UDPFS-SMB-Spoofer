#include <irx.h>
#include <loadcore.h>

IRX_ID("SMAP_driver", 0x2, 0x1A);

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return MODULE_RESIDENT_END;
}
