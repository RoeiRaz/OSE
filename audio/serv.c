#include <inc/x86.h>
#include <inc/string.h>

#include "audio.h"

int sb16io_base = 0x220;
const int tries = 0x100000;

static void 
die(char *err) {
    cprintf("### SB16 ERROR: %s\n", err);
    sys_env_destroy(sys_getenvid());
}

static void 
busysleep(void) {
    int r = 0, i;
    for (i = tries; i > 0; i--) r += i;
}

uint16_t sb16dsp_read(void) {
    while (! (inw(sb16io_base + 0x0E) & 0x80));
    return inw(sb16io_base + 0x0A);
}

void sb16dsp_write(uint16_t word) {
    while ((inw(sb16io_base + 0x0C) & 0x80));
    outw(sb16io_base + 0x0C, word);
}

void sb16dsp_reset(void) {
    uint16_t r;
    outw(sb16io_base + 0x06, 1);
    busysleep();
    outw(sb16io_base + 0x06, 0);
    r = sb16dsp_read();
    if ((r & 0xFF) != 0xAA)
        die("dsp reset failed");
}

void
umain(int argc, char **argv)
{
    struct sb16_version_t version;
    sys_sb16_read_version(&version);
    cprintf("##################### SOUND BLASTER, VERSION %d (%d) #####################\n", version.major, version.minor);
    return;
}
