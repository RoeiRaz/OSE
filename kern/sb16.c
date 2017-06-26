/**
 * SOFTWARE LICENSE
 *
 * Copyright (c) 2017 Roei
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to 
 * deal in the Software without restriction, including without limitation the 
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or 
 * sell copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 *
 * none.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 */


#include <inc/x86.h>
#include <inc/string.h>
#include <kern/sb16.h>
#include <kern/pmap.h>
#include <kern/picirq.h>
#include <kern/env.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/env.h>
#include <inc/trap.h>

// This is the default IO base address for SoundBlaster16 cards.
// The base IO port might be different (when running on QEMU it would probably still be 220),
// so a validation during startup is required (for example, retrieve version number).
int sb16io_base = 0x220;

// Don't mind this. if you're running this driver on a very fast computer, please increase
// this number.
const int waitcycles = 0x100000;

// Buffer for DMA transfers. Must not cross 64kb page bounderies. Must not be bigger
// than 64kb. For optimal performance (?) we allocate 64kb for this.
uint8_t sb16_buffer[SB16_BUFFER_SIZE] __attribute__ ((aligned(SB16_BUFFER_SIZE)));

// Determines whether to print debug messages. Initialization sequence prints
// and important feedbacks are always printed, regardless of this macro values.
#define DEBUG 0

// holds for a couple of cycles; used when we need to wait for the device to stabalize.
// don't mind this.
static void 
busysleep(void) {
    int r = 0, i;
    for (i = waitcycles; i > 0; i--) r += i;
}


static uint8_t 
sb16dsp_read(void) {
    while (! (inb(sb16io_base + 0x0E) & 0x80));
    return inb(sb16io_base + 0x0A);
}

static void 
sb16dsp_write(uint8_t word) {
    while ((inb(sb16io_base + 0x0C) & 0x80));
    outb(sb16io_base + 0x0C, word);
}

static void 
sb16mixer_set_address(uint8_t mixer_index) {
    outb(sb16io_base + 0x04, mixer_index);
}

static uint8_t 
sb16mixer_read_value(void) {
    return inb(sb16io_base + 0x05);
}

static void 
sb16mixer_write_value(uint8_t data) {
    outb(sb16io_base + 0x05, data);
}

static uint8_t 
sb16mixer_read(uint8_t mixer_index) {
    sb16mixer_set_address(mixer_index);
    return sb16mixer_read_value();
}

static void 
sb16mixer_write(uint8_t mixer_index, uint8_t data) {
    sb16mixer_set_address(mixer_index);
    sb16mixer_write_value(data);
}

void 
sb16dsp_reset(void) {
    uint16_t r;
    outw(sb16io_base + 0x06, 1);
    busysleep();
    outw(sb16io_base + 0x06, 0);
    r = sb16dsp_read();
    if ((r & 0xFF) != 0xAA)
        panic("dsp reset failed");
}

void 
sb16_read_version(struct sb16_version_t *version) {
    sb16dsp_write(0xE1);
    *(uint16_t*)version = sb16dsp_read();
}

static char * 
okorbad(bool condition) {
    static char * OK = "OK";
    static char * BAD = "BAD";
    return condition ? OK : BAD;
}

static bool 
check_sb16_buffer_address() {
    // check that the buffer is 16 bit (64kb) aligned because of weird ISA DMA constraints
    if (0xFFFF & PADDR(sb16_buffer)) return false;
    
    // we can only address 24 bits with ISA DMA..........
    if (0xFF000000 & PADDR(sb16_buffer)) return false;
    
    return true;
}

void sb16_init(void) {
    sb16dsp_reset();
    struct sb16_version_t version;
    sb16_read_version(&version);
    cprintf("-------------------------------------------------\n");
    cprintf("# SoundBlaster initialization sequence...\n");
    cprintf("soundblaster16 (major %d minor %d) initialized [%s]\n", version.major, version.minor, "OK");
    cprintf("soundblaster16 interrupt settings: %x [%s]\n", sb16mixer_read(0x80), "OK");
    cprintf("soundblaster16 DMA settings: %x [%s]\n", sb16mixer_read(0x81), "OK");
    cprintf("soundblaster16 Buffer physical location: %xh [%s]\n", PADDR(sb16_buffer), okorbad(check_sb16_buffer_address()));
    cprintf("-------------------------------------------------\n");
}
