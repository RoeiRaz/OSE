/* e1000.c - e1000 slim kernel, 26/5/2017, Roei Rosenzweig and Yosef Raisman © */
// LAB 6: Your driver code here

#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <kern/picirq.h>
#include <kern/env.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/env.h>
#include <inc/trap.h>

#define CACHE_LINE_SIZE (128)

#define E1000_NUM_TX_DESC   (8 * 4)     // must be less than/equal 64 for tests
                                        //to be effective. must be multiple of 8.
                                        
#define E1000_NUM_RX_DESC   (8 * 16)     // atleast 128. must be multiple of 8.     


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

// Receive descriptor array, aligned to page (thus also aligned to 16bytes)
// Note that E1000 reads *physical* addresses.
// Should be zeroed on initialization.
struct e1000_rx_desc 
e1000_rx_desc_array[E1000_NUM_RX_DESC] 
__attribute__ ((aligned (PGSIZE)));

// Reserved buffers for transmit packets 
char 
e1000_tx_packet_buffers[E1000_NUM_TX_DESC][ETH_MAX_PACKET_SIZE]
__attribute__((aligned (CACHE_LINE_SIZE)));

// Reserved buffers for receive packets
char 
e1000_rx_packet_buffers[E1000_NUM_RX_DESC][E1000_RECEIVE_PACKET_SIZE]
__attribute__((aligned (CACHE_LINE_SIZE)));

// Hardcoded MAC address of this device
union hwaddr hwaddr = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56, 0x0, 0x0}};

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

/*
 * Advances the RDT register to point to the next entry in the circular queue.
 * Clears the DD bit of the RX entry afterwards.
 * The next entry might be already in use; this function doesn't care.
 * 
 * Used when want to 'recv' the next RX entry.
 */
static void
e1000_rx_step() {
    int rx_idx;
    
    // Reads RDT
    rx_idx = e1000r(E1000_RDT);
    
    // Advances the RDT to the next entry
    e1000w(E1000_RDT, ((rx_idx + 1) % E1000_NUM_RX_DESC));
    
    // Clear DD flag
    e1000_rx_desc_array[rx_idx].status &= ~E1000_RXD_STAT_DD;
}

/*
 * Returns pointer to the next rx entry.
 * Used to check if there is awaiting packet for receiving.
 */
