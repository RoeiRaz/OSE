#include "audio.h"
#include <inc/x86.h>
#include <inc/string.h>

#include <inc/x86.h>

// ##########################################################################
// ####################### IDE API, FOR ACCESSING HDC #######################
// ##########################################################################
#define SECTSIZE    512
#define IDE_BSY		0x80
#define IDE_DRDY	0x40
#define IDE_DF		0x20
#define IDE_ERR		0x01

static int diskno = 0;

static int
ide_wait_ready(bool check_error)
{
	int r;

	while (((r = inb(0x177)) & IDE_BSY))
		/* do nothing */;
    
    // choose diskno before polling for drive rdy bit.
    outb(0x176, 0xE0 | ((diskno)<<4));

    while (((r = inb(0x177)) & (IDE_BSY | IDE_DRDY)) != IDE_DRDY)
		/* do nothing */;
    
	if (check_error && (r & (IDE_DF|IDE_ERR)) != 0)
		return -1;
	return 0;
}

int
ide_read(uint32_t secno, void *dst, size_t nsecs)
{
	int r;

	assert(nsecs <= 256);

	ide_wait_ready(0);

	outb(0x172, nsecs);
	outb(0x173, secno & 0xFF);
	outb(0x174, (secno >> 8) & 0xFF);
	outb(0x175, (secno >> 16) & 0xFF);
	outb(0x176, 0xE0 | ((diskno)<<4) | ((secno>>24)&0x0F));
	outb(0x177, 0x20);	// CMD 0x20 means read sector

	for (; nsecs > 0; nsecs--, dst += SECTSIZE) {
		if ((r = ide_wait_ready(1)) < 0)
			return r;
		insl(0x170, dst, SECTSIZE/4);
	}

	return 0;
}


// ##########################################################################
// ####################### IDE API, FOR ACCESSING HDC #######################
// ##########################################################################

#define WAV_BUFFER_SIZE (1 << 25)
int8_t wavbuff[WAV_BUFFER_SIZE];
int8_t *wavhead = wavbuff;

#define RIFF_CHUNK_ID   (0x46464952)
#define FMT_CHUNK_ID    (0x20746d66)
#define DATA_CHUNK_ID   (0x61746164)

#define AS_CHUNKP(x) ((struct chunk *)(x))
#define AS_FMTP(x) ((struct fmt *)(x))
#define AS_BYTEP(x) ((int8_t *)(x))

struct chunk {
    uint32_t chunkid;
    uint32_t chunksize;
};

struct fmt {
    uint32_t subchunk1id;
    uint32_t subchunk1size;
    uint16_t audioformat;
    uint16_t numchannels;
    uint32_t samplerate;
    uint32_t byterate;
    uint16_t blockalign;
    uint16_t bitespersample;
};

void check_format(struct fmt *w) {
    if (w->samplerate != 44100) panic("sample rate invalid");
    if (w->numchannels != 1) panic("more than one channel");
    if (w->bitespersample != 16) panic("must be 16 bits per sample");
    if (w->audioformat != 1) panic("cannot handle compressed files");
}


void print_chunk_id(struct chunk *c) {
    cprintf("chunk id: ");
    cprintf("%c", (char) (c->chunkid >> 0) & 0xFF);
    cprintf("%c", (char) (c->chunkid >> 8) & 0xFF);
    cprintf("%c", (char) (c->chunkid >> 16) & 0xFF);
    cprintf("%c ", (char) (c->chunkid >> 24) & 0xFF);
    cprintf("(%x)\n", c->chunkid);
    
}

void next_chunk(struct chunk **c) {
    *c = (struct chunk *)((int8_t *)(*c) + ((*c)->chunksize + sizeof(struct chunk)));
}


// Holder for the square wave beep
#define BUFFER_SIZE (1 << 15)
int16_t b[BUFFER_SIZE];

void
umain(int argc, char **argv)
{
    int i, numsect, numsamples;
    
    cprintf("############################### AUDIO SERVER START ###############################\n");
    // Construct square wave
    for (i = 0; i < BUFFER_SIZE; i++) {
        if ((i /200) % 2) b[i] = 0xFFF;
        else b[i] = -0xFFF;
    }
    
    // play the square wave (beep). Blocks until done.
    sys_sb16_play(b, BUFFER_SIZE);
    
    
    // Read first sector of HDC, verify the wav file
    ide_read(0, b, 1);
    if (AS_CHUNKP(b)->chunkid != RIFF_CHUNK_ID)
        panic("hdc does not contain a valid wav file!");
    
    // Get wav file length
    numsect = ROUNDUP(((struct chunk *) b)->chunksize, SECTSIZE) / SECTSIZE;
    cprintf("WAV file size: %d sectors (%d mb)\n", numsect, (numsect * SECTSIZE) >> 20);
    if (WAV_BUFFER_SIZE < ((struct chunk *) b)->chunksize)
        panic("wav file is too big");
    
    // Copy the wav file to the big buffer
    cprintf("Copying WAV file to memory...\n");
    for (i = 0; i < numsect; i++)
        ide_read(i, wavhead + i * SECTSIZE, 1);
    
    // Procceed to the fmt subchunk
    cprintf("Copying complete. Locating 'fmt' subchunk..\n");
    wavhead = AS_BYTEP(wavbuff) + 12; // strip RIFF header (it has 3 fields of 32bits each)
    while (AS_CHUNKP(wavhead)->chunkid != FMT_CHUNK_ID) {
        next_chunk((struct chunk **)(&wavhead));
    }
    
    // Validate fmt subchunk
    cprintf("Found 'fmt' subchunk. Validating...\n");
    check_format(AS_FMTP(wavhead));
    
    // Procceed to data subchunk
    cprintf("Format is good. Locating 'data' subchunk...\n");
    while (AS_CHUNKP(wavhead)->chunkid != DATA_CHUNK_ID) {
        next_chunk((struct chunk **)(&wavhead));
    }
    
    // Read the length of the data
    numsamples = AS_CHUNKP(wavhead)->chunksize / 2;
    cprintf("Located data chunk. PCM data length: %dB\n", (numsamples * 2));
    
    // Play!
    wavhead += 8; // Strip data chunk header (8 bytes)
    cprintf("Starting playback from %x, playing %d samples\n", wavhead, numsamples);
    sys_sb16_play((int16_t *) wavhead, numsamples);
    
    cprintf("############################### AUDIO SERVER FINISHED ###############################\n");
    
    return;
}
