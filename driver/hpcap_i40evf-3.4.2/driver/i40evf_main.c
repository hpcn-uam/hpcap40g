/*******************************************************************************
 *
 * Intel(R) 40-10 Gigabit Ethernet Virtual Function Driver
 * Copyright(c) 2013 - 2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include "i40evf.h"
#include "i40e_helper.h"
#include "i40e_prototype.h"
#include "i40evf_client.h"

/* All i40evf tracepoints are defined by the include below, which must
 * be included exactly once across the whole kernel with
 * CREATE_TRACE_POINTS defined
 */
#define CREATE_TRACE_POINTS
#include "i40e_trace.h"

#ifdef DEV_HPCAP
#include "driver_hpcap.h"
#include "hpcap.h"
#include "hpcap_version.h"
#include "hpcap_pci.h"
#include "hpcap_params.h"
#endif /* DEV_HPCAP */

static int i40evf_setup_all_tx_resources(struct i40evf_adapter *adapter);
static int i40evf_setup_all_rx_resources(struct i40evf_adapter *adapter);
static int i40evf_close(struct net_device *netdev);

char i40evf_driver_name[] = "i40evf";
static const char i40evf_driver_string[] =
	"Intel(R) 40-10 Gigabit Virtual Function Network Driver";

#ifndef DRV_VERSION_LOCAL
#define DRV_VERSION_LOCAL
#endif /* DRV_VERSION_LOCAL */

#define DRV_VF_VERSION_DESC ""

#define DRV_VERSION_MAJOR 3
#define DRV_VERSION_MINOR 4
#define DRV_VERSION_BUILD 2
#define DRV_VERSION __stringify(DRV_VERSION_MAJOR) "." \
	     __stringify(DRV_VERSION_MINOR) "." \
	     __stringify(DRV_VERSION_BUILD) \
	     DRV_VF_VERSION_DESC __stringify(DRV_VERSION_LOCAL)
const char i40evf_driver_version[] = DRV_VERSION;
static const char i40evf_copyright[] =
	"Copyright(c) 2013 - 2017 Intel Corporation.";

/* i40evf_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id i40evf_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_VF), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_VF_HV), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_X722_VF), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_ADAPTIVE_VF), 0},
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, i40evf_pci_tbl);

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) 40-10 Gigabit Ethernet Virtual Function Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static struct workqueue_struct *i40evf_wq;

#ifdef DEV_HPCAP
extern int adapters_found;
extern struct i40evf_adapter* adapters[HPCAP_MAX_NIC];
#endif /* DEV_HPCAP */

/**
 * i40evf_schedule_reset - Set the flags and schedule a reset event
 * @adapter: board private structure
 **/
void i40evf_schedule_reset(struct i40evf_adapter *adapter)
{
	if (!(adapter->flags &
		  (I40EVF_FLAG_RESET_PENDING | I40EVF_FLAG_RESET_NEEDED))) {
		adapter->flags |= I40EVF_FLAG_RESET_NEEDED;
		schedule_work(&adapter->reset_task);
	}
}

/**
 * i40evf_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void i40evf_tx_timeout(struct net_device *netdev)
{
#ifdef DEV_HPCAP
	int i;
#endif

	struct i40evf_adapter *adapter = netdev_priv(netdev);

#ifdef DEV_HPCAP

	if (is_hpcap_adapter(adapter)) {
		// HPCAP adapters don't transmit anything, so just avoid those nasty timeout messages.
		for (i = 0; i < netdev->num_tx_queues; i++) {
			struct netdev_queue *q;
			q = netdev_get_tx_queue(netdev, i);
			q->trans_start = jiffies + netdev->watchdog_timeo * 10;
		}

		return;
	}

#endif

	adapter->tx_timeout_count++;
	i40evf_schedule_reset(adapter);
}

/**
 * i40evf_misc_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static void i40evf_misc_irq_disable(struct i40evf_adapter *adapter)
{
	struct i40e_hw *hw = &adapter->hw;

	if (!adapter->msix_entries)
		return;

	wr32(hw, I40E_VFINT_DYN_CTL01, 0);

	/* read flush */
	rd32(hw, I40E_VFGEN_RSTAT);

	synchronize_irq(adapter->msix_entries[0].vector);
}

/**
 * i40evf_misc_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/
static void i40evf_misc_irq_enable(struct i40evf_adapter *adapter)
{
	struct i40e_hw *hw = &adapter->hw;

	wr32(hw, I40E_VFINT_DYN_CTL01, I40E_VFINT_DYN_CTL01_INTENA_MASK |
		 I40E_VFINT_DYN_CTL01_ITR_INDX_MASK);
	wr32(hw, I40E_VFINT_ICR0_ENA1, I40E_VFINT_ICR0_ENA1_ADMINQ_MASK);

	/* read flush */
	rd32(hw, I40E_VFGEN_RSTAT);
}

/**
 * i40evf_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static void i40evf_irq_disable(struct i40evf_adapter *adapter)
{
	int i;
	struct i40e_hw *hw = &adapter->hw;

	if (!adapter->msix_entries)
		return;

	for (i = 1; i < adapter->num_msix_vectors; i++) {
		wr32(hw, I40E_VFINT_DYN_CTLN1(i - 1), 0);
		synchronize_irq(adapter->msix_entries[i].vector);
	}

	/* read flush */
	rd32(hw, I40E_VFGEN_RSTAT);
}

/**
 * i40evf_irq_enable_queues - Enable interrupt for specified queues
 * @adapter: board private structure
 * @mask: bitmap of queues to enable
 **/
void i40evf_irq_enable_queues(struct i40evf_adapter *adapter, u32 mask)
{
	struct i40e_hw *hw = &adapter->hw;
	int i;

	for (i = 1; i < adapter->num_msix_vectors; i++) {
		if (mask & BIT(i - 1)) {
			wr32(hw, I40E_VFINT_DYN_CTLN1(i - 1),
				 I40E_VFINT_DYN_CTLN1_INTENA_MASK |
				 I40E_VFINT_DYN_CTLN1_ITR_INDX_MASK);
		}
	}
}

/**
 * i40evf_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 * @flush: boolean value whether to run rd32()
 **/
void i40evf_irq_enable(struct i40evf_adapter *adapter, bool flush)
{
	struct i40e_hw *hw = &adapter->hw;

	i40evf_misc_irq_enable(adapter);
	i40evf_irq_enable_queues(adapter, ~0);

	if (flush)
		rd32(hw, I40E_VFGEN_RSTAT);
}

/**
 * i40evf_msix_aq - Interrupt handler for vector 0
 * @irq: interrupt number
 * @data: pointer to netdev
 **/
static irqreturn_t i40evf_msix_aq(int irq, void *data)
{
	struct net_device *netdev = data;
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40e_hw *hw = &adapter->hw;

	/* handle non-queue interrupts */
	rd32(hw, I40E_VFINT_ICR01);
	rd32(hw, I40E_VFINT_ICR0_ENA1);

	/* schedule work on the private workqueue */
	schedule_work(&adapter->adminq_task);

	return IRQ_HANDLED;
}

/**
 * i40evf_msix_clean_rings - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 **/
static irqreturn_t i40evf_msix_clean_rings(int irq, void *data)
{
	struct i40e_q_vector *q_vector = data;

	if (!q_vector->tx.ring && !q_vector->rx.ring)
		return IRQ_HANDLED;

#ifdef DEV_HPCAP

	if (is_hpcap_adapter(q_vector->adapter))
		return IRQ_HANDLED;

#endif

	napi_schedule_irqoff(&q_vector->napi);

	return IRQ_HANDLED;
}

/**
 * i40evf_map_vector_to_rxq - associate irqs with rx queues
 * @adapter: board private structure
 * @v_idx: interrupt number
 * @r_idx: queue number
 **/
static void
i40evf_map_vector_to_rxq(struct i40evf_adapter *adapter, int v_idx, int r_idx)
{
	struct i40e_q_vector *q_vector = &adapter->q_vectors[v_idx];
	struct i40e_ring *rx_ring = &adapter->rx_rings[r_idx];
	struct i40e_hw *hw = &adapter->hw;

	rx_ring->q_vector = q_vector;
	rx_ring->next = q_vector->rx.ring;
	rx_ring->vsi = &adapter->vsi;
	q_vector->rx.ring = rx_ring;
	q_vector->rx.count++;
	q_vector->rx.next_update = jiffies + 1;
	q_vector->rx.target_itr = ITR_TO_REG(rx_ring->itr_setting);
	q_vector->ring_mask |= BIT(r_idx);
	wr32(hw, I40E_VFINT_ITRN1(I40E_RX_ITR, q_vector->reg_idx),
		 q_vector->rx.current_itr);
	q_vector->rx.current_itr = q_vector->rx.target_itr;
}

/**
 * i40evf_map_vector_to_txq - associate irqs with tx queues
 * @adapter: board private structure
 * @v_idx: interrupt number
 * @t_idx: queue number
 **/
static void
i40evf_map_vector_to_txq(struct i40evf_adapter *adapter, int v_idx, int t_idx)
{
	struct i40e_q_vector *q_vector = &adapter->q_vectors[v_idx];
	struct i40e_ring *tx_ring = &adapter->tx_rings[t_idx];
	struct i40e_hw *hw = &adapter->hw;

	tx_ring->q_vector = q_vector;
	tx_ring->next = q_vector->tx.ring;
	tx_ring->vsi = &adapter->vsi;
	q_vector->tx.ring = tx_ring;
	q_vector->tx.count++;
	q_vector->tx.next_update = jiffies + 1;
	q_vector->tx.target_itr = ITR_TO_REG(tx_ring->itr_setting);
	q_vector->num_ringpairs++;
	wr32(hw, I40E_VFINT_ITRN1(I40E_TX_ITR, q_vector->reg_idx),
		 q_vector->tx.target_itr);
	q_vector->tx.current_itr = q_vector->tx.target_itr;
}

/**
 * i40evf_map_rings_to_vectors - Maps descriptor rings to vectors
 * @adapter: board private structure to initialize
 *
 * This function maps descriptor rings to the queue-specific vectors
 * we were allotted through the MSI-X enabling code.  Ideally, we'd have
 * one vector per ring/queue, but on a constrained vector budget, we
 * group the rings as "efficiently" as possible.  You would add new
 * mapping configurations in here.
 **/
static void i40evf_map_rings_to_vectors(struct i40evf_adapter *adapter)
{
	int rings_remaining = adapter->num_active_queues;
	int ridx = 0, vidx = 0;
	int q_vectors;

	q_vectors = adapter->num_msix_vectors - NONQ_VECS;

	for (; ridx < rings_remaining; ridx++) {
		i40evf_map_vector_to_rxq(adapter, vidx, ridx);
		i40evf_map_vector_to_txq(adapter, vidx, ridx);

		/* In the case where we have more queues than vectors, continue
		 * round-robin on vectors until all queues are mapped.
		 */
		if (++vidx >= q_vectors)
			vidx = 0;
	}

	adapter->aq_required |= I40EVF_FLAG_AQ_MAP_VECTORS;
}

#ifdef HAVE_IRQ_AFFINITY_NOTIFY
/**
 * i40evf_irq_affinity_notify - Callback for affinity changes
 * @notify: context as to what irq was changed
 * @mask: the new affinity mask
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * so that we may register to receive changes to the irq affinity masks.
 **/
static void i40evf_irq_affinity_notify(struct irq_affinity_notify *notify,
									   const cpumask_t *mask)
{
	struct i40e_q_vector *q_vector =
		container_of(notify, struct i40e_q_vector, affinity_notify);

	cpumask_copy(&q_vector->affinity_mask, mask);
}

/**
 * i40evf_irq_affinity_release - Callback for affinity notifier release
 * @ref: internal core kernel usage
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * to inform the current notification subscriber that they will no longer
 * receive notifications.
 **/
static void i40evf_irq_affinity_release(struct kref *ref) {}
#endif /* HAVE_IRQ_AFFINITY_NOTIFY */

/**
 * i40evf_request_traffic_irqs - Initialize MSI-X interrupts
 * @adapter: board private structure
 *
 * Allocates MSI-X vectors for tx and rx handling, and requests
 * interrupts from the kernel.
 **/
static int
i40evf_request_traffic_irqs(struct i40evf_adapter *adapter, char *basename)
{
	unsigned int vector, q_vectors;
	unsigned int rx_int_idx = 0, tx_int_idx = 0;
	int irq_num, err;
	int cpu;

	i40evf_irq_disable(adapter);
	/* Decrement for Other and TCP Timer vectors */
	q_vectors = adapter->num_msix_vectors - NONQ_VECS;

	for (vector = 0; vector < q_vectors; vector++) {
		struct i40e_q_vector *q_vector = &adapter->q_vectors[vector];
		irq_num = adapter->msix_entries[vector + NONQ_VECS].vector;

		if (q_vector->tx.ring && q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name),
					 "i40evf-%s-TxRx-%d", basename, rx_int_idx++);
			tx_int_idx++;
		} else if (q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name),
					 "i40evf-%s-rx-%d", basename, rx_int_idx++);
		} else if (q_vector->tx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name),
					 "i40evf-%s-tx-%d", basename, tx_int_idx++);
		} else {
			/* skip this unused q_vector */
			continue;
		}

		err = request_irq(irq_num,
						  i40evf_msix_clean_rings,
						  0,
						  q_vector->name,
						  q_vector);

		if (err) {
			dev_info(&adapter->pdev->dev,
					 "Request_irq failed, error: %d\n", err);
			goto free_queue_irqs;
		}

#ifdef HAVE_IRQ_AFFINITY_NOTIFY
		/* register for affinity change notifications */
		q_vector->affinity_notify.notify = i40evf_irq_affinity_notify;
		q_vector->affinity_notify.release =
			i40evf_irq_affinity_release;
		irq_set_affinity_notifier(irq_num, &q_vector->affinity_notify);
