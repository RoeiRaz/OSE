#include "audio.h"
int16_t arr[] = {
381,549,735,875,946,967,918,803,626,401,174,-34,-23,-43,-58,-67,-76,-85,-90,-93,-92,-87,-79,-67,-56,-49,-46,-48,-50,-51,-50,-47,-41,-35,-29,-24,-20,-18,-18,-19,-25,-32,-37,-4};
#include <inc/x86.h>
#include <inc/string.h>



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
