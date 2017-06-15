#include "ns.h"
#include <inc/lib.h>
#include <inc/env.h>

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
    envid_t envid;
    int value;
    int r;
    
    
	binaryname = "ns_output";
    
    cprintf("%s -> %x\n", binaryname, thisenv->env_id);
    
    cprintf("ns_output env type = %d\n", thisenv->env_type);
    
	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
    while (true) {
        // Wait for IPC input.
        if ((value = ipc_recv(&envid, &nsipcbuf, NULL)) < 0)
            panic("net/output: cannot recv");
        
        // If we received something not from the ns, ignore it.
        if (envid != ns_envid)
            continue;
        
        if (value != NSREQ_OUTPUT)
            continue;
        
        // beam me up, Scotty
        while ((r = sys_e1000_transmit(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) < 0) {
            if (r != -E_RING_FULL)
                panic("net/output: transmit error");
            
            sys_yield();
        }
    }
}
