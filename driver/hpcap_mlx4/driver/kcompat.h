/**
 * File for sorting out incompatibilities in kernel versions.
 */

#ifndef KCOMPAT
#define KCOMPAT

#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

/**
 * See:
 * 	https://github.com/torvalds/linux/commit/62026aedaacedbe1ffe94a3599ad4acd8ecdf587
 * 	https://github.com/torvalds/linux/commit/c32f74ab2872994bc8336ed367313da3139350ca
 * 	https://lkml.org/lkml/2013/7/10/327
 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0) )
#define reinit_completion(ptr) INIT_COMPLETION(*(ptr))
#endif


/** cpumask_set_cpu_local_first: this is a function inserted in
 * 3.16 by the Mellanox devs (https://lkml.org/lkml/2014/5/25/55) and
 * refactored in 4.1 because someone thought it was crap (https://github.com/torvalds/linux/commit/f36963c9d3f6)
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0) )
#define cpumask_set_cpu_local_first(i, node, dstp) cpumask_set_cpu(cpumask_local_spread(i, node), dstp)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
int cpumask_set_cpu_local_first(int i, int numa_node, cpumask_t *dstp);
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0))
void kvfree(const void *addr);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#define skb_vlan_tag_present(__skb)     ((__skb)->vlan_tci & VLAN_TAG_PRESENT)
#define skb_vlan_tag_get(__skb)         ((__skb)->vlan_tci & ~VLAN_TAG_PRESENT)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0))

extern const unsigned char pcie_link_speed[];

int pcie_get_minimum_link(struct pci_dev *dev, enum pci_bus_speed *speed,
						  enum pcie_link_width *width);

#endif

#endif
