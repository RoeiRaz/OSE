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

#include <kern/isadma.h>
#include <inc/x86.h>
#include <inc/string.h>
#include <kern/pmap.h>
#include <kern/picirq.h>
#include <kern/env.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/env.h>
#include <inc/trap.h>

static void
assert_supported_channel(int ch) {
    if (ch < 5 || ch > 7) panic("ISA-DMA channel %d is not useable or not supported in this JOS implementation\n");
}

static void
set_page_address_reg(int ch, uint8_t addr) {
    static uint16_t mapping[] = { [5] = 0x8B, [6] = 0x89, [7] = 0x8A };
    assert_supported_channel(ch);
    outb(mapping[ch], addr);
}

void 
isadma_set_masked(int ch, bool masked) {
    int mask_on = masked ? 1 : 0;
    assert_supported_channel(ch);
    outb(ISADMA_MASTER_SCMR, (mask_on << 2) |  (ch - 4));
}

void
isadma_flipflop_reset(void) {
    outb(ISADMA_MASTER_FLFP, 0xFF);
}

void
isadma_set_mode(int ch, int type, bool auto_reset, bool reverse, int mode) {
    int auto_on = auto_reset ? 1 : 0;
    int reverse_on = reverse ? 1 : 0;
    assert_supported_channel(ch);
    outb(ISADMA_MASTER_MODE, (ch - 4) | (type << 2) | (auto_on << 4) | (reverse_on << 5) | (mode << 6));
}

/**
 * len - number of WORDS to transfer
 */
void
isadma_set_address(int ch, physaddr_t addr, int len) {
    if ((addr & 0xFFFF) | (addr & 0xFF000000))
        panic("ISA-DMA address is invalid (either not 64kb aligned or more than 24 bits)");
    
    len -= 1;
    
    isadma_flipflop_reset();
    
    outb(ISADMA_MASTER_BASE + 4 * (ch - 4), addr & 0xFF);   // low byte
    outb(ISADMA_MASTER_BASE + 4 * (ch - 4), (addr & 0xFF00) >> 8); // high byte
    
    isadma_flipflop_reset();
    
    outb(ISADMA_MASTER_BASE + 4 * (ch - 4) + 2, len & 0xFF);   // low byte
    outb(ISADMA_MASTER_BASE + 4 * (ch - 4) + 2, (len & 0xFF00) >> 8);   // high byte
    
    set_page_address_reg(ch, (addr & 0xFF0000) >> 16);
}
