// HARD CODED WAV DATA CHUNK START
#include "audio.h"
int16_t arr[] = {0, 0};
// HARD CODED WAV DATA CHUNK END

#include <inc/x86.h>
#include <inc/string.h>

#include "audio.h"

#define BUFFER_SIZE (1 << 15)

int16_t b[BUFFER_SIZE];

void
umain(int argc, char **argv)
{
    int i;
    
    cprintf("###############################3 AUDIO SERVER START\n");
    for (i = 0; i < BUFFER_SIZE; i++) {
        if ((i /200) % 2) b[i] = 0xFFF;
        else b[i] = -0xFFF;
    }
    sys_sb16_play(b, BUFFER_SIZE);
    
    cprintf("###############################3 AUDIO SERVER FINISHED\n");
    
    sys_sb16_play(arr, sizeof(arr) / 2);
    return;
}
