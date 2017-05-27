#include "ns.h"

extern union Nsipc nsipcbuf;

#define NUM_INPUT_PAGES (64)

union Nsipc nsipcbufs[NUM_INPUT_PAGES] __attribute__((aligned(PGSIZE)));

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";
    int r;
    envid_t whom;
    int idx = 0;
    
    
	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
    while (true) {
        
        nsipcbufs[idx].pkt.jp_data[0] = 0;
        
        // Receive packet from device
        while ((r = sys_e1000_receive(nsipcbufs[idx].pkt.jp_data, PGSIZE - sizeof (size_t))) < 0) {
            if (r != -E_RING_EMPTY)
                panic("net/input recv failed");
            
            sys_yield();
        }
        cprintf("received\n");
        
         // Set the length
        nsipcbufs[idx].pkt.jp_len = r;
        
        // Set input to network server
        ipc_send(ns_envid, NSREQ_INPUT, &nsipcbufs[idx], PTE_U | PTE_W | PTE_P);
        
        idx = (idx + 1) % NUM_INPUT_PAGES;
        
        sys_yield();
    }
}