#endif
#ifdef HAVE_IRQ_AFFINITY_HINT
		/* Spread the IRQ affinity hints across online CPUs. Note that
		 * get_cpu_mask returns a mask with a permanent lifetime so
		 * it's safe to use as a hint for irq_set_affinity_hint.
		 */
		cpu = cpumask_local_spread(q_vector->v_idx, -1);
		irq_set_affinity_hint(irq_num, get_cpu_mask(cpu));
#endif /* HAVE_IRQ_AFFINITY_HINT */
	}

	return 0;

free_queue_irqs:

	while (vector) {
		vector--;
		irq_num = adapter->msix_entries[vector + NONQ_VECS].vector;
#ifdef HAVE_IRQ_AFFINITY_NOTIFY
		irq_set_affinity_notifier(irq_num, NULL);
#endif
#ifdef HAVE_IRQ_AFFINITY_HINT
		irq_set_affinity_hint(irq_num, NULL);
#endif
		free_irq(irq_num, &adapter->q_vectors[vector]);
	}

	return err;
}

/**
 * i40evf_request_misc_irq - Initialize MSI-X interrupts
 * @adapter: board private structure
 *
 * Allocates MSI-X vector 0 and requests interrupts from the kernel. This
 * vector is only for the admin queue, and stays active even when the netdev
 * is closed.
 **/
static int i40evf_request_misc_irq(struct i40evf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	snprintf(adapter->misc_vector_name,
			 sizeof(adapter->misc_vector_name) - 1, "i40evf-%s:mbx",
			 dev_name(&adapter->pdev->dev));
	err = request_irq(adapter->msix_entries[0].vector,
					  &i40evf_msix_aq, 0,
					  adapter->misc_vector_name, netdev);

	if (err) {
		dev_err(&adapter->pdev->dev,
				"request_irq for %s failed: %d\n",
				adapter->misc_vector_name, err);
		free_irq(adapter->msix_entries[0].vector, netdev);
	}

	return err;
}

/**
 * i40evf_free_traffic_irqs - Free MSI-X interrupts
 * @adapter: board private structure
 *
 * Frees all MSI-X vectors other than 0.
 **/
static void i40evf_free_traffic_irqs(struct i40evf_adapter *adapter)
{
	int vector, irq_num, q_vectors;

	if (!adapter->msix_entries)
		return;

	q_vectors = adapter->num_msix_vectors - NONQ_VECS;

	for (vector = 0; vector < q_vectors; vector++) {
		irq_num = adapter->msix_entries[vector + NONQ_VECS].vector;
#ifdef HAVE_IRQ_AFFINITY_NOTIFY
		irq_set_affinity_notifier(irq_num, NULL);
#endif
#ifdef HAVE_IRQ_AFFINITY_HINT
		irq_set_affinity_hint(irq_num, NULL);
#endif
		free_irq(irq_num, &adapter->q_vectors[vector]);
	}
}

/**
 * i40evf_free_misc_irq - Free MSI-X miscellaneous vector
 * @adapter: board private structure
 *
 * Frees MSI-X vector 0.
 **/
static void i40evf_free_misc_irq(struct i40evf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (!adapter->msix_entries)
		return;

	free_irq(adapter->msix_entries[0].vector, netdev);
}

/**
 * i40evf_configure_tx - Configure Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void i40evf_configure_tx(struct i40evf_adapter *adapter)
{
	struct i40e_hw *hw = &adapter->hw;
	int i;

	for (i = 0; i < adapter->num_active_queues; i++)
		adapter->tx_rings[i].tail = hw->hw_addr + I40E_QTX_TAIL1(i);
}

#ifdef DEV_HPCAP
extern void i40e_alloc_rx_buffers_hpcap(struct i40e_ring *rx_ring, u16 cleaned_count);
#endif

/**
 * i40evf_configure_rx - Configure Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void i40evf_configure_rx(struct i40evf_adapter *adapter)
{
	unsigned int rx_buf_len = I40E_RXBUFFER_2048;
	struct i40e_hw *hw = &adapter->hw;
	int i;

#ifdef CONFIG_I40E_DISABLE_PACKET_SPLIT
	/* reset Rx buffer length for an Ethernet packet */
	rx_buf_len = I40E_RXBUFFER_1536 - NET_IP_ALIGN;

	if ((adapter->netdev->mtu + I40E_PACKET_HDR_PAD) > rx_buf_len)
		rx_buf_len = adapter->netdev->mtu + I40E_PACKET_HDR_PAD;

#else
	/* Legacy Rx will always default to a 2048 buffer size. */
#if (PAGE_SIZE < 8192)

	if (!(adapter->flags & I40EVF_FLAG_LEGACY_RX)) {
		struct net_device *netdev = adapter->netdev;

		/* For jumbo frames on systems with 4K pages we have to use
		 * an order 1 page, so we might as well increase the size
		 * of our Rx buffer to make better use of the available space
		 */
		rx_buf_len = I40E_RXBUFFER_3072;

		/* We use a 1536 buffer size for configurations with
		 * standard Ethernet mtu.  On x86 this gives us enough room
		 * for shared info and 192 bytes of padding.
		 */
		if (!I40E_2K_TOO_SMALL_WITH_PADDING &&
			(netdev->mtu <= ETH_DATA_LEN))
			rx_buf_len = I40E_RXBUFFER_1536 - NET_IP_ALIGN;
	}

#endif
#endif /* CONFIG_I40E_DISABLE_PACKET_SPLIT */

	for (i = 0; i < adapter->num_active_queues; i++) {
		adapter->rx_rings[i].tail = hw->hw_addr + I40E_QRX_TAIL1(i);
		adapter->rx_rings[i].rx_buf_len = rx_buf_len;

		if (adapter->flags & I40EVF_FLAG_LEGACY_RX)
			clear_ring_build_skb_enabled(&adapter->rx_rings[i]);
		else
			set_ring_build_skb_enabled(&adapter->rx_rings[i]);
	}
}

#ifdef HAVE_VLAN_RX_REGISTER
/**
 * i40evf_vlan_rx_register - Register for RX vlan filtering, enable VLAN
 * tag stripping
 * @adapter: board private structure
 **/
static void i40evf_vlan_rx_register(struct net_device *netdev,
									struct vlan_group *grp)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	/* Since VLAN tag stripping is always enabled, just store vlgrp. */
	adapter->vsi.vlgrp = grp;

}

#endif
/**
 * i40evf_find_vlan - Search filter list for specific vlan filter
 * @adapter: board private structure
 * @vlan: vlan tag
 *
 * Returns ptr to the filter object or NULL. Must be called while holding the
 * mac_vlan_list_lock.
 **/
static struct
i40evf_vlan_filter *i40evf_find_vlan(struct i40evf_adapter *adapter, u16 vlan)
{
	struct i40evf_vlan_filter *f;

	list_for_each_entry(f, &adapter->vlan_filter_list, list) {
		if (vlan == f->vlan)
			return f;
	}
	return NULL;
}

/**
 * i40evf_add_vlan - Add a vlan filter to the list
 * @adapter: board private structure
 * @vlan: VLAN tag
 *
 * Returns ptr to the filter object or NULL when no memory available.
 **/
static struct
i40evf_vlan_filter *i40evf_add_vlan(struct i40evf_adapter *adapter, u16 vlan)
{
	struct i40evf_vlan_filter *f = NULL;

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	f = i40evf_find_vlan(adapter, vlan);

	if (!f) {
		f = kzalloc(sizeof(*f), GFP_ATOMIC);

		if (!f)
			goto clearout;

		f->vlan = vlan;

		INIT_LIST_HEAD(&f->list);
		list_add(&f->list, &adapter->vlan_filter_list);
		f->add = true;
		adapter->aq_required |= I40EVF_FLAG_AQ_ADD_VLAN_FILTER;
	}

clearout:
	spin_unlock_bh(&adapter->mac_vlan_list_lock);
	return f;
}

/**
 * i40evf_del_vlan - Remove a vlan filter from the list
 * @adapter: board private structure
 * @vlan: VLAN tag
 **/
static void i40evf_del_vlan(struct i40evf_adapter *adapter, u16 vlan)
{
	struct i40evf_vlan_filter *f;

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	f = i40evf_find_vlan(adapter, vlan);

	if (f) {
		f->remove = true;
		adapter->aq_required |= I40EVF_FLAG_AQ_DEL_VLAN_FILTER;
	}

	spin_unlock_bh(&adapter->mac_vlan_list_lock);
}

/**
 * i40evf_vlan_rx_add_vid - Add a VLAN filter to a device
 * @netdev: network device struct
 * @vid: VLAN tag
 **/
#ifdef HAVE_INT_NDO_VLAN_RX_ADD_VID
#ifdef NETIF_F_HW_VLAN_CTAG_RX
static int i40evf_vlan_rx_add_vid(struct net_device *netdev,
								  __always_unused __be16 proto, u16 vid)
#else
static int i40evf_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
#endif
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	if (!VLAN_ALLOWED(adapter))
		return -EIO;

	if (i40evf_add_vlan(adapter, vid) == NULL)
		return -ENOMEM;

	return 0;
}
#else
static void i40evf_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	if (VLAN_ALLOWED(adapter))
		i40evf_add_vlan(adapter, vid);
}
#endif

/**
 * i40evf_vlan_rx_kill_vid - Remove a VLAN filter from a device
 * @netdev: network device struct
 * @vid: VLAN tag
 **/
#ifdef HAVE_INT_NDO_VLAN_RX_ADD_VID
#ifdef NETIF_F_HW_VLAN_CTAG_RX
static int i40evf_vlan_rx_kill_vid(struct net_device *netdev,
								   __always_unused __be16 proto, u16 vid)
#else
static int i40evf_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
#endif
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	if (VLAN_ALLOWED(adapter)) {
		i40evf_del_vlan(adapter, vid);
		return 0;
	}

	return -EIO;
}
#else
static void i40evf_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	if (VLAN_ALLOWED(adapter))
		i40evf_del_vlan(adapter, vid);
}
#endif

/**
 * i40evf_find_filter - Search filter list for specific mac filter
 * @adapter: board private structure
 * @macaddr: the MAC address
 *
 * Returns ptr to the filter object or NULL. Must be called while holding the
 * mac_vlan_list_lock.
 **/
static struct
i40evf_mac_filter *i40evf_find_filter(struct i40evf_adapter *adapter,
									  u8 *macaddr)
{
	struct i40evf_mac_filter *f;

	if (!macaddr)
		return NULL;

	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		if (ether_addr_equal(macaddr, f->macaddr))
			return f;
	}
	return NULL;
}

/**
 * i40e_add_filter - Add a mac filter to the filter list
 * @adapter: board private structure
 * @macaddr: the MAC address
 *
 * Returns ptr to the filter object or NULL when no memory available.
 **/
static struct
i40evf_mac_filter *i40evf_add_filter(struct i40evf_adapter *adapter,
									 u8 *macaddr)
{
	struct i40evf_mac_filter *f;

	if (!macaddr)
		return NULL;

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	f = i40evf_find_filter(adapter, macaddr);

	if (!f) {
		f = kzalloc(sizeof(*f), GFP_ATOMIC);

		if (!f)
			goto clearout;

		ether_addr_copy(f->macaddr, macaddr);

		list_add_tail(&f->list, &adapter->mac_filter_list);
		f->add = true;
		adapter->aq_required |= I40EVF_FLAG_AQ_ADD_MAC_FILTER;
	} else
		f->remove = false;

clearout:
	spin_unlock_bh(&adapter->mac_vlan_list_lock);
	return f;
}

/**
 * i40evf_set_mac - NDO callback to set port mac address
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int i40evf_set_mac(struct net_device *netdev, void *p)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40e_hw *hw = &adapter->hw;
	struct i40evf_mac_filter *f;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, addr->sa_data))
		return 0;

	if (adapter->flags & I40EVF_FLAG_ADDR_SET_BY_PF)
		return -EPERM;

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	f = i40evf_find_filter(adapter, hw->mac.addr);

	if (f) {
		f->remove = true;
		adapter->aq_required |= I40EVF_FLAG_AQ_DEL_MAC_FILTER;
	}

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	f = i40evf_add_filter(adapter, addr->sa_data);

	if (f) {
		ether_addr_copy(hw->mac.addr, addr->sa_data);
		ether_addr_copy(netdev->dev_addr, adapter->hw.mac.addr);
	}

	return (f == NULL) ? -ENOMEM : 0;
}

/**
 * i40evf_set_rx_mode - NDO callback to set the netdev filters
 * @netdev: network interface device structure
 **/
static void i40evf_set_rx_mode(struct net_device *netdev)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40evf_mac_filter *f, *ftmp;
	struct netdev_hw_addr *uca;
#ifdef NETDEV_HW_ADDR_T_MULTICAST
	struct netdev_hw_addr *mca;
#else
	struct dev_mc_list *mca;
