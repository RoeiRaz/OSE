/* e1000.h - e1000 slim kernel, 26/5/2017, Roei Rosenzweig and Yosef Raisman © */

#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

/**
 * PCI attach function
 * TODO maybe we should rename this to something nicer
 */
int e1000_attachfn(struct pci_func *pcif);

/**
 * Device constants
 */
#define E1000_VENDOR_ID         (0x8086) // noice
#define E1000_PRODUCT_ID        (0x100e) // this is really cool

#endif	// JOS_KERN_E1000_H
