#include <kern/e1000.h>
#include <kern/pci.h>

// LAB 6: Your driver code here
int e1000_attachfn(struct pci_func *pcif) {
    // Currently, we only enable the device without doing anything else.
    pci_func_enable(pcif);
    return 0;
}