#endif /* NETDEV_HW_ADDR_T_MULTICAST */
	struct netdev_hw_addr *ha;

	/* add addr if not already in the filter list */
	netdev_for_each_uc_addr(uca, netdev) {
		i40evf_add_filter(adapter, uca->addr);
	}
	netdev_for_each_mc_addr(mca, netdev) {
#ifdef NETDEV_HW_ADDR_T_MULTICAST
		i40evf_add_filter(adapter, mca->addr);
#else
		i40evf_add_filter(adapter, mca->da_addr);
#endif /* NETDEV_HW_ADDR_T_MULTICAST */
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {

		netdev_for_each_mc_addr(mca, netdev)
#ifdef NETDEV_HW_ADDR_T_MULTICAST

		if (ether_addr_equal(mca->addr, f->macaddr))
#else
		if (ether_addr_equal(mca->da_addr, f->macaddr))
#endif
			goto bottom_of_search_loop;

		netdev_for_each_uc_addr(uca, netdev)

		if (ether_addr_equal(uca->addr, f->macaddr))
			goto bottom_of_search_loop;

		for_each_dev_addr(netdev, ha)

		if (ether_addr_equal(ha->addr, f->macaddr))
			goto bottom_of_search_loop;

		if (ether_addr_equal(f->macaddr, adapter->hw.mac.addr))
			goto bottom_of_search_loop;

		/* f->macaddr wasn't found in uc, mc, or ha list so delete it */
		f->remove = true;
		adapter->aq_required |= I40EVF_FLAG_AQ_DEL_MAC_FILTER;

bottom_of_search_loop:
		continue;
	}

	if (netdev->flags & IFF_PROMISC &&
		!(adapter->flags & I40EVF_FLAG_PROMISC_ON))
		adapter->aq_required |= I40EVF_FLAG_AQ_REQUEST_PROMISC;
	else if (!(netdev->flags & IFF_PROMISC) &&
			 adapter->flags & I40EVF_FLAG_PROMISC_ON)
		adapter->aq_required |= I40EVF_FLAG_AQ_RELEASE_PROMISC;

	if (netdev->flags & IFF_ALLMULTI &&
		!(adapter->flags & I40EVF_FLAG_ALLMULTI_ON))
		adapter->aq_required |= I40EVF_FLAG_AQ_REQUEST_ALLMULTI;
	else if (!(netdev->flags & IFF_ALLMULTI) &&
			 adapter->flags & I40EVF_FLAG_ALLMULTI_ON)
		adapter->aq_required |= I40EVF_FLAG_AQ_RELEASE_ALLMULTI;

	spin_unlock_bh(&adapter->mac_vlan_list_lock);
}

/**
 * i40evf_napi_enable_all - enable NAPI on all queue vectors
 * @adapter: board private structure
 **/
static void i40evf_napi_enable_all(struct i40evf_adapter *adapter)
{
	int q_idx;
	struct i40e_q_vector *q_vector;
	int q_vectors = adapter->num_msix_vectors - NONQ_VECS;

	for (q_idx = 0; q_idx < q_vectors; q_idx++) {
		struct napi_struct *napi;

		q_vector = &adapter->q_vectors[q_idx];
		napi = &q_vector->napi;

#ifdef DEV_HPCAP

		if (!is_hpcap_adapter(adapter))
#endif
			napi_enable(napi);
	}
}

/**
 * i40evf_napi_disable_all - disable NAPI on all queue vectors
 * @adapter: board private structure
 **/
static void i40evf_napi_disable_all(struct i40evf_adapter *adapter)
{
	int q_vectors = adapter->num_msix_vectors - NONQ_VECS;
	struct i40e_q_vector *q_vector;
	int q_idx;

	for (q_idx = 0; q_idx < q_vectors; q_idx++) {
		q_vector = &adapter->q_vectors[q_idx];
		napi_disable(&q_vector->napi);
	}
}

/**
 * i40evf_configure - set up transmit and receive data structures
 * @adapter: board private structure
 **/
static void i40evf_configure(struct i40evf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	i40evf_set_rx_mode(netdev);

	i40evf_configure_tx(adapter);
	i40evf_configure_rx(adapter);
	adapter->aq_required |= I40EVF_FLAG_AQ_CONFIGURE_QUEUES;

	for (i = 0; i < adapter->num_active_queues; i++) {
		struct i40e_ring *ring = &adapter->rx_rings[i];

#ifdef DEV_HPCAP
		ring->adapter = adapter;

		if (is_hpcap_adapter(adapter))
			i40e_alloc_rx_buffers_hpcap(ring, I40E_DESC_UNUSED(ring));
		else
#endif
			i40e_alloc_rx_buffers(ring, I40E_DESC_UNUSED(ring));
	}
}

/**
 * i40evf_up_complete - Finish the last steps of bringing up a connection
 * @adapter: board private structure
 *
 * Expects to be called while holding the __I40EVF_IN_CRITICAL_TASK bit lock.
 **/
static void i40evf_up_complete(struct i40evf_adapter *adapter)
{
#ifdef CONFIG_NETDEVICES_MULTIQUEUE

	if (adapter->num_active_queues > 1)
		netdev->features |= NETIF_F_MULTI_QUEUE;

#endif
	adapter->state = __I40EVF_RUNNING;
	clear_bit(__I40E_VSI_DOWN, adapter->vsi.state);

	i40evf_napi_enable_all(adapter);

	adapter->aq_required |= I40EVF_FLAG_AQ_ENABLE_QUEUES;

	if (CLIENT_ENABLED(adapter))
		adapter->flags |= I40EVF_FLAG_CLIENT_NEEDS_OPEN;

	mod_timer_pending(&adapter->watchdog_timer, jiffies + 1);

#ifdef DEV_HPCAP

	if (is_hpcap_adapter(adapter))
		hpcap_launch_poll_threads(adapter);

#endif /* DEV_HPCAP */

}

/**
 * i40e_down - Shutdown the connection processing
 * @adapter: board private structure
 *
 * Expects to be called while holding the __I40EVF_IN_CRITICAL_TASK bit lock.
 **/
void i40evf_down(struct i40evf_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct i40evf_mac_filter *f;

	if (adapter->state <= __I40EVF_DOWN_PENDING)
		return;


#ifdef DEV_HPCAP

	if (is_hpcap_adapter(adapter))
		hpcap_stop_poll_threads(adapter);

#endif /* DEV_HPCAP */

	adapter_dbg(DBG_NET, "Preparing to down interface");

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);
	adapter->link_up = false;

	adapter_dbg(DBG_NET, "netif disabled");

#ifdef DEV_HPCAP

	if (!is_hpcap_adapter(adapter))
#endif
		i40evf_napi_disable_all(adapter);

	adapter_dbg(DBG_NET, "Disabling IRQ...");
	i40evf_irq_disable(adapter);

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	/* remove all MAC filters */
	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		f->remove = true;
	}
	/* remove all VLAN filters */
	list_for_each_entry(f, &adapter->vlan_filter_list, list) {
		f->remove = true;
	}

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	if (!(adapter->flags & I40EVF_FLAG_PF_COMMS_FAILED) &&
		adapter->state != __I40EVF_RESETTING) {
		/* cancel any current operation */
		adapter->current_op = VIRTCHNL_OP_UNKNOWN;
		/* Schedule operations to close down the HW. Don't wait
		 * here for this to complete. The watchdog is still running
		 * and it will take care of this.
		 */
		adapter->aq_required |= I40EVF_FLAG_AQ_DEL_MAC_FILTER;
		adapter->aq_required |= I40EVF_FLAG_AQ_DEL_VLAN_FILTER;
		adapter->aq_required |= I40EVF_FLAG_AQ_DISABLE_QUEUES;
	}

	mod_timer_pending(&adapter->watchdog_timer, jiffies + 1);
}

/**
 * i40evf_acquire_msix_vectors - Setup the MSIX capability
 * @adapter: board private structure
 * @vectors: number of vectors to request
 *
 * Work with the OS to set up the MSIX vectors needed.
 *
 * Returns 0 on success, negative on failure
 **/
static int
i40evf_acquire_msix_vectors(struct i40evf_adapter *adapter, int vectors)
{
	int v_actual;

	/* We'll want at least 3 (vector_threshold):
	 * 0) Other (Admin Queue and link, mostly)
	 * 1) TxQ[0] Cleanup
	 * 2) RxQ[0] Cleanup
	 *
	 * The more we get, the more we will assign to Tx/Rx Cleanup
	 * for the separate queues...where Rx Cleanup >= Tx Cleanup.
	 * Right now, we simply care about how many we'll get; we'll
	 * set them up later while requesting irq's.
	 */
	v_actual = pci_enable_msix_range(adapter->pdev, adapter->msix_entries,
									 MIN_MSIX_COUNT, vectors);

	if (v_actual < 0) {
		dev_err(&adapter->pdev->dev, "Unable to allocate MSI-X interrupts: %d\n",
				v_actual);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
		return v_actual;
	}

	if (CLIENT_ALLOWED(adapter)) {
		adapter->num_iwarp_msix = (v_actual - 1) / 2;
		adapter->num_msix_vectors = v_actual - adapter->num_iwarp_msix;
		adapter->iwarp_base_vector = adapter->num_msix_vectors;
	} else
		adapter->num_msix_vectors = v_actual;

	return 0;
}

/**
 * i40evf_free_queues - Free memory for all rings
 * @adapter: board private structure to initialize
 *
 * Free all of the memory associated with queue pairs.
 **/
static void i40evf_free_queues(struct i40evf_adapter *adapter)
{
	if (!adapter->vsi_res)
		return;

	adapter->num_active_queues = 0;
	kfree(adapter->tx_rings);
	adapter->tx_rings = NULL;
	kfree(adapter->rx_rings);
	adapter->rx_rings = NULL;

#ifdef DEV_HPCAP
	kfree(adapter->rx_ring);
	adapter->rx_ring = NULL;
#endif
}

/**
 * i40evf_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.  The polling_netdev array is
 * intended for Multiqueue, but should work fine with a single queue.
 **/
static int i40evf_alloc_queues(struct i40evf_adapter *adapter)
{
	int i, num_active_queues;

	/* If we're in reset reallocating queues we don't actually know yet for
	 * certain the PF gave us the number of queues we asked for but we'll
	 * assume it did.  Once basic reset is finished we'll confirm once we
	 * start negotiating config with PF.
	 */
	if (adapter->num_req_queues)
		num_active_queues = adapter->num_req_queues;
	else
		num_active_queues = min_t(int,
								  adapter->vsi_res->num_queue_pairs,
								  (int)(num_online_cpus()));

	adapter->tx_rings = kcalloc(num_active_queues,
								sizeof(struct i40e_ring), GFP_KERNEL);

	if (!adapter->tx_rings)
		goto err_out;

	adapter->rx_rings = kcalloc(num_active_queues,
								sizeof(struct i40e_ring), GFP_KERNEL);

#ifdef DEV_HPCAP
	/* This is a dirty hack to have the same name and type for the ring type structure */
	adapter->rx_ring = kcalloc(num_active_queues, sizeof(struct i40e_ring*), GFP_KERNEL);

	for (i = 0; i < num_active_queues; i++)
		adapter->rx_ring[i] = adapter->rx_rings + i;

#endif

	if (!adapter->rx_rings)
		goto err_out;

	for (i = 0; i < num_active_queues; i++) {
		struct i40e_ring *tx_ring;
		struct i40e_ring *rx_ring;

		tx_ring = &adapter->tx_rings[i];

		tx_ring->queue_index = i;
		tx_ring->netdev = adapter->netdev;
		tx_ring->dev = pci_dev_to_dev(adapter->pdev);
		tx_ring->count = adapter->tx_desc_count;
		tx_ring->itr_setting = I40E_ITR_TX_DEF;

		if (adapter->flags & I40EVF_FLAG_WB_ON_ITR_CAPABLE)
			tx_ring->flags |= I40E_TXR_FLAGS_WB_ON_ITR;

		rx_ring = &adapter->rx_rings[i];
		rx_ring->queue_index = i;
		rx_ring->netdev = adapter->netdev;
		rx_ring->dev = pci_dev_to_dev(adapter->pdev);
		rx_ring->count = adapter->rx_desc_count;
		rx_ring->itr_setting = I40E_ITR_RX_DEF;
	}

	adapter->num_active_queues = num_active_queues;

	return 0;

err_out:
	i40evf_free_queues(adapter);
	return -ENOMEM;
}

/**
 * i40evf_set_interrupt_capability - set MSI-X or FAIL if not supported
 * @adapter: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static int i40evf_set_interrupt_capability(struct i40evf_adapter *adapter)
{
	int vector, v_budget;
	int pairs = 0;
	int err = 0;

	if (!adapter->vsi_res) {
		err = -EIO;
		goto out;
	}

	pairs = adapter->num_active_queues;

	/* It's easy to be greedy for MSI-X vectors, but it really doesn't do
	 * us much good if we have more vectors than CPUs. However, we already
	 * limit the total number of queues by the number of CPUs so we do not
	 * need any further limiting here.
	 */
	v_budget = min_t(int, pairs + NONQ_VECS,
					 (int)adapter->vf_res->max_vectors);

	adapter->msix_entries = kcalloc(v_budget,
									sizeof(struct msix_entry), GFP_KERNEL);

	if (!adapter->msix_entries) {
		err = -ENOMEM;
		goto out;
	}

	for (vector = 0; vector < v_budget; vector++)
		adapter->msix_entries[vector].entry = vector;

	err = i40evf_acquire_msix_vectors(adapter, v_budget);
out:
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	/* Notify the stack of the (possibly) reduced Tx Queue count. */
	adapter->netdev->egress_subqueue_count = pairs;
#else /* CONFIG_NETDEVICES_MULTIQUEUE */
	netif_set_real_num_rx_queues(adapter->netdev, pairs);
	netif_set_real_num_tx_queues(adapter->netdev, pairs);
#endif /* CONFIG_NETDEVICES_MULTIQUEUE */
	return err;
}

