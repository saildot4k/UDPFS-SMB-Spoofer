#include <irx.h>
#include <loadcore.h>

IRX_ID("SMSUTILS", 1, 0);

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return MODULE_RESIDENT_END;
}
