/* e1000.c - e1000 slim kernel, 26/5/2017, Roei Rosenzweig and Yosef Raisman © */

#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <inc/assert.h>

volatile uint32_t *e1000_addr;

// LAB 6: Your driver code here
int e1000_attachfn(struct pci_func *pcif) {
    
    // Enable the device
    pci_func_enable(pcif);
    
    // Map the device into memory at BAR0
    if ((e1000_addr = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0])) == NULL)
        panic("e1000 failed to map device into memory (MMIO)");
    
    cprintf("GARBAGE? OR LAUGAGE?! - E1000 says: %x\n", e1000_addr[2]);
    return 0;
}