/**
 * i40evf_alloc_q_vectors - Allocate memory for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int i40evf_alloc_q_vectors(struct i40evf_adapter *adapter)
{
	int q_idx = 0, num_q_vectors;
	struct i40e_q_vector *q_vector;

	num_q_vectors = adapter->num_msix_vectors - NONQ_VECS;
	adapter->q_vectors = kcalloc(num_q_vectors, sizeof(*q_vector),
								 GFP_KERNEL);

	if (!adapter->q_vectors)
		return -ENOMEM;

	for (q_idx = 0; q_idx < num_q_vectors; q_idx++) {
		q_vector = &adapter->q_vectors[q_idx];
		q_vector->adapter = adapter;
		q_vector->vsi = &adapter->vsi;
		q_vector->v_idx = q_idx;
		q_vector->reg_idx = q_idx;
#ifdef HAVE_IRQ_AFFINITY_NOTIFY
		cpumask_copy(&q_vector->affinity_mask, cpu_possible_mask);
#endif

#ifdef DEV_HPCAP

		if (!is_hpcap_adapter(adapter))
#endif

			netif_napi_add(adapter->netdev, &q_vector->napi,
						   i40e_napi_poll, NAPI_POLL_WEIGHT);
	}

	return 0;
}

/**
 * i40evf_free_q_vectors - Free memory allocated for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void i40evf_free_q_vectors(struct i40evf_adapter * adapter)
{
	int q_idx, num_q_vectors;
	int napi_vectors;

	if (!adapter->q_vectors)
		return;

	num_q_vectors = adapter->num_msix_vectors - NONQ_VECS;
	napi_vectors = adapter->num_active_queues;

	for (q_idx = 0; q_idx < num_q_vectors; q_idx++) {
		struct i40e_q_vector *q_vector = &adapter->q_vectors[q_idx];

#ifdef DEV_HPCAP

		if (q_idx < napi_vectors && !is_hpcap_adapter(adapter)) {
#else

		if (q_idx < napi_vectors) {
#endif

			netif_napi_del(&q_vector->napi);
		}
	}

	kfree(adapter->q_vectors);
	adapter->q_vectors = NULL;
}

/**
 * i40evf_reset_interrupt_capability - Reset MSIX setup
 * @adapter: board private structure
 *
 **/
void i40evf_reset_interrupt_capability(struct i40evf_adapter * adapter)
{
	if (!adapter->msix_entries)
		return;

	pci_disable_msix(adapter->pdev);
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
}

/**
 * i40evf_init_interrupt_scheme - Determine if MSIX is supported and init
 * @adapter: board private structure to initialize
 *
 **/
int i40evf_init_interrupt_scheme(struct i40evf_adapter * adapter)
{
	int err;

	err = i40evf_alloc_queues(adapter);

	if (err) {
		dev_err(&adapter->pdev->dev,
				"Unable to allocate memory for queues\n");
		goto err_alloc_queues;
	}

	rtnl_lock();
	err = i40evf_set_interrupt_capability(adapter);
	rtnl_unlock();

	if (err) {
		dev_err(&adapter->pdev->dev,
				"Unable to setup interrupt capabilities\n");
		goto err_set_interrupt;
	}

	err = i40evf_alloc_q_vectors(adapter);

	if (err) {
		dev_err(&adapter->pdev->dev,
				"Unable to allocate memory for queue vectors\n");
		goto err_alloc_q_vectors;
	}

	dev_info(&adapter->pdev->dev, "Multiqueue %s: Queue pair count = %u",
			 (adapter->num_active_queues > 1) ? "Enabled" : "Disabled",
			 adapter->num_active_queues);

	return 0;
err_alloc_q_vectors:
	i40evf_reset_interrupt_capability(adapter);
err_set_interrupt:
	i40evf_free_queues(adapter);
err_alloc_queues:
	return err;
}

/**
 * i40evf_config_rss_aq - Configure RSS keys and lut by using AQ commands
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/
static int i40evf_config_rss_aq(struct i40evf_adapter * adapter)
{
	struct i40e_aqc_get_set_rss_key_data *rss_key =
		(struct i40e_aqc_get_set_rss_key_data *)adapter->rss_key;
	struct i40e_hw *hw = &adapter->hw;
	int ret = 0;

	if (adapter->current_op != VIRTCHNL_OP_UNKNOWN) {
		/* bail because we already have a command pending */
		dev_err(&adapter->pdev->dev,
				"Cannot configure RSS, command %d pending\n",
				adapter->current_op);
		return -EBUSY;
	}

	ret = i40e_aq_set_rss_key(hw, adapter->vsi.id, rss_key);

	if (ret) {
		dev_err(&adapter->pdev->dev, "Cannot set RSS key, err %s aq_err %s\n",
				i40e_stat_str(hw, ret),
				i40e_aq_str(hw, hw->aq.asq_last_status));
		return ret;
	}


	ret = i40e_aq_set_rss_lut(hw, adapter->vsi.id, false,
							  adapter->rss_lut, adapter->rss_lut_size);

	if (ret) {
		dev_err(&adapter->pdev->dev, "Cannot set RSS lut, err %s aq_err %s\n",
				i40e_stat_str(hw, ret),
				i40e_aq_str(hw, hw->aq.asq_last_status));
	}

	return ret;
}

/**
 * i40evf_config_rss_reg - Configure RSS keys and lut by writing registers
 * @adapter: board private structure
 *
 * Returns 0 on success, negative on failure
 **/
static int i40evf_config_rss_reg(struct i40evf_adapter * adapter)
{
	struct i40e_hw *hw = &adapter->hw;
	u32 *dw;
	u16 i;

	dw = (u32 *)adapter->rss_key;

	for (i = 0; i <= adapter->rss_key_size / 4; i++)
		wr32(hw, I40E_VFQF_HKEY(i), dw[i]);

	dw = (u32 *)adapter->rss_lut;

	for (i = 0; i <= adapter->rss_lut_size / 4; i++)
		wr32(hw, I40E_VFQF_HLUT(i), dw[i]);

	i40e_flush(hw);

	return 0;
}

/**
 * i40evf_config_rss - Configure RSS keys and lut
 * @adapter: board private structure
 *
 * Returns 0 on success, negative on failure
 **/
int i40evf_config_rss(struct i40evf_adapter * adapter)
{

	if (RSS_PF(adapter)) {
		adapter->aq_required |= I40EVF_FLAG_AQ_SET_RSS_LUT |
								I40EVF_FLAG_AQ_SET_RSS_KEY;
		return 0;
	} else if (RSS_AQ(adapter))
		return i40evf_config_rss_aq(adapter);
	else
		return i40evf_config_rss_reg(adapter);
}


/**
 * i40evf_fill_rss_lut - Fill the lut with default values
 * @adapter: board private structure
 **/
static void i40evf_fill_rss_lut(struct i40evf_adapter * adapter)
{
	u16 i;

	for (i = 0; i < adapter->rss_lut_size; i++)
		adapter->rss_lut[i] = i % adapter->num_active_queues;
}

/**
 * i40evf_init_rss - Prepare for RSS
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/
static int i40evf_init_rss(struct i40evf_adapter * adapter)
{
	struct i40e_hw *hw = &adapter->hw;
	int ret;

	if (!RSS_PF(adapter)) {
		/* Enable PCTYPES for RSS, TCP/UDP with IPv4/IPv6 */
		if (adapter->vf_res->vf_cap_flags &
			VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2)
			adapter->hena = I40E_DEFAULT_RSS_HENA_EXPANDED;
		else
			adapter->hena = I40E_DEFAULT_RSS_HENA;

		wr32(hw, I40E_VFQF_HENA(0), (u32)adapter->hena);
		wr32(hw, I40E_VFQF_HENA(1), (u32)(adapter->hena >> 32));
	}

	i40evf_fill_rss_lut(adapter);

	netdev_rss_key_fill((void *)adapter->rss_key, adapter->rss_key_size);
	ret = i40evf_config_rss(adapter);

	return ret;
}

/**
 * i40evf_free_rss - Free memory used by RSS structs
 * @adapter: board private structure
 **/
static void i40evf_free_rss(struct i40evf_adapter * adapter)
{
	kfree(adapter->rss_key);
	adapter->rss_key = NULL;

	kfree(adapter->rss_lut);
	adapter->rss_lut = NULL;
}

/**
 * i40evf_reinit_interrupt_scheme - Reallocate queues and vectors
 * @adapter: board private structure
 *
 * Returns 0 on success, negative on failure
 **/
static int i40evf_reinit_interrupt_scheme(struct i40evf_adapter * adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	if (netif_running(netdev))
		i40evf_free_traffic_irqs(adapter);

	i40evf_free_misc_irq(adapter);
	i40evf_reset_interrupt_capability(adapter);
	i40evf_free_q_vectors(adapter);
	i40evf_free_queues(adapter);

	err =  i40evf_init_interrupt_scheme(adapter);

	if (err)
		goto err;

	netif_tx_stop_all_queues(netdev);

	err = i40evf_request_misc_irq(adapter);

	if (err)
		goto err;

	set_bit(__I40E_VSI_DOWN, adapter->vsi.state);

	i40evf_map_rings_to_vectors(adapter);

	if (RSS_AQ(adapter))
		adapter->aq_required |= I40EVF_FLAG_AQ_CONFIGURE_RSS;
	else
		err = i40evf_init_rss(adapter);

err:
	return err;
}

/**
 * i40evf_watchdog_timer - Periodic call-back timer
 * @data: pointer to adapter disguised as unsigned long
 **/
static void i40evf_watchdog_timer(unsigned long data)
{
	struct i40evf_adapter *adapter = (struct i40evf_adapter *)data;

	schedule_work(&adapter->watchdog_task);
	/* timer will be rescheduled in watchdog task */
}

/**
 * i40evf_watchdog_task - Periodic call-back task
 * @work: pointer to work_struct
 **/
