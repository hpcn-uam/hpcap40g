#include "kcompat.h"

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pci.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0) && NR_CPUS > 1)

/**
 * cpumask_set_cpu_local_first - set i'th cpu with local numa cpu's first
 *
 * @param i index number
 * @param numa_node local numa_node
 * @param dstp cpumask with the relevant cpu bit set according to the policy
 *
 * This function sets the cpumask according to a numa aware policy.
 * cpumask could be used as an affinity hint for the IRQ related to a
 * queue. When the policy is to spread queues across cores - local cores
 * first.
 *
 * @return 0 on success, -ENOMEM for no memory, and -EAGAIN when failed to set
 * the cpu bit and need to re-call the function.
 *
 * @note This weird function is introduced in 3.16 (see https://lkml.org/lkml/2014/5/25/55)
 *       but someone decided that it should be deprecated for 4.1 (https://github.com/torvalds/linux/commit/f36963c9d3f6)
 *
 * 		 This is an implementation for compatibility on kernel versions previous to 3.16.
 * 		 kcompat.h has the implementation for kerneles after 4.1.
 */
int cpumask_set_cpu_local_first(int i, int numa_node, cpumask_t *dstp)
{
	cpumask_var_t mask;
	int cpu;
	int ret = 0;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	i %= num_online_cpus();

	if (numa_node == -1 || !cpumask_of_node(numa_node)) {
		/* Use all online cpu's for non numa aware system */
		cpumask_copy(mask, cpu_online_mask);
	} else {
		int n;

		cpumask_and(mask,
					cpumask_of_node(numa_node), cpu_online_mask);

		n = cpumask_weight(mask);

		if (i >= n) {
			i -= n;

			/* If index > number of local cpu's, mask out local
			 * cpu's
			 */
			cpumask_andnot(mask, cpu_online_mask, mask);
		}
	}

	for_each_cpu(cpu, mask) {
		if (--i < 0)
			goto out;
	}

	ret = -EAGAIN;

out:
	free_cpumask_var(mask);

	if (!ret)
		cpumask_set_cpu(cpu, dstp);

	return ret;
}

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0))
void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0))

const unsigned char pcie_link_speed[] = {
	PCI_SPEED_UNKNOWN,		/* 0 */
	PCIE_SPEED_2_5GT,		/* 1 */
	PCIE_SPEED_5_0GT,		/* 2 */
	PCIE_SPEED_8_0GT,		/* 3 */
	PCI_SPEED_UNKNOWN,		/* 4 */
	PCI_SPEED_UNKNOWN,		/* 5 */
	PCI_SPEED_UNKNOWN,		/* 6 */
	PCI_SPEED_UNKNOWN,		/* 7 */
	PCI_SPEED_UNKNOWN,		/* 8 */
	PCI_SPEED_UNKNOWN,		/* 9 */
	PCI_SPEED_UNKNOWN,		/* A */
	PCI_SPEED_UNKNOWN,		/* B */
	PCI_SPEED_UNKNOWN,		/* C */
	PCI_SPEED_UNKNOWN,		/* D */
	PCI_SPEED_UNKNOWN,		/* E */
	PCI_SPEED_UNKNOWN		/* F */
};


/**
 * pcie_get_minimum_link - determine minimum link settings of a PCI device
 * @param dev PCI device to query
 * @param speed storage for minimum speed
 * @param width storage for minimum width
 *
 * This function will walk up the PCI device chain and determine the minimum
 * link width and speed of the device.
 */
int pcie_get_minimum_link(struct pci_dev *dev, enum pci_bus_speed *speed,
						  enum pcie_link_width *width)
{
	int ret;

	*speed = PCI_SPEED_UNKNOWN;
	*width = PCIE_LNK_WIDTH_UNKNOWN;

	while (dev) {
		u16 lnksta;
		enum pci_bus_speed next_speed;
		enum pcie_link_width next_width;

		ret = pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnksta);

		if (ret)
			return ret;

		next_speed = pcie_link_speed[lnksta & PCI_EXP_LNKSTA_CLS];
		next_width = (lnksta & PCI_EXP_LNKSTA_NLW) >>
					 PCI_EXP_LNKSTA_NLW_SHIFT;

		if (next_speed < *speed)
			*speed = next_speed;

		if (next_width < *width)
			*width = next_width;

		dev = dev->bus->self;
	}

	return 0;
}
#endif