static struct e1000_rx_desc *
e1000_rx_follow() {
   return &e1000_rx_desc_array[(e1000r(E1000_RDT) + 1) % E1000_NUM_RX_DESC];
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

/**
 * initialize the receive descriptor in index i.
 * points to the corresponding buffer, set the correct length,
 * zeroes everything else.
 */
static void
e1000_rx_desc_init(int i) {
    // reset the descriptor
    memset(&e1000_rx_desc_array[i], 0, sizeof (struct e1000_rx_desc));
    
    // point to the packet buffer (physical address!)
    e1000_rx_desc_array[i].buffer_addr = (uint32_t) PADDR(&e1000_rx_packet_buffers[i]);
    e1000_rx_desc_array[i].length = E1000_RECEIVE_PACKET_SIZE;
}

/*
 * Uploads the MAC address to the 'hwaddr' union.
 */
static void
e1000_hwaddr_initialization() {
    hwaddr.words.word1 = e1000_read_eeprom(0);
    hwaddr.words.word2 = e1000_read_eeprom(1);
    hwaddr.words.word3 = e1000_read_eeprom(2);
    hwaddr.words.word4 = e1000_read_eeprom(3);
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
 * Initializes the device for receiving, follows section 14.4 of the manual.
 * Assumes that a MMIO memory for the device was mapped.
 */
static void
e1000_receive_initialization() {
    int i;
    
    // Set RAL and RAH to the hardcoded MAC address.
    e1000w(E1000_RA, hwaddr.words.word1 | (hwaddr.words.word2 << 16));
    e1000w(E1000_RA + 4, hwaddr.words.word3 | (hwaddr.words.word4 << 16));
    
    // Set 'Receive Valid' of RAH
    e1000s(E1000_RA + 4, E1000_RAH_AV, true);
    
    // Initialize MTA (Multicast table array.?) to zero.
    // We don't even support multicast
    e1000w(E1000_MTA, (0));
    
    // Allow interrupts
    e1000w(E1000_IMS, (E1000_IMS_RXT0));
    e1000r(E1000_ICR);
    
    // Set the receive descriptor base address. we are in a 32bit machine, 
    // so we can just use RDBAL and ignore RDBAH. the address is aligned to 16bytes.
    e1000w(E1000_RDBAL, PADDR(e1000_rx_desc_array));
    
    // Set the transmit descriptor length.
    e1000w(E1000_RDLEN, sizeof(e1000_rx_desc_array));
    
    // Reset the head/tail of the receive queue.
    // TODO require further investigation.
    e1000w(E1000_RDH, (0));
    e1000w(E1000_RDT, (E1000_NUM_RX_DESC - 1));
    
    // Zero everything in the Receive Control register first.
    e1000w(E1000_RCTL, (0));
    
    // Set buffer size to 2048
    e1000s(E1000_RCTL, E1000_RCTL_SZ_2048, true);
    
    // Set strip CRC
    e1000s(E1000_RCTL, E1000_RCTL_SECRC, true);
    
    // Ensure all the STA.DD bits are set in the tx descriptors, to
    // report that they are usable. Also, set the buffer pointer to
    // the corresponding buffer.
    for (i = 0; i < E1000_NUM_RX_DESC; i++)
        e1000_rx_desc_init(i);
    
    // Set enable bit on receive control register RCTL.EN = 1
    e1000s(E1000_RCTL, E1000_RCTL_EN, true);
    
    e1000w(E1000_ICS, E1000_ICS_RXT0);
}

/* 
 * Trasmits a packet.
 * returns 0 on success
 * returns -E1000_E_RING_FULL if the queue is full
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
        return -E_RING_FULL;
    
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

/**
 * Receives a packet.
 * Returns (non-negative) length of packet on success.
 * Returns -E_INVAL if the passed buffer is not big enough.
 * Returns -E_RING_EMPTY if the queue is empty.
 */
int
e1000_receive(char *packet, size_t len) {
    struct e1000_rx_desc *rx_desc;
    int rx_idx;
    int length;
    
    if (len < E1000_RECEIVE_PACKET_SIZE)
        return -E_INVAL;
    
    // read the next receeive descriptor
    rx_desc = e1000_rx_follow();
    rx_idx = rx_desc - e1000_rx_desc_array;
    
    // Checkc if the ring is empty
    if (! (rx_desc->status & E1000_RXD_STAT_DD))
        return -E_RING_EMPTY;
    
    // Read length
    length = rx_desc->length;
    
    // Move the packet to the user buffer
    memcpy(packet, &e1000_rx_packet_buffers[rx_idx], E1000_RECEIVE_PACKET_SIZE);
    
    // TODO catch phrase
    e1000_rx_step();
    
    return length;
}

/**
 * Checks whether the RX queue is empty.
 */
bool
e1000_rx_empty() {
    struct e1000_rx_desc *rx_desc;
    
    // read the next receeive descriptor
    rx_desc = e1000_rx_follow();
    
    return (! (rx_desc->status & E1000_RXD_STAT_DD));
}

/**
 * Interrupt handler. We keep it simple and static: the interrupt handler
 * checks for process that is waiting to receive packet, and if there is
 * an incoming available packet, injects it into the process structure 
 * before making it runnable again.
 */
void
e1000_intr(void) {
    struct Env *e;
    int i;
    int r;
    physaddr_t prev_cr3;
    
    // Read the cause for the interrupt. also flushes the register, so we must handle all
    // the interrupts that accumulated (it is possible that we accumulated more than 1 type!)
    e1000r(E1000_ICR);
    
    
    // search for applicable env.
    for (i = 0; i < NENV; i++)
        if (envs[i].env_status != ENV_FREE && envs[i].env_e1000_receiving)
            break;
    
    
    // if we didn't find one, finish here.
    if (i == NENV)
        return;
    
    prev_cr3 = rcr3();
    lcr3(PADDR(envs[i].env_pgdir));

    if ((r = e1000_receive(envs[i].env_e1000_packet, envs[i].env_e1000_size)) < 0) {
        if (r != -E_RING_EMPTY) panic("receive error: %e", r);
        lcr3(prev_cr3);
        return;
    }
    
    lcr3(prev_cr3);
    
    // If we received the packet successfully, inject it and resume running the env.
    envs[i].env_status = ENV_RUNNABLE;
    envs[i].env_e1000_receiving = false;
    envs[i].env_e1000_size = r;
    
    // returns the size of the packet
    envs[i].env_tf.tf_regs.reg_eax = r;
}

/*
 * Generate a packet timer interrupt from the device.
 */
void
e1000_gen_intr(void) {
    e1000w(E1000_ICS, E1000_ICS_RXT0);
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
    
    // Enable relevant IRQ in the PIC
    irq_setmask_8259A(irq_mask_8259A & (~(1 << pcif->irq_line)));
    
    e1000_hwaddr_initialization();
    e1000_transmit_initialization();
    e1000_receive_initialization();
    
    // TODO remove this PoC
    uint16_t mac12 = e1000_read_eeprom(0);

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

uint16_t
e1000_read_eeprom(int address) {
    e1000w(E1000_EERD, E1000_EERD_START | ((address << E1000_EERD_ADDR_OFFSET) & E1000_EERD_ADDR));
    while (! (e1000r(E1000_EERD) & E1000_EERD_DONE));
    return (e1000r(E1000_EERD) & E1000_EERD_DATA) >> E1000_EERD_DATA_OFFSET;  
}

void
e1000_read_hwaddr(union hwaddr *dst_hwaddr) {
    *dst_hwaddr = hwaddr;
}

void 
e1000_read_status(struct e1000_status_t *e1000_status) {
    uint32_t status;
    if (! e1000_status)
        panic("e1000 driver: trying to read status into NULL");
    status = e1000r(E1000_STATUS);
    *e1000_status = *((struct e1000_status_t *)(&status));
}
