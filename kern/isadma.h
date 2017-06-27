#ifndef JOS_INC_ISADMA_H
#define JOS_INC_ISADMA_H

#include <inc/types.h>

#define ISADMA_MASTER_BASE              (0xC0)
#define ISADMA_MASTER_SCMR              (0xD4)
#define ISADMA_MASTER_MODE              (0xD6)
#define ISADMA_MASTER_FLFP              (0xD8)
#define ISADMA_CHANNEL_OFFSET           (0x04)

#define ISADMA_MODE_DMND                (0x0)
#define ISADMA_MODE_SNGL                (0x1)
#define ISADMA_MODE_BLCK                (0x2)

#define ISADMA_TYPE_P2M                 (0x1)
#define ISADMA_TYPE_M2P                 (0x2)

void isadma_set_masked(int ch, bool masked);
void isadma_set_address(int ch, physaddr_t addr, int len);
void isadma_set_mode(int ch, int type, bool auto_reset, bool reverse, int mode);

#endif
