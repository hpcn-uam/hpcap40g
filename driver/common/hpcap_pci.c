/**
 * @brief Management of the correspondence interface -- fixed port. See
 * section 3.6 Interface numbering in HPCAP_DevGuide.pdf
 */

#include "hpcap_pci.h"
#include "hpcap_debug.h"
#include "hpcap.h"
#include <linux/sort.h>

static uint32_t hpcap_iface_pci_ids[HPCAP_MAX_NIC + 1] = { 0 };

#define ixgbe_build_pci_id(pdev) ((pdev)->bus->number << 24 | (pdev)->devfn)

static int _u32_cmp(const void *lhs, const void *rhs)
{
	uint32_t lhs_integer = *(const uint32_t *)(lhs);
	uint32_t rhs_integer = *(const uint32_t *)(rhs);

	if (lhs_integer < rhs_integer) return -1;
	else if (lhs_integer > rhs_integer) return 1;
	else return 0;
}

void hpcap_set_fixed_iface(const struct pci_dev* pdev, size_t i)
{
	hpcap_iface_pci_ids[i] = ixgbe_build_pci_id(pdev);
}

int hpcap_fix_iface_numbers(const struct pci_device_id* pci_tbl)
{
	struct pci_dev* pdev;
	const struct pci_device_id* dev_id;
	size_t i = 0;

	printdbg(DBG_DRV, "Searching PCI devices for fixing interface numbers\n");

	for (dev_id = pci_tbl; dev_id->vendor != 0; dev_id++) {
		for (pdev = pci_get_device(dev_id->vendor, dev_id->device, NULL);
			 pdev != NULL;
			 pdev = pci_get_device(dev_id->vendor, dev_id->device, pdev), i++) {
			hpcap_set_fixed_iface(pdev, i);
			printdbg(DBG_DRV, "Adapter %s assigned interface number %zu\n", pci_name(pdev), i);
		}
	}

	sort(hpcap_iface_pci_ids, i, sizeof(uint32_t), _u32_cmp, NULL);

	return i;
}

int hpcap_get_iface_number_for(const struct pci_dev* pdev)
{
	uint32_t pci_id = ixgbe_build_pci_id(pdev);
	size_t i;

	for (i = 0; hpcap_iface_pci_ids[i] != 0; i++) {
		if (hpcap_iface_pci_ids[i] == pci_id)
			return i;
	}

	return -1;
}

int hpcap_get_first_free_iface_number(void)
{
	int i;

	for (i = 0; hpcap_iface_pci_ids[i] != 0; i++);

	return i;
}

int hpcap_get_iface_number_or_default(const struct pci_dev* pdev)
{
	int assigned_bd_number = hpcap_get_iface_number_for(pdev);

	if (assigned_bd_number >= 0)
		printdbg(DBG_DRV, "Adapter %s got fixed index %d based on PCI slot allocation.\n", pci_name(pdev), assigned_bd_number);
	else {
		assigned_bd_number = hpcap_get_first_free_iface_number();
		hpcap_set_fixed_iface(pdev, assigned_bd_number);

		printdbg(DBG_DRV, "Fixed index for adapter %s not found. Assigning free index %d.\n", pci_name(pdev), assigned_bd_number);
	}

	return assigned_bd_number;
}
