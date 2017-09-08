/**
 * @brief Management of the correspondence interface -- fixed port. See
 * section 3.5 Interface naming in HPCAP_DevGuide.pdf
 *
 * @addtogroup HPCAP
 * @{
 */


#ifndef HPCAP_PCI
#define HPCAP_PCI

#include <linux/pci.h>

/**
 * Assign an interface index to the given PCI device in the internal
 * table.
 * @param pdev PCI device.
 * @param i    Interface index.
 */
void hpcap_set_fixed_iface(const struct pci_dev* pdev, size_t i);

/**
 * Find PCI devices connected to the system based on the PCI device ID
 * table, then assign ascendent interface indexes based on the bus, device
 * and function numbers, in that order.
 * @param  pci_tbl PCI device IDs table. Last entry must have vendor == 0.
 * @return         Number of PCI devices found.
 */
int hpcap_fix_iface_numbers(const struct pci_device_id* pci_tbl);

/**
 * Get the assigned interface for the given PCI device.
 * @param  pdev PCI device.
 * @return      Assigned interface or -1 if it wasn't found.
 */
int hpcap_get_iface_number_for(const struct pci_dev* pdev);

/**
 * Return the first adapter index that is not reserved for any PCI device.
 * @return  Adapter index.
 * @note This function does not modify the internal table. You should call
 * hpcap_set_fixed_iface to allocate the number you received.
 */
int hpcap_get_first_free_iface_number(void);

/**
 * Get the interface number for the given PCI device. If the device
 * has not any interface number allocated, get the first free and allocate it.
 * @param  pdev PCI device.
 * @return      interface number.
 */
int hpcap_get_iface_number_or_default(const struct pci_dev* pdev);

/** @} */

#endif
