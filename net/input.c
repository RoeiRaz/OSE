#include "ns.h"

extern union Nsipc nsipcbuf;

#define INPUT_BUFFER_SIZE (2048)

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";
    int r;
    envid_t whom;
    
	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
    while (true) {
        
        // Set the length
        nsipcbuf.pkt.jp_len = INPUT_BUFFER_SIZE;
        
        // Receive packet from device
        while ((r = sys_e1000_receive(nsipcbuf.pkt.jp_data, INPUT_BUFFER_SIZE)) < 0) {
            if (r != -E_RING_EMPTY)
                panic("net/input recv failed");
            
            sys_yield();
        }
        
        ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U | PTE_W | PTE_P);
        
        while((r = ipc_recv(&whom, NULL, NULL)) && whom != ns_envid);
        
        if(r < 0)
            panic("net/input ipc_recv error");
    }
}
