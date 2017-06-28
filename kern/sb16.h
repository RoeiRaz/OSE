#ifndef JOS_KERN_X86_H
#define JOS_KERN_X86_H

#include <inc/sb16.h>
#include <kern/env.h>

// All the sizes here are in BYTES, but 'length' and 'size' arguments in functions or variables are in WORDS! (16bit)
#define DMA_BUFFER_SIZE_WORDS         (1 << 15) // 64Kb
#define SB16_BUFFER_SIZE_WORDS        (DMA_BUFFER_SIZE_WORDS << 1) // DMA_BUFFER_SIZE_WORDS * 2

void sb16_read_version(struct sb16_version_t *version);
void sb16_init(void);
void sb16_intr(void);
int sb16_play(struct Env *env, int16_t *data, size_t len_words);

#endif
