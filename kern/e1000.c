/* e1000.c - e1000 slim kernel, 26/5/2017, Roei Rosenzweig and Yosef Raisman © */
// LAB 6: Your driver code here

#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/error.h>

#define E1000_NUM_TX_DESC   (8 * 4)     // must be less than/equal 64 for tests
                                        //to be effective. must be multiple of 8.


/**
 * MMIU addresses and metadata.
 */
physaddr_t e1000addr;
int e1000len;
volatile uint32_t *e1000 = NULL;


// Transmit descriptor array, aligned to page (thus also aligned to 16bytes)
// Note that E1000 reads *physical* addresses.
// Should be zeroed on initialization.
struct e1000_tx_desc 
e1000_tx_desc_array[E1000_NUM_TX_DESC] 
__attribute__ ((aligned (PGSIZE)));

// Reserved buffers for packets
// TODO are 2d arrays in embeded/kernel programming a bad practice?
char e1000_tx_packet_buffers[E1000_NUM_TX_DESC][ETH_MAX_PACKET_SIZE];

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

/*
 * Advances the TDT register to point to the next entry in the circular queue.
 * Clears the DD bit of the current TX entry pointed by TDT.
 * The next entry might be already in use; this function doesn't care.
 * 
 * Used when want to 'send' the current TX entry.
 */
static void
e1000_tx_step() {
    int tx_idx;
    
    // Reads TDT
    tx_idx = e1000r(E1000_TDT);
    
    // Clear DD flag
    e1000_tx_desc_array[tx_idx].upper.fields.status &= ~E1000_TXD_STAT_DD;
    
    // Advances the TDT to the next entry
    e1000w(E1000_TDT, ((tx_idx + 1) % E1000_NUM_TX_DESC));
}

/**
 * initialize the trasmit descriptor in index i.
 * points to the corresponding buffer, set the correct length,
 * enables DD flag, and enables the RS flag.
 */
static void
e1000_tx_desc_init(int i) {
    // reset the descriptor
    memset(&e1000_tx_desc_array[i], 0, sizeof (struct e1000_tx_desc));
    
    // enable DD flag and RS flag
    e1000_tx_desc_array[i].upper.data |= E1000_TXD_STAT_DD;
    e1000_tx_desc_array[i].lower.data |= E1000_TXD_CMD_RS;
    
    // point to the packet buffer (physical address!)
    e1000_tx_desc_array[i].buffer_addr = (uint32_t) PADDR(&e1000_tx_packet_buffers[i]);
    e1000_tx_desc_array[i].lower.flags.length = ETH_MAX_PACKET_SIZE;
}

/*
 * Initializes the device for trasmiting, follows section 14.5 of the manual.
 * Assumes that a MMIO memory for the device was mapped.
 */
static void
e1000_transmit_initialization() {
    int i;
    
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
    
    // Ensure all the STA.DD bits are set in the tx descriptors, to
    // report that they are usable. Also, set the buffer pointer to
    // the corresponding buffer.
    for (i = 0; i < E1000_NUM_TX_DESC; i++)
        e1000_tx_desc_init(i);
}

/* 
 * Trasmits a packet. TODO: alpha version
 * returns 0 on success
 * returns -E1000_E_RING_FUL if the queue is full
 */
int 
e1000_transmit(char *packet, size_t length) {
    struct e1000_tx_desc *tx_desc;
    int tx_index;
    
    // We currently don't allow packets bigger than the size specified by Ethernet.
    assert(length < ETH_MAX_PACKET_SIZE);
    
    // get current tx descriptor and descriptor index
    tx_index = e1000r(E1000_TDT);
    tx_desc = &e1000_tx_desc_array[tx_index];
    
    // check if the current tx descriptor isn't taken. if
    // it does, drop the packet. TODO require upgrade.
    if (! (tx_desc->upper.fields.status & E1000_TXD_STAT_DD))
        return -E1000_E_RING_FULL;
    
    // move the packet into the reserved space (so DMA wont cause race conditions?)
    memmove(&e1000_tx_packet_buffers[tx_index], packet, length);
    
    // set 'end of packet' so that E1000 will actually send it
    e1000_tx_desc_array[tx_index].lower.data |= E1000_TXD_CMD_EOP;
    
    // set the length of the packet in the descriptor
    e1000_tx_desc_array[tx_index].lower.flags.length = length;
    
    // Beam me up, scotty 
    e1000_tx_step();
    
    return 0;
}

/*
 * The PCI attach procedure for the device
 */
int 
e1000_attachfn(struct pci_func *pcif) {
    
    struct e1000_status_t status;
    
    // Enable the device
    pci_func_enable(pcif);
    
    // Store BAR0
    e1000addr = pcif->reg_base[0];
    e1000len = pcif->reg_size[0];
    
    // Map the device into memory at BAR0
    if ((e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0])) == NULL)
        panic("e1000 failed to map device into memory (MMIO)");
    
    e1000_transmit_initialization();
    
    e1000_read_status(&status);
    
    cprintf("E1000 status register:\n");
    cprintf("- Double duplex: %s\n", status.e1s_fd ? "yes" : "no");
    cprintf("- Link: %s\n", status.e1s_lu ? "up" : "down");
    cprintf("- Transmission paused: %s\n", status.e1s_txoff ? "yes" : "no");
    cprintf("- Transsmision speed: %d\n", status.e1s_speed);
    
    char Surrender[1024] = "\
        Mother told me, yes, she told me\
        That I'd meet girls like you\
        She also told me, 'Stay away\
        You'll never know what you'll catch'\
        Just the other day I heard\
        Of a soldier's falling off\
        Some Indonesian junk\
        That's going round\
        \
        Your Mommy's all right\
        Your Daddy's all right\
        They just seem a little weird\
        Surrender\
        Surrender\
        But don't give yourself away\
        Hey, heeeeeey\
        \
        Father says, 'Your mother's right\
        She's really up on things\
        Before we married, Mommy served\
        In the WACS in the Philippines\
        Now, I had heard the WACS recruited\
        Old maids for the war\
        But mommy isn't one of those\
        I've known her all these years\
        \
        Your Mommy's all right\
        Your Daddy's all right\
        They just seem a little weird\
        Surrender\
        Surrender\
        But don't give yourself away\
        Hey, heeeeeey";
    //e1000_transmit(Surrender, 1024); //TODO
    return 0;
}



void 
e1000_read_status(struct e1000_status_t *e1000_status) {
    uint32_t status;
    if (! e1000_status)
        panic("e1000 driver: trying to read status into NULL");
    status = e1000r(E1000_STATUS);
    *e1000_status = *((struct e1000_status_t *)(&status));
}
