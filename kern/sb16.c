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
#include <kern/isadma.h>
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
int16_t 
sb16_buffer[SB16_BUFFER_SIZE_WORDS]
__attribute__ ((aligned(DMA_BUFFER_SIZE_WORDS << 1)));

int16_t *audio_data;
size_t data_length_words;

// Due to budget limitation, the driver is capable to serve one environment at a time.
// The locking environment is sleeping, waiting for this driver to wake it up when playback is done.
struct Env *locking_environment = NULL;

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

static void
sb16_ack_intr(void) {
    inb(sb16io_base + 0x0F);
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
sb16dsp_set_output_rate(uint16_t rate_hz) {
    sb16dsp_write(0x41);
    sb16dsp_write((rate_hz & 0xFF00) >> 8);
    sb16dsp_write(rate_hz & 0xFF);
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

static void
playback(int length_words) {
    
    if (length_words <= 0) return;
    if (length_words > DMA_BUFFER_SIZE_WORDS) panic("playback length is bigger than 64kb");
    
    // Enable DAC
    sb16dsp_write(0xD1);
    
    // set sample rate
    sb16dsp_set_output_rate(44100);
    
    // set up the ISA DMA channel
    isadma_set_masked(5, true);
    isadma_set_address(5, PADDR(sb16_buffer), length_words);
    isadma_set_mode(5, ISADMA_TYPE_M2P, false, false, ISADMA_MODE_SNGL);
    isadma_set_masked(5, false);
    
    // Program auto initialized mono signed 16 bit output
    sb16dsp_write(0xB0);
    sb16dsp_write(0x10);
    
    // length is half the buffer size (chunk size)
    sb16dsp_write((length_words - 1) & 0xFF);
    sb16dsp_write(((length_words - 1) >> 8) & 0xFF); // playback is starting here
}

static void
process_audio_data() {
    if (data_length_words <= 0) return;
    
    // heavy part
    physaddr_t prev_pgdir = rcr3();
    lcr3(PADDR(locking_environment->env_pgdir));
    memcpy(sb16_buffer, audio_data, MIN(DMA_BUFFER_SIZE_WORDS, data_length_words) << 1);
    lcr3(prev_pgdir);
    
    // initiate playback
    playback(MIN(DMA_BUFFER_SIZE_WORDS, data_length_words));
    
    // increment pointer, decrement length counter
    data_length_words -= MIN(DMA_BUFFER_SIZE_WORDS, data_length_words);
    audio_data += MIN(DMA_BUFFER_SIZE_WORDS, data_length_words);
}

int
sb16_play(struct Env *env, int16_t *data, size_t len_words) {
    
    if (locking_environment != NULL)
        return -1;
    
    if (len_words <= 0)
        panic("length must be positive");
    
    locking_environment = env;
    
    audio_data = data;
    data_length_words = len_words;
    
    process_audio_data();
    
    return 0;
}

int16_t b[DMA_BUFFER_SIZE_WORDS];

static void
fillbuffer() {
    int i;
    for (i = 0; i < DMA_BUFFER_SIZE_WORDS; i++) {
        if ((i / 25) % 2) b[i] = 0xFFF;
        else b[i] = -0xFFF;
    }
    sb16_play((struct Env *)1, b, DMA_BUFFER_SIZE_WORDS);
}

void sb16_init(void) {
    sb16dsp_reset();
    struct sb16_version_t version;
    sb16_read_version(&version);
    
    // Enable relevant IRQ in the PIC
    irq_setmask_8259A(irq_mask_8259A & (~(1 << 5)));
    
    
    cprintf("-------------------------------------------------\n");
    cprintf("# SoundBlaster initialization sequence...\n");
    cprintf("soundblaster16 (major %d minor %d) initialized [%s]\n", version.major, version.minor, "OK");
    cprintf("soundblaster16 interrupt settings: %x [%s]\n", sb16mixer_read(0x80), "OK");
    cprintf("soundblaster16 DMA settings: %x [%s]\n", sb16mixer_read(0x81), "OK");
    cprintf("soundblaster16 Buffer physical location: %xh [%s]\n", PADDR(sb16_buffer), okorbad(check_sb16_buffer_address()));
    cprintf("-------------------------------------------------\n");
}

void sb16_intr(void) {
#if DEBUG
    cprintf("SB16 INTERRUPT ROUTINE ENCOUNTERED\n");
#endif
    // Ack interrupt
    sb16_ack_intr();
    
    if (data_length_words) {
        process_audio_data();
        return;
    } else {
        // wakeup env
        locking_environment->env_status = ENV_RUNNABLE;
        locking_environment->env_tf.tf_regs.reg_eax = 0;
        locking_environment = NULL;
    }
}
