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

/**
 * PCI attach function
 * TODO maybe we should rename this to something nicer
 */
int e1000_attachfn(struct pci_func *pcif);

/**
 * Reads the status register from the device into 'struct e1000_status'
 */
void e1000_read_status(struct e1000_status_t *e1000_status);


/**
 * Device constants
 */
#define E1000_VENDOR_ID         (0x8086)
#define E1000_PRODUCT_ID        (0x100e)


#endif	// JOS_KERN_E1000_H