static void i40evf_watchdog_task(struct work_struct * work)
{
	struct i40evf_adapter *adapter = container_of(work,
									 struct i40evf_adapter,
									 watchdog_task);
	struct i40e_hw *hw = &adapter->hw;
	u32 reg_val;

	if (test_and_set_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section))
		goto restart_watchdog;

	if (adapter->flags & I40EVF_FLAG_PF_COMMS_FAILED) {
		reg_val = rd32(hw, I40E_VFGEN_RSTAT) &
				  I40E_VFGEN_RSTAT_VFR_STATE_MASK;

		if ((reg_val == VIRTCHNL_VFR_VFACTIVE) ||
			(reg_val == VIRTCHNL_VFR_COMPLETED)) {
			/* A chance for redemption! */
			dev_err(&adapter->pdev->dev, "Hardware came out of reset. Attempting reinit.\n");
			adapter->state = __I40EVF_STARTUP;
			adapter->flags &= ~I40EVF_FLAG_PF_COMMS_FAILED;
			schedule_delayed_work(&adapter->init_task, 10);
			clear_bit(__I40EVF_IN_CRITICAL_TASK,
					  &adapter->crit_section);
			/* Don't reschedule the watchdog, since we've restarted
			 * the init task. When init_task contacts the PF and
			 * gets everything set up again, it'll restart the
			 * watchdog for us. Down, boy. Sit. Stay. Woof.
			 */
			return;
		}

		adapter->aq_required = 0;
		adapter->current_op = VIRTCHNL_OP_UNKNOWN;
		goto watchdog_done;
	}

	if ((adapter->state < __I40EVF_DOWN) ||
		(adapter->flags & I40EVF_FLAG_RESET_PENDING))
		goto watchdog_done;

	/* check for reset */
	reg_val = rd32(hw, I40E_VF_ARQLEN1) & I40E_VF_ARQLEN1_ARQENABLE_MASK;

	if (!(adapter->flags & I40EVF_FLAG_RESET_PENDING) && !reg_val) {
		adapter->state = __I40EVF_RESETTING;
		adapter->flags |= I40EVF_FLAG_RESET_PENDING;
		dev_err(&adapter->pdev->dev, "Hardware reset detected\n");
		schedule_work(&adapter->reset_task);
		adapter->aq_required = 0;
		adapter->current_op = VIRTCHNL_OP_UNKNOWN;
		goto watchdog_done;
	}

	/* Process admin queue tasks. After init, everything gets done
	 * here so we don't race on the admin queue.
	 */
	if (adapter->current_op) {
		if (!i40e_asq_done(hw)) {
			dev_dbg(&adapter->pdev->dev, "Admin queue timeout\n");
			i40evf_send_api_ver(adapter);
		}

		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_GET_CONFIG) {
		i40evf_send_vf_config_msg(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_DISABLE_QUEUES) {
		i40evf_disable_queues(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_MAP_VECTORS) {
		i40evf_map_queues(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_ADD_MAC_FILTER) {
		i40evf_add_ether_addrs(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_ADD_VLAN_FILTER) {
		i40evf_add_vlans(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_DEL_MAC_FILTER) {
		i40evf_del_ether_addrs(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_DEL_VLAN_FILTER) {
		i40evf_del_vlans(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_ENABLE_VLAN_STRIPPING) {
		i40evf_enable_vlan_stripping(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_DISABLE_VLAN_STRIPPING) {
		i40evf_disable_vlan_stripping(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_CONFIGURE_QUEUES) {
		i40evf_configure_queues(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_ENABLE_QUEUES) {
		i40evf_enable_queues(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_CONFIGURE_RSS) {
		/* This message goes straight to the firmware, not the
		 * PF, so we don't have to set current_op as we will
		 * not get a response through the ARQ.
		 */
		i40evf_init_rss(adapter);
		adapter->aq_required &= ~I40EVF_FLAG_AQ_CONFIGURE_RSS;
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_GET_HENA) {
		i40evf_get_hena(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_SET_HENA) {
		i40evf_set_hena(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_SET_RSS_KEY) {
		i40evf_set_rss_key(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_SET_RSS_LUT) {
		i40evf_set_rss_lut(adapter);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_REQUEST_PROMISC) {
		i40evf_set_promiscuous(adapter, FLAG_VF_UNICAST_PROMISC |
							   FLAG_VF_MULTICAST_PROMISC);
		goto watchdog_done;
	}

	if (adapter->aq_required & I40EVF_FLAG_AQ_REQUEST_ALLMULTI) {
		i40evf_set_promiscuous(adapter, FLAG_VF_MULTICAST_PROMISC);
		goto watchdog_done;
	}

	if ((adapter->aq_required & I40EVF_FLAG_AQ_RELEASE_PROMISC) &&
		(adapter->aq_required & I40EVF_FLAG_AQ_RELEASE_ALLMULTI)) {
		i40evf_set_promiscuous(adapter, 0);
		goto watchdog_done;
	}

	schedule_delayed_work(&adapter->client_task, msecs_to_jiffies(5));

	if (adapter->state == __I40EVF_RUNNING)
		i40evf_request_stats(adapter);

watchdog_done:

	if (adapter->state == __I40EVF_RUNNING)
		i40e_detect_recover_hung(&adapter->vsi);

	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);
restart_watchdog:

	if (adapter->state == __I40EVF_REMOVE)
		return;

	if (adapter->aq_required)
		mod_timer(&adapter->watchdog_timer,
				  jiffies + msecs_to_jiffies(20));
	else
		mod_timer(&adapter->watchdog_timer, jiffies + (HZ * 2));

	schedule_work(&adapter->adminq_task);
}

/**
 * i40evf_disable_vf - disable a VF that failed to reset
 * @adapter: private adapter structure
 *
 * Helper function to shut down the VF when a reset never finishes.
 **/
static void i40evf_disable_vf(struct i40evf_adapter * adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct i40evf_mac_filter *f, *ftmp;
	struct i40evf_vlan_filter *fv, *fvtmp;
	/* reset never finished */

	adapter->flags |= I40EVF_FLAG_PF_COMMS_FAILED;

	/* We don't use netif_running() because it may be true prior to
	 * ndo_open() returning, so we can't assume it means all our open
	 * tasks have finished, since we're not holding the rtnl_lock here.
	 */
	if (adapter->state == __I40EVF_RUNNING) {
		set_bit(__I40E_VSI_DOWN, adapter->vsi.state);
		netif_carrier_off(netdev);
		netif_tx_disable(netdev);
		adapter->link_up = false;
		i40evf_napi_disable_all(adapter);
		i40evf_irq_disable(adapter);
		i40evf_free_traffic_irqs(adapter);
		i40evf_free_all_tx_resources(adapter);
		i40evf_free_all_rx_resources(adapter);
	}

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	/* Delete all of the filters, both MAC and VLAN. */
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list,
							 list) {
		list_del(&f->list);
		kfree(f);
	}

	list_for_each_entry_safe(fv, fvtmp, &adapter->vlan_filter_list,
							 list) {
		list_del(&fv->list);
		kfree(fv);
	}

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	i40evf_free_misc_irq(adapter);
	i40evf_reset_interrupt_capability(adapter);
	i40evf_free_queues(adapter);
	i40evf_free_q_vectors(adapter);
	kfree(adapter->vf_res);
	adapter->vf_res = NULL;
	i40e_shutdown_adminq(&adapter->hw);
	adapter->netdev->flags &= ~IFF_UP;
	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);
	clear_bit(__I40EVF_IN_CLIENT_TASK, &adapter->crit_section);
	adapter->flags &= ~I40EVF_FLAG_RESET_PENDING;
	adapter->state = __I40EVF_DOWN;
	wake_up(&adapter->down_waitqueue);
	dev_info(&adapter->pdev->dev, "Reset task did not complete, VF disabled\n");
}

#define I40EVF_RESET_WAIT_MS 10
#define I40EVF_RESET_WAIT_COUNT 500
/**
 * i40evf_reset_task - Call-back task to handle hardware reset
 * @work: pointer to work_struct
 *
 * During reset we need to shut down and reinitialize the admin queue
 * before we can use it to communicate with the PF again. We also clear
 * and reinit the rings because that context is lost as well.
 **/
static void i40evf_reset_task(struct work_struct * work)
{
	struct i40evf_adapter *adapter = container_of(work,
									 struct i40evf_adapter,
									 reset_task);
	struct net_device *netdev = adapter->netdev;
	struct i40e_hw *hw = &adapter->hw;
	struct i40evf_vlan_filter *vlf;
	struct i40evf_mac_filter *f;
	u32 reg_val;
	int i = 0, err;
	bool running;

	/* When device is being removed it doesn't make sense to run the reset
	 * task, just return in such a case.
	 */
	if (test_bit(__I40EVF_IN_REMOVE_TASK, &adapter->crit_section))
		return;

	while (test_and_set_bit(__I40EVF_IN_CRITICAL_TASK,
							&adapter->crit_section))
		usleep_range(500, 1000);

	while (test_and_set_bit(__I40EVF_IN_CLIENT_TASK,
							&adapter->crit_section))
		usleep_range(500, 1000);

	if (CLIENT_ENABLED(adapter)) {
		adapter->flags &= ~(I40EVF_FLAG_CLIENT_NEEDS_OPEN |
							I40EVF_FLAG_CLIENT_NEEDS_CLOSE |
							I40EVF_FLAG_CLIENT_NEEDS_L2_PARAMS |
							I40EVF_FLAG_SERVICE_CLIENT_REQUESTED);
		cancel_delayed_work_sync(&adapter->client_task);
		i40evf_notify_client_close(&adapter->vsi, true);
	}

	i40evf_misc_irq_disable(adapter);

	if (adapter->flags & I40EVF_FLAG_RESET_NEEDED) {
		adapter->flags &= ~I40EVF_FLAG_RESET_NEEDED;
		/* Restart the AQ here. If we have been reset but didn't
		 * detect it, or if the PF had to reinit, our AQ will be hosed.
		 */
		i40e_shutdown_adminq(hw);
		i40e_init_adminq(hw);
		i40evf_request_reset(adapter);
	}

	adapter->flags |= I40EVF_FLAG_RESET_PENDING;

	/* poll until we see the reset actually happen */
	for (i = 0; i < I40EVF_RESET_WAIT_COUNT; i++) {
		reg_val = rd32(hw, I40E_VF_ARQLEN1) &
				  I40E_VF_ARQLEN1_ARQENABLE_MASK;

		if (!reg_val)
			break;

		usleep_range(5000, 10000);
	}

	if (i == I40EVF_RESET_WAIT_COUNT) {
		dev_info(&adapter->pdev->dev, "Never saw reset\n");
		goto continue_reset; /* act like the reset happened */
	}

	/* wait until the reset is complete and the PF is responding to us */
	for (i = 0; i < I40EVF_RESET_WAIT_COUNT; i++) {
		/* sleep first to make sure a minimum wait time is met */
		msleep(I40EVF_RESET_WAIT_MS);

		reg_val = rd32(hw, I40E_VFGEN_RSTAT) &
				  I40E_VFGEN_RSTAT_VFR_STATE_MASK;

		if (reg_val == VIRTCHNL_VFR_VFACTIVE)
			break;
	}

	pci_set_master(adapter->pdev);

	if (i == I40EVF_RESET_WAIT_COUNT) {
		dev_err(&adapter->pdev->dev, "Reset never finished (%x)\n",
				reg_val);
		i40evf_disable_vf(adapter);
		return; /* Do not attempt to reinit. It's dead, Jim. */
	}

continue_reset:
	/* We don't use netif_running() because it may be true prior to
	 * ndo_open() returning, so we can't assume it means all our open
	 * tasks have finished, since we're not holding the rtnl_lock here.
	 */
	running = (adapter->state == __I40EVF_RUNNING);

	if (running) {
		netif_carrier_off(netdev);
		netif_tx_stop_all_queues(netdev);
		adapter->link_up = false;
		i40evf_napi_disable_all(adapter);
	}

	i40evf_irq_disable(adapter);

	adapter->state = __I40EVF_RESETTING;
	adapter->flags &= ~I40EVF_FLAG_RESET_PENDING;

	/* free the Tx/Rx rings and descriptors, might be better to just
	 * re-use them sometime in the future
	 */
	i40evf_free_all_rx_resources(adapter);
	i40evf_free_all_tx_resources(adapter);

	/* kill and reinit the admin queue */
	i40e_shutdown_adminq(hw);
	adapter->current_op = VIRTCHNL_OP_UNKNOWN;
	err = i40e_init_adminq(hw);

	if (err)
		dev_info(&adapter->pdev->dev, "Failed to init adminq: %d\n",
				 err);

	adapter->aq_required = 0;

	if (adapter->flags & I40EVF_FLAG_REINIT_ITR_NEEDED) {
		err = i40evf_reinit_interrupt_scheme(adapter);

		if (err)
			goto reset_err;
	}

	adapter->aq_required |= I40EVF_FLAG_AQ_GET_CONFIG;
	adapter->aq_required |= I40EVF_FLAG_AQ_MAP_VECTORS;

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	/* re-add all MAC filters */
	list_for_each_entry(f, &adapter->mac_filter_list, list) {
		f->add = true;
	}
	/* re-add all VLAN filters */
	list_for_each_entry(vlf, &adapter->vlan_filter_list, list) {
		vlf->add = true;
	}

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	adapter->aq_required |= I40EVF_FLAG_AQ_ADD_MAC_FILTER;
	adapter->aq_required |= I40EVF_FLAG_AQ_ADD_VLAN_FILTER;
	i40evf_misc_irq_enable(adapter);

	mod_timer(&adapter->watchdog_timer, jiffies + 2);

	/* We were running when the reset started, so we need to restore some
	 * state here.
	 */
	if (running) {
		/* allocate transmit descriptors */
		err = i40evf_setup_all_tx_resources(adapter);

		if (err)
			goto reset_err;

		/* allocate receive descriptors */
		err = i40evf_setup_all_rx_resources(adapter);

		if (err)
			goto reset_err;

		if (adapter->flags & I40EVF_FLAG_REINIT_ITR_NEEDED) {
			err = i40evf_request_traffic_irqs(adapter,
											  netdev->name);

			if (err)
				goto reset_err;

			adapter->flags &= ~I40EVF_FLAG_REINIT_ITR_NEEDED;
		}

		i40evf_configure(adapter);

		i40evf_up_complete(adapter);

	} else {
		adapter->state = __I40EVF_DOWN;
		wake_up(&adapter->down_waitqueue);
	}

	clear_bit(__I40EVF_IN_CLIENT_TASK, &adapter->crit_section);
	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);

	return;
reset_err:
	clear_bit(__I40EVF_IN_CLIENT_TASK, &adapter->crit_section);
	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);
	dev_err(&adapter->pdev->dev, "failed to allocate resources during reinit\n");
	i40evf_close(netdev);
}

/**
 * i40evf_adminq_task - worker thread to clean the admin queue
 * @work: pointer to work_struct containing our data
 **/
static void i40evf_adminq_task(struct work_struct * work)
{
	struct i40evf_adapter *adapter =
		container_of(work, struct i40evf_adapter, adminq_task);
	struct i40e_hw *hw = &adapter->hw;
	struct i40e_arq_event_info event;
	enum virtchnl_ops v_op;
	i40e_status ret, v_ret;
	u32 val, oldval;
	u16 pending;

	if (adapter->flags & I40EVF_FLAG_PF_COMMS_FAILED)
		goto out;

	event.buf_len = I40EVF_MAX_AQ_BUF_SIZE;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);

	if (!event.msg_buf)
		goto out;

	do {
		ret = i40e_clean_arq_element(hw, &event, &pending);
		v_op = (enum virtchnl_ops)le32_to_cpu(event.desc.cookie_high);
		v_ret = (i40e_status)le32_to_cpu(event.desc.cookie_low);

		if (ret || !v_op)
			break; /* No event to process or error cleaning ARQ */

		i40evf_virtchnl_completion(adapter, v_op, v_ret, event.msg_buf,
								   event.msg_len);

		if (pending != 0)
			memset(event.msg_buf, 0, I40EVF_MAX_AQ_BUF_SIZE);
	} while (pending);

	if ((adapter->flags &
		 (I40EVF_FLAG_RESET_PENDING | I40EVF_FLAG_RESET_NEEDED)) ||
		adapter->state == __I40EVF_RESETTING)
		goto freedom;

	/* check for error indications */
	val = rd32(hw, hw->aq.arq.len);

	if (val == 0xdeadbeef) /* indicates device in reset */
		goto freedom;

	oldval = val;

	if (val & I40E_VF_ARQLEN1_ARQVFE_MASK) {
		dev_info(&adapter->pdev->dev, "ARQ VF Error detected\n");
		val &= ~I40E_VF_ARQLEN1_ARQVFE_MASK;
	}

	if (val & I40E_VF_ARQLEN1_ARQOVFL_MASK) {
		dev_info(&adapter->pdev->dev, "ARQ Overflow Error detected\n");
		val &= ~I40E_VF_ARQLEN1_ARQOVFL_MASK;
	}

	if (val & I40E_VF_ARQLEN1_ARQCRIT_MASK) {
		dev_info(&adapter->pdev->dev, "ARQ Critical Error detected\n");
		val &= ~I40E_VF_ARQLEN1_ARQCRIT_MASK;
	}

	if (oldval != val)
		wr32(hw, hw->aq.arq.len, val);

	val = rd32(hw, hw->aq.asq.len);
	oldval = val;

	if (val & I40E_VF_ATQLEN1_ATQVFE_MASK) {
		dev_info(&adapter->pdev->dev, "ASQ VF Error detected\n");
		val &= ~I40E_VF_ATQLEN1_ATQVFE_MASK;
	}

	if (val & I40E_VF_ATQLEN1_ATQOVFL_MASK) {
		dev_info(&adapter->pdev->dev, "ASQ Overflow Error detected\n");
		val &= ~I40E_VF_ATQLEN1_ATQOVFL_MASK;
	}

	if (val & I40E_VF_ATQLEN1_ATQCRIT_MASK) {
		dev_info(&adapter->pdev->dev, "ASQ Critical Error detected\n");
		val &= ~I40E_VF_ATQLEN1_ATQCRIT_MASK;
	}

	if (oldval != val)
		wr32(hw, hw->aq.asq.len, val);

freedom:
	kfree(event.msg_buf);
out:
	/* re-enable Admin queue interrupt cause */
	i40evf_misc_irq_enable(adapter);
}

/**
 * i40evf_client_task - worker thread to perform client work
 * @work: pointer to work_struct containing our data
 *
 * This task handles client interactions. Because client calls can be
 * reentrant, we can't handle them in the watchdog.
 **/
static void i40evf_client_task(struct work_struct * work)
{
	struct i40evf_adapter *adapter =
		container_of(work, struct i40evf_adapter, client_task.work);

	/* If we can't get the client bit, just give up. We'll be rescheduled
	 * later.
	 */

	if (test_and_set_bit(__I40EVF_IN_CLIENT_TASK, &adapter->crit_section))
		return;

	if (adapter->flags & I40EVF_FLAG_SERVICE_CLIENT_REQUESTED) {
		i40evf_client_subtask(adapter);
		adapter->flags &= ~I40EVF_FLAG_SERVICE_CLIENT_REQUESTED;
		goto out;
	}

	if (adapter->flags & I40EVF_FLAG_CLIENT_NEEDS_L2_PARAMS) {
		i40evf_notify_client_l2_params(&adapter->vsi);
		adapter->flags &= ~I40EVF_FLAG_CLIENT_NEEDS_L2_PARAMS;
		goto out;
	}

	if (adapter->flags & I40EVF_FLAG_CLIENT_NEEDS_CLOSE) {
		i40evf_notify_client_close(&adapter->vsi, false);
		adapter->flags &= ~I40EVF_FLAG_CLIENT_NEEDS_CLOSE;
		goto out;
	}

	if (adapter->flags & I40EVF_FLAG_CLIENT_NEEDS_OPEN) {
		i40evf_notify_client_open(&adapter->vsi);
		adapter->flags &= ~I40EVF_FLAG_CLIENT_NEEDS_OPEN;
	}

out:
	clear_bit(__I40EVF_IN_CLIENT_TASK, &adapter->crit_section);
}

/**
 * i40evf_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/
void i40evf_free_all_tx_resources(struct i40evf_adapter * adapter)
{
	int i;

	if (!adapter->tx_rings)
		return;

	for (i = 0; i < adapter->num_active_queues; i++)
		if (adapter->tx_rings[i].desc)
			i40e_free_tx_resources(&adapter->tx_rings[i]);
}

/**
 * i40evf_setup_all_tx_resources - allocate all queues Tx resources
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int i40evf_setup_all_tx_resources(struct i40evf_adapter * adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_active_queues; i++) {
		adapter->tx_rings[i].count = adapter->tx_desc_count;
		err = i40e_setup_tx_descriptors(&adapter->tx_rings[i]);

		if (!err)
			continue;

		dev_err(&adapter->pdev->dev,
				"Allocation for Tx Queue %u failed\n", i);
		break;
	}

	return err;
}

/**
 * i40evf_setup_all_rx_resources - allocate all queues Rx resources
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int i40evf_setup_all_rx_resources(struct i40evf_adapter * adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_active_queues; i++) {
		adapter->rx_rings[i].count = adapter->rx_desc_count;
		err = i40e_setup_rx_descriptors(&adapter->rx_rings[i]);

		if (!err)
			continue;

		dev_err(&adapter->pdev->dev,
				"Allocation for Rx Queue %u failed\n", i);
		break;
	}

	return err;
}

/**
 * i40evf_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/
void i40evf_free_all_rx_resources(struct i40evf_adapter * adapter)
{
	int i;

	if (!adapter->rx_rings)
		return;

	for (i = 0; i < adapter->num_active_queues; i++)
		if (adapter->rx_rings[i].desc)
			i40e_free_rx_resources(&adapter->rx_rings[i]);
}

/**
 * i40evf_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
static int i40evf_open(struct net_device * netdev)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	int err;

	if (adapter->flags & I40EVF_FLAG_PF_COMMS_FAILED) {
		dev_err(&adapter->pdev->dev, "Unable to open device due to PF driver failure.\n");
		return -EIO;
	}

	while (test_and_set_bit(__I40EVF_IN_CRITICAL_TASK,
							&adapter->crit_section))
		usleep_range(500, 1000);

	if (adapter->state != __I40EVF_DOWN) {
		err = -EBUSY;
		goto err_unlock;
	}

	/* allocate transmit descriptors */
	err = i40evf_setup_all_tx_resources(adapter);

	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = i40evf_setup_all_rx_resources(adapter);

	if (err)
		goto err_setup_rx;

	/* clear any pending interrupts, may auto mask */
	err = i40evf_request_traffic_irqs(adapter, netdev->name);

	if (err)
		goto err_req_irq;

	i40evf_add_filter(adapter, adapter->hw.mac.addr);
	i40evf_configure(adapter);

	i40evf_up_complete(adapter);

	i40evf_irq_enable(adapter, true);

	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);

	return 0;

err_req_irq:
	i40evf_down(adapter);
	i40evf_free_traffic_irqs(adapter);
err_setup_rx:
	i40evf_free_all_rx_resources(adapter);
err_setup_tx:
	i40evf_free_all_tx_resources(adapter);
err_unlock:
	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);

	return err;
}

/**
 * i40evf_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled. All IRQs except vector 0 (reserved for admin queue)
 * are freed, along with all transmit and receive resources.
 **/
static int i40evf_close(struct net_device * netdev)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	int status;

	if (adapter->state <= __I40EVF_DOWN_PENDING)
		return 0;

	while (test_and_set_bit(__I40EVF_IN_CRITICAL_TASK,
							&adapter->crit_section))
		usleep_range(500, 1000);

	set_bit(__I40E_VSI_DOWN, adapter->vsi.state);

	if (CLIENT_ENABLED(adapter))
		adapter->flags |= I40EVF_FLAG_CLIENT_NEEDS_CLOSE;

	i40evf_down(adapter);
	adapter->state = __I40EVF_DOWN_PENDING;
	i40evf_free_traffic_irqs(adapter);

	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);

	/* We explicitly don't free resources here because the hardware is
	 * still active and can DMA into memory. Resources are cleared in
	 * i40evf_virtchnl_completion() after we get confirmation from the PF
	 * driver that the rings have been stopped.
	 *
	 * Also, we wait for state to transition to __I40EVF_DOWN before
	 * returning. State change occurs in i40evf_virtchnl_completion() after
	 * VF resources are released (which occurs after PF driver processes and
	 * responds to admin queue commands).
	 */

	status = wait_event_timeout(adapter->down_waitqueue,
								adapter->state == __I40EVF_DOWN,
								msecs_to_jiffies(200));

	if (!status)
		netdev_warn(netdev, "Device resources not yet released\n");

	return 0;
}

/**
 * i40evf_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/
static struct net_device_stats *i40evf_get_stats(struct net_device * netdev)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	/* only return the current stats */
	return &adapter->net_stats;
}

/**
 * i40evf_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int i40evf_change_mtu(struct net_device * netdev, int new_mtu)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	int max_frame = new_mtu + I40E_PACKET_HDR_PAD;

	if ((new_mtu < 68) || (max_frame > I40E_MAX_RXBUFFER))
		return -EINVAL;

#ifndef HAVE_NDO_FEATURES_CHECK

	/* MTU < 576 causes problems with TSO */
	if (new_mtu < 576) {
		netdev->features &= ~NETIF_F_TSO;
		netdev->features &= ~NETIF_F_TSO6;
	} else {
#ifdef HAVE_NDO_SET_FEATURES
#ifndef HAVE_RHEL6_NET_DEVICE_OPS_EXT

		if (netdev->wanted_features & NETIF_F_TSO)
			netdev->features |= NETIF_F_TSO;

		if (netdev->wanted_features & NETIF_F_TSO6)
			netdev->features |= NETIF_F_TSO6;

#else

		if (netdev_extended(netdev)->wanted_features & NETIF_F_TSO)
			netdev->features |= NETIF_F_TSO;

		if (netdev_extended(netdev)->wanted_features & NETIF_F_TSO6)
			netdev->features |= NETIF_F_TSO6;

#endif /* HAVE_RHEL6_NET_DEVICE_OPS_EXT */
#else
		netdev->features |= NETIF_F_TSO;
		netdev->features |= NETIF_F_TSO6;
#endif /* HAVE_NDO_SET_FEATURES */
	}

#else
	netdev->features |= NETIF_F_TSO;
	netdev->features |= NETIF_F_TSO6;
#endif /* !HAVE_NDO_FEATURES_CHECK */
	netdev_info(netdev, "changing MTU from %d to %d\n",
				netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;
	adapter->flags |= I40EVF_FLAG_RESET_NEEDED;
	schedule_work(&adapter->reset_task);

	return 0;
}

/**
 * i40evf_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 * Note: expects to be called while under rtnl_lock()
 **/
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
static int i40evf_set_features(struct net_device * netdev, u32 features)
#else
static int i40evf_set_features(struct net_device * netdev,
							   netdev_features_t features)
#endif
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	/* Don't allow changing VLAN_RX flag when VLAN is set for VF
	 * and return an error in this case
	 */
	if (VLAN_ALLOWED(adapter)) {
#ifdef NETIF_F_HW_VLAN_CTAG_RX

		if (features & NETIF_F_HW_VLAN_CTAG_RX)
#else
		if (features & NETIF_F_HW_VLAN_RX)
#endif
			adapter->aq_required |=
				I40EVF_FLAG_AQ_ENABLE_VLAN_STRIPPING;
		else
			adapter->aq_required |=
				I40EVF_FLAG_AQ_DISABLE_VLAN_STRIPPING;

#ifdef NETIF_F_HW_VLAN_CTAG_RX
	} else if ((netdev->features ^ features) & NETIF_F_HW_VLAN_CTAG_RX) {
#else
	} else if ((netdev->features ^ features) & NETIF_F_HW_VLAN_RX) {
#endif
		return -EINVAL;
	}

	return 0;
}

