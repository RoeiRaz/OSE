/* e1000.c - e1000 slim kernel, 26/5/2017, Roei Rosenzweig and Yosef Raisman © */
// LAB 6: Your driver code here

#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <inc/assert.h>

#define E1000_NUM_TX_DESC (8 * 4) // must be less than/equal 64 for tests to be effective. must be multiple of 8.

physaddr_t e1000addr;
int e1000len;
volatile uint32_t *e1000 = NULL;


// Transmit descriptor array, aligned to page (thus also aligned to 16bytes)
// Note that E1000 reads *physical* addresses.
struct e1000_tx_desc 
e1000_tx_desc_array[E1000_NUM_TX_DESC] 
__attribute__ ((aligned (PGSIZE)));


/*
 * Read a register, returns it as a uint32_t.
 * offset is given in bytes, so we can use the values defined in e1000_hw.h.
 */
static uint32_t 
e1000r(int offset) {
    assert(offset >= 0);
    assert((offset & 0x3) == 0);
    assert(offset < e1000len);
    return *(uint32_t *)(((char *) e1000) + offset);
}

/*
 * Writes a whole register.
 * offset is given in bytes, so we can use the values defined in e1000_hw.h.
 */
static void 
e1000w(int offset, uint32_t val) {
    assert(offset >= 0);
    assert((offset & 0x3) == 0);
    assert(offset < e1000len);
    *(uint32_t *)(((char *) e1000) + offset) = val;
}

/*
 * Sets masked bits in the register given by 'offset' to the value 'val'.
 * offset is given in bytes, so we can use the values defined in e1000_hw.h.
 */
static void 
e1000s(int offset, uint32_t mask, bool val) {
    if (val)
        e1000w(offset, e1000r(offset) | mask);
    else
        e1000w(offset, e1000r(offset) & (~mask));
}

/*
 * Checks if one of the masked bit in the register is up.
 * offset is given in bytes, so we can use the values defined in e1000_hw.h.
 */
static bool 
e1000c(int offset, uint32_t mask) {
    return e1000r(offset) & mask;
}

/*
 * Sets a field in register 'offset', using 'location' as bit offset from bit 0.
 * offset is given in bytes, so we can use the values defined in e1000_hw.h.
 */
static void 
e1000woff(int offset, uint32_t mask, int location, uint32_t value) {
    assert(location >= 0);
    assert(location < sizeof (uint32_t) * 8);
    e1000w(offset, (e1000r(offset) & (~mask)) | ((value << location) & mask));
}

static void
transmit_initialization() {
    // Set the transmit descriptor base address. we are in a 32bit machine, 
    // so we can just use TDBAL and ignore TDBAH. the address is aligned to 16bytes.
    e1000w(E1000_TDBAL ,PADDR(e1000_tx_desc_array));
    
    // Set the transmit descriptor length.
    // TODO check it again, very likely to lead to subtle bugs
    e1000w(E1000_TDLEN, sizeof(e1000_tx_desc_array));
    
    // Ensure transmit head/tail are zeroed.
    e1000w(E1000_TDH, (0));
    e1000w(E1000_TDT, (0));
    
    // Set enable bit on trasmit control register TCTL.EN = 1
    e1000s(E1000_TCTL, E1000_TCTL_EN, true);
    
    // Set padding for short packedts TCTL.PSP = 1
    e1000s(E1000_TCTL, E1000_TCTL_PSP, true);
    
    // Set collision distance TCTL.COLD = 0x40
    e1000woff(E1000_TCTL, E1000_TCTL_COLD, E1000_TCTL_COLD_OFFSET, 0x40);
    
    // Resete TIPG to zero for backward compatibillity (?)
    e1000w(E1000_TIPG, 0);
    
    // Set IPGT values according to IEEE802.3, full duplex link
    e1000woff(E1000_TIPG, E1000_TIPG_IPGT, E1000_TIPG_IPGT_OFFSET, 10);
    e1000woff(E1000_TIPG, E1000_TIPG_IPGR1, E1000_TIPG_IPGR1_OFFSET, 4);
    e1000woff(E1000_TIPG, E1000_TIPG_IPGR2, E1000_TIPG_IPGR2_OFFSET, 6);
}

int e1000_attachfn(struct pci_func *pcif) {
    
    struct e1000_status_t status;
    
    // Enable the device
    pci_func_enable(pcif);
    
    // Store BAR0
    e1000addr = pcif->reg_base[0];
    e1000len = pcif->reg_size[0];
    
    // Map the device into memory at BAR0
    if ((e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0])) == NULL)
        panic("e1000 failed to map device into memory (MMIO)");
    
    transmit_initialization();
    
    e1000_read_status(&status);
    
    cprintf("E1000 status register:\n");
    cprintf("- Double duplex: %s\n", status.e1s_fd ? "yes" : "no");
    cprintf("- Link: %s\n", status.e1s_lu ? "up" : "down");
    cprintf("- Transmission paused: %s\n", status.e1s_txoff ? "yes" : "no");
    cprintf("- Transsmision speed: %d\n", status.e1s_speed);
    
    return 0;
}



void e1000_read_status(struct e1000_status_t *e1000_status) {
    uint32_t status;
    if (! e1000_status)
        panic("e1000 driver: trying to read status into NULL");
    status = e1000r(E1000_STATUS);
    *e1000_status = *((struct e1000_status_t *)(&status));
}
