#ifndef JOS_KERN_X86_H
#define JOS_KERN_X86_H

#include <inc/sb16.h>

// All the sizes here are in BYTES, but 'length' and 'size' arguments in functions or variables are in WORDS! (16bit)
#define DMA_BUFFER_SIZE         (1 << 16) // 64Kb
#define DMA_CHUNK_SIZE          (DMA_BUFFER_SIZE >> 1)
#define PEN_BUFFER_SIZE         (1 << 22) // 4Mb
#define PEN_CHUNKS_NUM          (PEN_BUFFER_SIZE / (DMA_BUFFER_SIZE / 2))

// Buffer half the size of the DMA buffer. there are 2 of these, and
// they are used for double buffering in auto init mode of the SB16.
//
// we wrap this buffer in struct, because it makes assignment more comfortable
// and generally looks nicer.
struct chunk_t {
    uint16_t data[DMA_CHUNK_SIZE >> 1];
};

// DMA buffer, consists of 2 chunks, inactive chunk is replaced while
// active chunk is played by the SB16
struct dma_buffer_t {
    struct chunk_t chunk_lo;
    struct chunk_t chunk_hi;
};

void sb16_read_version(struct sb16_version_t *version);
void sb16_init(void);
void sb16_intr(void);

#endif