#ifdef HAVE_NDO_FEATURES_CHECK
/**
 * i40evf_features_check - Validate encapsulated packet conforms to limits
 * @skb: skb buff
 * @netdev: This physical port's netdev
 * @features: Offload features that the stack believes apply
 **/
static netdev_features_t i40evf_features_check(struct sk_buff * skb,
		struct net_device * dev,
		netdev_features_t features)
{
	size_t len;

	/* No point in doing any of this if neither checksum nor GSO are
	 * being requested for this frame.  We can rule out both by just
	 * checking for CHECKSUM_PARTIAL
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return features;

	/* We cannot support GSO if the MSS is going to be less than
	 * 64 bytes.  If it is then we need to drop support for GSO.
	 */
	if (skb_is_gso(skb) && (skb_shinfo(skb)->gso_size < 64))
		features &= ~NETIF_F_GSO_MASK;

	/* MACLEN can support at most 63 words */
	len = skb_network_header(skb) - skb->data;

	if (len & ~(63 * 2))
		goto out_err;

	/* IPLEN and EIPLEN can support at most 127 dwords */
	len = skb_transport_header(skb) - skb_network_header(skb);

	if (len & ~(127 * 4))
		goto out_err;

	if (skb->encapsulation) {
		/* L4TUNLEN can support 127 words */
		len = skb_inner_network_header(skb) - skb_transport_header(skb);

		if (len & ~(127 * 2))
			goto out_err;

		/* IPLEN can support at most 127 dwords */
		len = skb_inner_transport_header(skb) -
			  skb_inner_network_header(skb);

		if (len & ~(127 * 4))
			goto out_err;
	}

	/* No need to validate L4LEN as TCP is the only protocol with a
	 * a flexible value and we support all possible values supported
	 * by TCP, which is at most 15 dwords
	 */

	return features;
out_err:
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

#endif /* HAVE_NDO_FEATURES_CHECK */
#ifdef HAVE_NDO_SET_FEATURES
/**
 * i40evf_fix_features - fix up the netdev feature bits
 * @netdev: our net device
 * @features: desired feature bits
 *
 * Returns fixed-up features bits
 **/
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
static u32 i40evf_fix_features(struct net_device * netdev, u32 features)
#else
static netdev_features_t i40evf_fix_features(struct net_device * netdev,
		netdev_features_t features)
#endif
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	if (!(adapter->vf_res->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_VLAN))
#ifdef NETIF_F_HW_VLAN_CTAG_RX
		features &= ~(NETIF_F_HW_VLAN_CTAG_TX |
					  NETIF_F_HW_VLAN_CTAG_RX |
					  NETIF_F_HW_VLAN_CTAG_FILTER);

#else
		features &= ~(NETIF_F_HW_VLAN_TX |
					  NETIF_F_HW_VLAN_RX |
					  NETIF_F_HW_VLAN_FILTER);
#endif

	return features;
}

#endif /* HAVE_NDO_SET_FEATURES */
static const struct net_device_ops i40evf_netdev_ops = {
	.ndo_open		= i40evf_open,
	.ndo_stop		= i40evf_close,
	.ndo_start_xmit		= i40e_lan_xmit_frame,
	.ndo_get_stats		= i40evf_get_stats,
	.ndo_set_rx_mode	= i40evf_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= i40evf_set_mac,
	.ndo_change_mtu		= i40evf_change_mtu,
	.ndo_tx_timeout		= i40evf_tx_timeout,
#ifdef HAVE_VLAN_RX_REGISTER
	.ndo_vlan_rx_register	= i40evf_vlan_rx_register,
#endif
	.ndo_vlan_rx_add_vid	= i40evf_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= i40evf_vlan_rx_kill_vid,
#ifdef HAVE_NDO_FEATURES_CHECK
	.ndo_features_check     = i40evf_features_check,
#endif /* HAVE_NDO_FEATURES_CHECK */
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
};

/* RHEL6 keeps these operations in a separate structure */
static const struct net_device_ops_ext i40evf_netdev_ops_ext = {
	.size			= sizeof(struct net_device_ops_ext),
#endif /* HAVE_RHEL6_NET_DEVICE_OPS_EXT */
#ifdef HAVE_NDO_SET_FEATURES
	.ndo_fix_features	= i40evf_fix_features,
	.ndo_set_features	= i40evf_set_features,
#endif /* HAVE_NDO_SET_FEATURES */
};

/**
 * i40evf_check_reset_complete - check that VF reset is complete
 * @hw: pointer to hw struct
 *
 * Returns 0 if device is ready to use, or -EBUSY if it's in reset.
 **/
