/* e1000.h - e1000 slim kernel, 26/5/2017, Roei Rosenzweig and Yosef Raisman © */

#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>
#include <kern/e1000_hw.h>
#include <inc/types.h>

extern volatile uint32_t *e1000;

/**
 * Structures and types
 */
struct e1000_status_t {
    bool e1s_fd : 1; /* Full duplex */
    bool e1s_lu : 1; /* Link up */
    char padding1 : 2;
    bool e1s_txoff : 1; /* Transmission pasued */
    char padding2: 1;
    int e1s_speed: 2; /* Link speed setting */
    int padding3 : 22;
} __attribute__((packed));

int e1000_attachfn(struct pci_func *pcif);
int e1000_transmit(char *packet, size_t length);
int e1000_receive(char *packet, size_t len);
void e1000_read_status(struct e1000_status_t *e1000_status);


/**
 * Device constants
 */
#define E1000_VENDOR_ID             (0x8086)
#define E1000_PRODUCT_ID            (0x100e)

/**
 * Misc constants
 */
#define E1000_RECEIVE_PACKET_SIZE   (2048)        // must be one of the predefined sizes in the manual
#define ETH_MAX_PACKET_SIZE         (1518)        // size of ethernet packet, in bytes

#endif	// JOS_KERN_E1000_H