static int i40evf_check_reset_complete(struct i40e_hw * hw)
{
	u32 rstat;
	int i;

	for (i = 0; i < 100; i++) {
		rstat = rd32(hw, I40E_VFGEN_RSTAT) &
				I40E_VFGEN_RSTAT_VFR_STATE_MASK;

		if ((rstat == VIRTCHNL_VFR_VFACTIVE) ||
			(rstat == VIRTCHNL_VFR_COMPLETED))
			return 0;

		usleep_range(10, 20);
	}

	return -EBUSY;
}

/**
 * i40evf_process_config - Process the config information we got from the PF
 * @adapter: board private structure
 *
 * Verify that we have a valid config struct, and set up our netdev features
 * and our VSI struct.
 **/
int i40evf_process_config(struct i40evf_adapter * adapter)
{
	struct virtchnl_vf_resource *vfres = adapter->vf_res;
	int i, num_req_queues = adapter->num_req_queues;
	struct net_device *netdev = adapter->netdev;
	struct i40e_vsi *vsi = &adapter->vsi;
	netdev_features_t hw_enc_features;
	netdev_features_t hw_features;

	/* got VF config message back from PF, now we can parse it */
	for (i = 0; i < vfres->num_vsis; i++) {
		if (vfres->vsi_res[i].vsi_type == VIRTCHNL_VSI_SRIOV)
			adapter->vsi_res = &vfres->vsi_res[i];
	}

	if (!adapter->vsi_res) {
		dev_err(&adapter->pdev->dev, "No LAN VSI found\n");
		return -ENODEV;
	}

	dev_info(&adapter->pdev->dev, "Requested %d queues, PF gave us %d.\n",
			 num_req_queues,
			 adapter->vsi_res->num_queue_pairs);

	if (num_req_queues &&
		num_req_queues != adapter->vsi_res->num_queue_pairs) {
		/* Problem.  The PF gave us fewer queues than what we had
		 * negotiated in our request.  Need a reset to see if we can't
		 * get back to a working state.
		 */
		dev_err(&adapter->pdev->dev,
				"Requested %d queues, but PF only gave us %d.\n",
				num_req_queues,
				adapter->vsi_res->num_queue_pairs);
		adapter->flags |= I40EVF_FLAG_REINIT_ITR_NEEDED;
		adapter->num_req_queues = adapter->vsi_res->num_queue_pairs;
		i40evf_schedule_reset(adapter);
		return -ENODEV;
	}

	adapter->num_req_queues = 0;

	hw_enc_features = NETIF_F_SG			|
					  NETIF_F_IP_CSUM		|
#ifdef NETIF_F_IPV6_CSUM
					  NETIF_F_IPV6_CSUM		|
#endif
					  NETIF_F_HIGHDMA		|
#ifdef NETIF_F_SOFT_FEATURES
					  NETIF_F_SOFT_FEATURES	|
#endif
					  NETIF_F_TSO			|
					  NETIF_F_TSO_ECN		|
					  NETIF_F_TSO6			|
					  NETIF_F_SCTP_CRC		|
#ifdef NETIF_F_RXHASH
					  NETIF_F_RXHASH		|
#endif
#ifdef HAVE_NDO_SET_FEATURES
					  NETIF_F_RXCSUM		|
#endif
					  0;

#ifdef HAVE_ENCAP_CSUM_OFFLOAD

	/* advertise to stack only if offloads for encapsulated packets is
	 * supported
	 */
	if (vfres->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_ENCAP) {
#ifdef HAVE_ENCAP_TSO_OFFLOAD
		hw_enc_features |= NETIF_F_GSO_UDP_TUNNEL	|
#ifdef HAVE_GRE_ENCAP_OFFLOAD
						   NETIF_F_GSO_GRE		|
#ifdef NETIF_F_GSO_PARTIAL
						   NETIF_F_GSO_GRE_CSUM		|
						   NETIF_F_GSO_PARTIAL		|
#endif
						   NETIF_F_GSO_UDP_TUNNEL_CSUM	|
#ifdef NETIF_F_GSO_IPXIP4
						   NETIF_F_GSO_IPXIP4		|
#ifdef NETIF_F_GSO_IPXIP6
						   NETIF_F_GSO_IPXIP6		|
#endif
#else /* NETIF_F_GSO_IPXIP4 */
#ifdef NETIF_F_GSO_IPIP
						   NETIF_F_GSO_IPIP		|
#endif
#ifdef NETIF_F_GSO_SIT
						   NETIF_F_GSO_SIT		|
#endif
#endif /* NETIF_F_GSO_IPXIP4 */
#endif /* NETIF_F_GRE_ENCAP_OFFLOAD */
						   0;

		if (!(vfres->vf_cap_flags &
			  VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM))
#ifndef NETIF_F_GSO_PARTIAL
			hw_enc_features ^= NETIF_F_GSO_UDP_TUNNEL_CSUM;

#else
			netdev->gso_partial_features |=
				NETIF_F_GSO_UDP_TUNNEL_CSUM;

		netdev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;
		netdev->hw_enc_features |= NETIF_F_TSO_MANGLEID;
#endif /* !NETIF_F_GSO_PARTIAL */
#endif /* HAVE_ENCAP_TSO_OFFLOAD */
		netdev->hw_enc_features |= hw_enc_features;
	}

#endif /* HAVE_ENCAP_CSUM_OFFLOAD */

#ifdef HAVE_NETDEV_VLAN_FEATURES
	/* record features VLANs can make use of */
#ifdef NETIF_F_GSO_PARTIAL
	netdev->vlan_features |= hw_enc_features | NETIF_F_TSO_MANGLEID;
#else
	netdev->vlan_features |= hw_enc_features;
#endif
#endif

	/* Write features and hw_features separately to avoid polluting
	 * with, or dropping, features that are set when we registered.
	 */
	hw_features = hw_enc_features;

	/* Enable VLAN features if supported */
	if (vfres->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_VLAN)
#ifdef NETIF_F_HW_VLAN_CTAG_RX
		hw_features |= (NETIF_F_HW_VLAN_CTAG_TX |
						NETIF_F_HW_VLAN_CTAG_RX);

#else
		hw_features |= (NETIF_F_HW_VLAN_TX |
						NETIF_F_HW_VLAN_RX);
#endif

#ifdef HAVE_NDO_SET_FEATURES
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
	hw_features |= get_netdev_hw_features(netdev);
	set_netdev_hw_features(netdev, hw_features);
#else
	netdev->hw_features |= hw_features;
#endif
#endif /* HAVE_NDO_SET_FEATURES */

	netdev->features |= hw_features;

	if (vfres->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_VLAN)
#ifdef NETIF_F_HW_VLAN_CTAG_RX
		netdev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

#else
		netdev->features |= NETIF_F_HW_VLAN_FILTER;
#endif

	adapter->vsi.id = adapter->vsi_res->vsi_id;

	adapter->vsi.back = adapter;
	adapter->vsi.base_vector = 1;
	adapter->vsi.work_limit = I40E_DEFAULT_IRQ_WORK;
	vsi->netdev = adapter->netdev;
	vsi->qs_handle = adapter->vsi_res->qset_handle;

	if (vfres->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_RSS_PF) {
		adapter->rss_key_size = vfres->rss_key_size;
		adapter->rss_lut_size = vfres->rss_lut_size;
	} else {
		adapter->rss_key_size = I40EVF_HKEY_ARRAY_SIZE;
		adapter->rss_lut_size = I40EVF_HLUT_ARRAY_SIZE;
	}

	return 0;
}

/**
 * i40evf_init_task - worker thread to perform delayed initialization
 * @work: pointer to work_struct containing our data
 *
 * This task completes the work that was begun in probe. Due to the nature
 * of VF-PF communications, we may need to wait tens of milliseconds to get
 * responses back from the PF. Rather than busy-wait in probe and bog down the
 * whole system, we'll do it in a task so we can sleep.
 * This task only runs during driver init. Once we've established
 * communications with the PF driver and set up our netdev, the watchdog
 * takes over.
 **/
static void i40evf_init_task(struct work_struct * work)
{
	struct i40evf_adapter *adapter = container_of(work,
									 struct i40evf_adapter,
									 init_task.work);
	struct net_device *netdev = adapter->netdev;
	struct i40e_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	int err, bufsz;

	switch (adapter->state) {
		case __I40EVF_STARTUP:
			/* driver loaded, probe complete */
			adapter->flags &= ~I40EVF_FLAG_PF_COMMS_FAILED;
			adapter->flags &= ~I40EVF_FLAG_RESET_PENDING;
			err = i40e_set_mac_type(hw);

			if (err) {
				dev_err(&pdev->dev, "Failed to set MAC type (%d)\n",
						err);
				goto err;
			}

			err = i40evf_check_reset_complete(hw);

			if (err) {
				dev_info(&pdev->dev, "Device is still in reset (%d), retrying\n",
						 err);
				goto err;
			}

			hw->aq.num_arq_entries = I40EVF_AQ_LEN;
			hw->aq.num_asq_entries = I40EVF_AQ_LEN;
			hw->aq.arq_buf_size = I40EVF_MAX_AQ_BUF_SIZE;
			hw->aq.asq_buf_size = I40EVF_MAX_AQ_BUF_SIZE;

			err = i40e_init_adminq(hw);

			if (err) {
				dev_err(&pdev->dev, "Failed to init Admin Queue (%d)\n",
						err);
				goto err;
			}

			err = i40evf_send_api_ver(adapter);

			if (err) {
				dev_err(&pdev->dev, "Unable to send to PF (%d)\n", err);
				i40e_shutdown_adminq(hw);
				goto err;
			}

			adapter->state = __I40EVF_INIT_VERSION_CHECK;
			goto restart;

		case __I40EVF_INIT_VERSION_CHECK:
			if (!i40e_asq_done(hw)) {
				dev_err(&pdev->dev, "Admin queue command never completed\n");
				i40e_shutdown_adminq(hw);
				adapter->state = __I40EVF_STARTUP;
				goto err;
			}

			/* aq msg sent, awaiting reply */
			err = i40evf_verify_api_ver(adapter);

			if (err) {
				if (err == I40E_ERR_ADMIN_QUEUE_NO_WORK)
					err = i40evf_send_api_ver(adapter);
				else
					dev_err(&pdev->dev, "Unsupported PF API version %d.%d, expected %d.%d\n",
							adapter->pf_version.major,
							adapter->pf_version.minor,
							VIRTCHNL_VERSION_MAJOR,
							VIRTCHNL_VERSION_MINOR);

				goto err;
			}

			err = i40evf_send_vf_config_msg(adapter);

			if (err) {
				dev_err(&pdev->dev, "Unable to send config request (%d)\n",
						err);
				goto err;
			}

			adapter->state = __I40EVF_INIT_GET_RESOURCES;
			goto restart;

		case __I40EVF_INIT_GET_RESOURCES:

			/* aq msg sent, awaiting reply */
			if (!adapter->vf_res) {
				bufsz = sizeof(struct virtchnl_vf_resource) +
						(I40E_MAX_VF_VSI *
						 sizeof(struct virtchnl_vsi_resource));
				adapter->vf_res = kzalloc(bufsz, GFP_KERNEL);

				if (!adapter->vf_res)
					goto err;
			}

			err = i40evf_get_vf_config(adapter);

			if (err == I40E_ERR_ADMIN_QUEUE_NO_WORK) {
				err = i40evf_send_vf_config_msg(adapter);
				goto err;
			} else if (err == I40E_ERR_PARAM) {
				/* We only get ERR_PARAM if the device is in a very bad
				 * state or if we've been disabled for previous bad
				 * behavior. Either way, we're done now.
				 */
				i40e_shutdown_adminq(hw);
				dev_err(&pdev->dev, "Unable to get VF config due to PF error condition, not retrying\n");
				return;
			}

			if (err) {
				dev_err(&pdev->dev, "Unable to get VF config (%d)\n",
						err);
				goto err_alloc;
			}

			adapter->state = __I40EVF_INIT_SW;
			break;

		default:
			goto err_alloc;
	}

	if (i40evf_process_config(adapter))
		goto err_alloc;

	adapter->current_op = VIRTCHNL_OP_UNKNOWN;

	adapter->flags |= I40EVF_FLAG_RX_CSUM_ENABLED;

#ifndef HAVE_SWIOTLB_SKIP_CPU_SYNC
	/* force legacy Rx mode if SKIP_CPU_SYNC is not supported */
	adapter->flags |= I40EVF_FLAG_LEGACY_RX;
#endif
	netdev->netdev_ops = &i40evf_netdev_ops;
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
	set_netdev_ops_ext(netdev, &i40evf_netdev_ops_ext);
#endif
	i40evf_set_ethtool_ops(netdev);
	netdev->watchdog_timeo = 5 * HZ;

#ifdef HAVE_NETDEVICE_MIN_MAX_MTU
	/* MTU range: 68 - 9710 */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = I40E_MAX_RXBUFFER - I40E_PACKET_HDR_PAD;
#endif

	if (!is_valid_ether_addr(adapter->hw.mac.addr)) {
		dev_info(&pdev->dev, "Invalid MAC address %pM, using random\n",
				 adapter->hw.mac.addr);
		eth_hw_addr_random(netdev);
		ether_addr_copy(adapter->hw.mac.addr, netdev->dev_addr);
	} else {
		adapter->flags |= I40EVF_FLAG_ADDR_SET_BY_PF;
		ether_addr_copy(netdev->dev_addr, adapter->hw.mac.addr);
		ether_addr_copy(netdev->perm_addr, adapter->hw.mac.addr);
	}

#ifdef DEV_HPCAP

	adapter->bd_number = hpcap_get_iface_number_or_default(pdev);

	hpcap_parse_opts(adapter);

	adapters[adapters_found] = adapter;
	adapters_found++;

	adapter->hpcap_client_discard = 0;
	adapter->hpcap_client_loss = 0;
#endif /* DEV_HPCAP */

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &i40evf_watchdog_timer;
	adapter->watchdog_timer.data = (unsigned long)adapter;
	mod_timer(&adapter->watchdog_timer, jiffies + 1);

	adapter->tx_desc_count = I40EVF_DEFAULT_TXD;
	adapter->rx_desc_count = I40EVF_DEFAULT_RXD;
	err = i40evf_init_interrupt_scheme(adapter);

	if (err)
		goto err_sw_init;

	i40evf_map_rings_to_vectors(adapter);

	if (adapter->vf_res->vf_cap_flags &
		VIRTCHNL_VF_OFFLOAD_WB_ON_ITR)
		adapter->flags |= I40EVF_FLAG_WB_ON_ITR_CAPABLE;

	err = i40evf_request_misc_irq(adapter);

	if (err)
		goto err_sw_init;

	netif_carrier_off(netdev);
	adapter->link_up = false;

	if (!adapter->netdev_registered) {
#ifdef DEV_HPCAP

		if (is_hpcap_adapter(adapter))
			sprintf(netdev->name, "hpcap%d", adapter->bd_number);
		else
			sprintf(netdev->name, "eth%d", adapter->bd_number);

#endif /* DEV_HPCAP */

		err = register_netdev(netdev);

		if (err)
			goto err_register;
	}

	adapter->netdev_registered = true;

	netif_tx_stop_all_queues(netdev);

	if (CLIENT_ALLOWED(adapter)) {
		err = i40evf_lan_add_device(adapter);

		if (err)
			dev_info(&pdev->dev, "Failed to add VF to client API service list: %d\n",
					 err);
	}

	dev_info(&pdev->dev, "MAC address: %pM\n", adapter->hw.mac.addr);

	if (netdev->features & NETIF_F_GRO)
		dev_info(&pdev->dev, "GRO is enabled\n");

	adapter->state = __I40EVF_DOWN;
	set_bit(__I40E_VSI_DOWN, adapter->vsi.state);
	i40evf_misc_irq_enable(adapter);
	wake_up(&adapter->down_waitqueue);

	adapter->rss_key = kzalloc(adapter->rss_key_size, GFP_KERNEL);
	adapter->rss_lut = kzalloc(adapter->rss_lut_size, GFP_KERNEL);

	if (!adapter->rss_key || !adapter->rss_lut)
		goto err_mem;

	if (RSS_AQ(adapter)) {
		adapter->aq_required |= I40EVF_FLAG_AQ_CONFIGURE_RSS;
		mod_timer_pending(&adapter->watchdog_timer, jiffies + 1);
	} else
		i40evf_init_rss(adapter);

	return;
restart:
	schedule_delayed_work(&adapter->init_task, msecs_to_jiffies(30));
	return;
err_mem:
	i40evf_free_rss(adapter);
err_register:
	i40evf_free_misc_irq(adapter);
err_sw_init:
	i40evf_reset_interrupt_capability(adapter);
err_alloc:
	kfree(adapter->vf_res);
	adapter->vf_res = NULL;
err:

	/* Things went into the weeds, so try again later */
	if (++adapter->aq_wait_count > I40EVF_AQ_MAX_ERR) {
		dev_err(&pdev->dev, "Failed to communicate with PF; waiting before retry\n");
		adapter->flags |= I40EVF_FLAG_PF_COMMS_FAILED;
		i40e_shutdown_adminq(hw);
		adapter->state = __I40EVF_STARTUP;
		schedule_delayed_work(&adapter->init_task, HZ * 5);
		return;
	}

	schedule_delayed_work(&adapter->init_task, HZ);
}

/**
 * i40evf_shutdown - Shutdown the device in preparation for a reboot
 * @pdev: pci device structure
 **/
static void i40evf_shutdown(struct pci_dev * pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (netif_running(netdev))
		i40evf_close(netdev);

	/* Prevent the watchdog from running. */
	adapter->state = __I40EVF_REMOVE;
	adapter->aq_required = 0;

#ifdef CONFIG_PM
	pci_save_state(pdev);

#endif
	pci_disable_device(pdev);
}

/**
 * i40evf_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in i40evf_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * i40evf_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
#ifdef HAVE_CONFIG_HOTPLUG
static int __devinit i40evf_probe(struct pci_dev * pdev,
								  const struct pci_device_id * ent)
#else
static int i40evf_probe(struct pci_dev * pdev, const struct pci_device_id * ent)
#endif
{
	struct net_device *netdev;
	struct i40evf_adapter *adapter = NULL;
	struct i40e_hw *hw = NULL;
	int err;

	err = pci_enable_device(pdev);

	if (err)
		return err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));

		if (err) {
			dev_err(&pdev->dev,
					"DMA configuration failed: 0x%x\n", err);
			goto err_dma;
		}
	}

	err = pci_request_regions(pdev, i40evf_driver_name);

	if (err) {
		dev_err(pci_dev_to_dev(pdev),
				"pci_request_regions failed 0x%x\n", err);
		goto err_pci_reg;
	}

	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);

	netdev = alloc_etherdev_mq(sizeof(struct i40evf_adapter), MAX_QUEUES);

	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);

	adapter->netdev = netdev;
	adapter->pdev = pdev;

	hw = &adapter->hw;
	hw->back = adapter;

	adapter->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;
	adapter->state = __I40EVF_STARTUP;

	/* Call save state here because it relies on the adapter struct. */
	pci_save_state(pdev);

	hw->hw_addr = ioremap(pci_resource_start(pdev, 0),
						  pci_resource_len(pdev, 0));

	if (!hw->hw_addr) {
		err = -EIO;
		goto err_ioremap;
	}

	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;
	hw->bus.device = PCI_SLOT(pdev->devfn);
	hw->bus.func = PCI_FUNC(pdev->devfn);
	hw->bus.bus_id = pdev->bus->number;

	/* set up the spinlocks for the AQ, do this only once in probe
	 * and destroy them only once in remove
	 */
	i40e_init_spinlock_d(&hw->aq.asq_spinlock);
	i40e_init_spinlock_d(&hw->aq.arq_spinlock);

	spin_lock_init(&adapter->mac_vlan_list_lock);

	INIT_LIST_HEAD(&adapter->mac_filter_list);
	INIT_LIST_HEAD(&adapter->vlan_filter_list);

	INIT_WORK(&adapter->reset_task, i40evf_reset_task);
	INIT_WORK(&adapter->adminq_task, i40evf_adminq_task);
	INIT_WORK(&adapter->watchdog_task, i40evf_watchdog_task);
	INIT_DELAYED_WORK(&adapter->client_task, i40evf_client_task);
	INIT_DELAYED_WORK(&adapter->init_task, i40evf_init_task);
	schedule_delayed_work(&adapter->init_task,
						  msecs_to_jiffies(5 * (pdev->devfn & 0x07)));

	/* Setup the wait queue for indicating transition to down status */
	init_waitqueue_head(&adapter->down_waitqueue);

	return 0;

err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

#ifdef CONFIG_PM
/**
 * i40evf_suspend - Power management suspend routine
 * @pdev: PCI device information struct
 * @state: unused
 *
 * Called when the system (VM) is entering sleep/suspend.
 **/
static int i40evf_suspend(struct pci_dev * pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	int retval = 0;

	netif_device_detach(netdev);

	while (test_and_set_bit(__I40EVF_IN_CRITICAL_TASK,
							&adapter->crit_section))
		usleep_range(500, 1000);

	if (netif_running(netdev)) {
		rtnl_lock();
		i40evf_down(adapter);
		rtnl_unlock();
	}

	i40evf_free_misc_irq(adapter);
	i40evf_reset_interrupt_capability(adapter);

	clear_bit(__I40EVF_IN_CRITICAL_TASK, &adapter->crit_section);

	retval = pci_save_state(pdev);

	if (retval)
		return retval;

	pci_disable_device(pdev);

	return 0;
}

/**
 * i40evf_resume - Power management resume routine
 * @pdev: PCI device information struct
 *
 * Called when the system (VM) is resumed from sleep/suspend.
 **/
static int i40evf_resume(struct pci_dev * pdev)
{
	struct i40evf_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	/* pci_restore_state clears dev->state_saved so call
	 * pci_save_state to restore it.
	 */
	pci_save_state(pdev);

	err = pci_enable_device_mem(pdev);

	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device from suspend.\n");
		return err;
	}

	pci_set_master(pdev);

	rtnl_lock();
	err = i40evf_set_interrupt_capability(adapter);

	if (err) {
		dev_err(&pdev->dev, "Cannot enable MSI-X interrupts.\n");
		return err;
	}

	err = i40evf_request_misc_irq(adapter);
	rtnl_unlock();

	if (err) {
		dev_err(&pdev->dev, "Cannot get interrupt vector.\n");
		return err;
	}

	schedule_work(&adapter->reset_task);

	netif_device_attach(netdev);

	return err;
}

#endif /* CONFIG_PM */
/**
 * i40evf_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * i40evf_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
#ifdef HAVE_CONFIG_HOTPLUG
static void __devexit i40evf_remove(struct pci_dev * pdev)
#else
static void i40evf_remove(struct pci_dev * pdev)
#endif
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40evf_vlan_filter *vlf, *vlftmp;
	struct i40evf_mac_filter *f, *ftmp;
	struct i40e_hw *hw = &adapter->hw;
	int err;
	/* Indicate we are in remove and not to run reset_task */
	set_bit(__I40EVF_IN_REMOVE_TASK, &adapter->crit_section);
	cancel_delayed_work_sync(&adapter->init_task);
	cancel_work_sync(&adapter->reset_task);
	cancel_delayed_work_sync(&adapter->client_task);

	if (adapter->netdev_registered) {
		/* will call i40evf_close if the device was open previously */
		unregister_netdev(netdev);
		adapter->netdev_registered = false;
	}

	if (CLIENT_ALLOWED(adapter)) {
		err = i40evf_lan_del_device(adapter);

		if (err)
			dev_warn(&pdev->dev, "Failed to delete client device: %d\n",
					 err);
	}

	/* Shut down all the garbage mashers on the detention level */
	adapter->state = __I40EVF_REMOVE;
	adapter->aq_required = 0;
	i40evf_request_reset(adapter);
	msleep(50);

	/* If the FW isn't responding, kick it once, but only once. */
	if (!i40e_asq_done(hw)) {
		i40evf_request_reset(adapter);
		msleep(50);
	}

	i40evf_free_all_tx_resources(adapter);
	i40evf_free_all_rx_resources(adapter);
	i40evf_misc_irq_disable(adapter);
	i40evf_free_misc_irq(adapter);
	i40evf_reset_interrupt_capability(adapter);
	i40evf_free_q_vectors(adapter);

	if (adapter->watchdog_timer.function)
		del_timer_sync(&adapter->watchdog_timer);

	i40evf_free_rss(adapter);

	if (hw->aq.asq.count)
		i40e_shutdown_adminq(hw);

	/* destroy the locks only once, here */
	i40e_destroy_spinlock_d(&hw->aq.arq_spinlock);
	i40e_destroy_spinlock_d(&hw->aq.asq_spinlock);

	iounmap(hw->hw_addr);
	pci_release_regions(pdev);

	i40evf_free_queues(adapter);
	kfree(adapter->vf_res);
	adapter->vf_res = NULL;

	spin_lock_bh(&adapter->mac_vlan_list_lock);

	/* If we got removed before an up/down sequence, we've got a filter
	 * hanging out there that we need to get rid of.
	 */
	list_for_each_entry_safe(f, ftmp, &adapter->mac_filter_list, list) {
		list_del(&f->list);
		kfree(f);
	}

	list_for_each_entry_safe(vlf, vlftmp, &adapter->vlan_filter_list,
							 list) {
		list_del(&vlf->list);
		kfree(vlf);
	}

	spin_unlock_bh(&adapter->mac_vlan_list_lock);

	free_netdev(netdev);

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

static struct pci_driver i40evf_driver = {
	.name     = i40evf_driver_name,
	.id_table = i40evf_pci_tbl,
	.probe    = i40evf_probe,
#ifdef HAVE_CONFIG_HOTPLUG
	.remove   = __devexit_p(i40evf_remove),
#else
	.remove   = i40evf_remove,
#endif
#ifdef CONFIG_PM
	.suspend  = i40evf_suspend,
	.resume   = i40evf_resume,
#endif
	.shutdown = i40evf_shutdown,
};

/**
 * i40e_init_module - Driver Registration Routine
 *
 * i40e_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init i40evf_init_module(void)
{
#ifdef DEV_HPCAP
	int devs_found;
#endif
	int ret;

	pr_info("i40evf: %s - version %s\n", i40evf_driver_string,
			i40evf_driver_version);

	pr_info("%s\n", i40evf_copyright);

#ifdef DEV_HPCAP
	ret = hpcap_precheck_options();

	if (ret < 0) {
		BPRINTK(ERR, "option precheck failed with code %d\n", ret);
		return ret;
	}

	devs_found = hpcap_fix_iface_numbers(i40evf_pci_tbl);

	printdbg(DBG_DRV, "Found %d devices, fixed adapter indexes.\n", devs_found);

#endif

	i40evf_wq = alloc_workqueue("%s", WQ_MEM_RECLAIM, 0,
								i40evf_driver_name);

	if (!i40evf_wq) {
		pr_err("%s: Failed to create workqueue\n", i40evf_driver_name);
		return -ENOMEM;
	}

	ret = pci_register_driver(&i40evf_driver);

#ifdef DEV_HPCAP
	msleep(500); // Allow for all delayed jobs to finish so we have all adapters
	ret = hpcap_register_adapters();
#endif /* DEV_HPCAP */

	return ret;
}

module_init(i40evf_init_module);

/**
 * i40e_exit_module - Driver Exit Cleanup Routine
 *
 * i40e_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit i40evf_exit_module(void)
{
#ifdef DEV_HPCAP
	int i;

	for (i = 0; i < adapters_found; i++)
		hpcap_unregister_chardev(adapters[i]);

	BPRINTK(INFO, "HPCAP unloaded.");
#endif
	pci_unregister_driver(&i40evf_driver);
	destroy_workqueue(i40evf_wq);
}

module_exit(i40evf_exit_module);

/* i40evf_main.c */
