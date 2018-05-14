/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright (c) 1999 - 2014 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _IXGBEVF_H_
#define _IXGBEVF_H_

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>

#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif
#ifdef NETIF_F_HW_VLAN_TX
#include <linux/if_vlan.h>
#endif
#ifdef HAVE_NDO_GET_STATS64
#include <linux/u64_stats_sync.h>
#endif

#include "kcompat.h"

#include "ixgbe_type.h"
#include "ixgbe_vf.h"

#ifdef CONFIG_NET_RX_BUSY_POLL
#include <net/busy_poll.h>
#define BP_EXTENDED_STATS
#endif

#define IXGBEVF

#define DEV_HPCAP //comment to stop using HPCAP
#ifdef DEV_HPCAP
/* disable features we don't need any more...*/
#define IXGBE_NO_LRO    1
#define IXGBE_NO_HW_RSC 1
#include "hpcap.h"
#include "hpcap_debug.h"

#ifdef HPCAP_SYSFS
#include "hpcap_sysfs_types.h"
#endif

#endif /* DEV_HPCAP */

#define IXGBE_MAX_TXD_PWR	14
#define IXGBE_MAX_DATA_PER_TXD	(1 << IXGBE_MAX_TXD_PWR)

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S) DIV_ROUND_UP((S), IXGBE_MAX_DATA_PER_TXD)
#define DESC_NEEDED (MAX_SKB_FRAGS + 4)

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct ixgbevf_tx_buffer {
	union ixgbe_adv_tx_desc *next_to_watch;
	unsigned long time_stamp;
	struct sk_buff *skb;
	unsigned int bytecount;
	unsigned short gso_segs;
	__be16 protocol;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct ixgbevf_rx_buffer {
	dma_addr_t dma;
	struct page *page;
	unsigned int page_offset;
};

struct ixgbevf_stats {
	u64 packets;
	u64 bytes;
#ifdef BP_EXTENDED_STATS
	u64 yields;
	u64 misses;
	u64 cleaned;
#endif
};

struct ixgbevf_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
};

struct ixgbevf_rx_queue_stats {
	u64 non_eop_descs;
	u64 alloc_rx_page_failed;
	u64 alloc_rx_buff_failed;
	u64 csum_err;
};

enum ixgbevf_ring_state_t {
	__IXGBEVF_TX_DETECT_HANG,
	__IXGBEVF_HANG_CHECK_ARMED,
	__IXGBEVF_RX_CSUM_UDP_ZERO_ERR,
};

#define check_for_tx_hang(ring) \
	test_bit(__IXGBEVF_TX_DETECT_HANG, &(ring)->state)
#define set_check_for_tx_hang(ring) \
	set_bit(__IXGBEVF_TX_DETECT_HANG, &(ring)->state)
#define clear_check_for_tx_hang(ring) \
	clear_bit(__IXGBEVF_TX_DETECT_HANG, &(ring)->state)


/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBEVF_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define MAX_RX_QUEUES IXGBE_VF_MAX_RX_QUEUES
#define MAX_TX_QUEUES IXGBE_VF_MAX_TX_QUEUES

#ifdef DEV_HPCAP
#define IXGBEVF_DEFAULT_TXD   4096
#define IXGBEVF_DEFAULT_RXD   4096
#else /* DEV_HPCAP */
#define IXGBEVF_DEFAULT_TXD   1024
#define IXGBEVF_DEFAULT_RXD   512
#endif
#define IXGBEVF_MAX_TXD       4096
#define IXGBEVF_MIN_TXD       64
#define IXGBEVF_MAX_RXD       4096
#define IXGBEVF_MIN_RXD       64

#ifdef DEV_HPCAP
#define IXGBEVF_SUBWINDOW_BITS    10
#define IXGBEVF_SUBWINDOW_SIZE    (1 << IXGBEVF_SUBWINDOW_BITS)
#define IXGBEVF_SUBWINDOW_MASK    (IXGBEVF_SUBWINDOW_SIZE - 1)
#define IXGBEVF_MAX_SUBWINDOWS    (IXGBEVF_MAX_RXD / IXGBEVF_SUBWINDOW_SIZE)
#endif /* DEV_HPCAP */

/* Supported Rx Buffer Sizes */
#define IXGBEVF_RXBUFFER_256   256    /* Used for packet split */
#define IXGBEVF_RXBUFFER_2048  2048

#define IXGBEVF_RX_HDR_SIZE IXGBEVF_RXBUFFER_256
#define IXGBEVF_RX_BUFSZ    IXGBEVF_RXBUFFER_2048



struct ixgbevf_ring {
	struct ixgbevf_ring *next;
	struct ixgbevf_q_vector *q_vector; /* backpointer to host q_vector */
	struct net_device *netdev;	/* netdev ring belongs to */
	struct device *dev;		/* device for DMA mapping */
	void *desc;			/* descriptor ring memory */
	union {
		struct ixgbevf_tx_buffer *tx_buffer_info;
		struct ixgbevf_rx_buffer *rx_buffer_info;
	};
	unsigned long state;
#ifndef NO_SURPRISE_REMOVE_SUPPORT
	u8 __iomem **adapter_present;
#endif /* NO_SURPRISE_REMOVE_SUPPORT */
	u8 __iomem *tail;
	dma_addr_t dma;			/* phys. address of descriptor ring */
	unsigned int size;		/* length in bytes */

#ifdef DEV_HPCAP
	struct ixgbevf_adapter *adapter;
	struct hpcap_buf *bufp;
	/* [queued, next_to_clean): packets waiting to be pulled */
	u16 queued; /* only used for RX */

	unsigned int total_bytes;
	unsigned int total_packets;
#ifdef REMOVE_DUPS
	u64 total_dups;
#endif

	u8 *window[IXGBEVF_MAX_SUBWINDOWS];
	dma_addr_t dma_window[IXGBEVF_MAX_SUBWINDOWS];
	unsigned int window_size;

	spinlock_t lock;
	wait_queue_head_t *wq;
#endif /* DEV_HPCAP */

	u16 count;			/* amount of descriptors */

	u8 queue_index;		/* needed for multiqueue queue management */
	u8 reg_idx;		/* holds the special value that gets
				 * the hardware register offset
				 * associated with this ring, which is
				 * different for DCB and RSS modes
				 */
	struct sk_buff *skb;
	u16 next_to_use;
	u16 next_to_clean;
	u16 next_to_alloc;

	struct ixgbevf_stats stats;
#ifdef HAVE_NDO_GET_STATS64
	struct u64_stats_sync	syncp;
#endif
	union {
		struct ixgbevf_tx_queue_stats tx_stats;
		struct ixgbevf_rx_queue_stats rx_stats;
	};
} ____cacheline_internodealigned_in_smp;




#define MAXIMUM_ETHERNET_VLAN_SIZE (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)

#define IXGBE_TX_FLAGS_CSUM		(u32)(1)
#define IXGBE_TX_FLAGS_VLAN		(u32)(1 << 1)
#define IXGBE_TX_FLAGS_TSO		(u32)(1 << 2)
#define IXGBE_TX_FLAGS_IPV4		(u32)(1 << 3)
#define IXGBE_TX_FLAGS_VLAN_MASK	0xffff0000
#define IXGBE_TX_FLAGS_VLAN_PRIO_MASK	0x0000e000
#define IXGBE_TX_FLAGS_VLAN_SHIFT	16

struct ixgbevf_ring_container {
	struct ixgbevf_ring *ring;	/* pointer to linked list of rings */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u8 count;			/* total number of rings in vector */
	u8 itr;				/* current ITR setting for ring */
};

/* iterator for handling rings in ring container */
#define ixgbevf_for_each_ring(pos, head) \
	for (pos = (head).ring; pos != NULL; pos = pos->next)

/* MAX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ixgbevf_q_vector {
	struct ixgbevf_adapter *adapter;
	u16 v_idx;		/* index of q_vector within array, also used for
				 * finding the bit in EICR and friends that
				 * represents the vector for this ring */
	u16 itr;		/* Interrupt throttle rate written to EITR */
	struct napi_struct napi;
#ifndef HAVE_NETDEV_NAPI_LIST
	struct net_device poll_dev;
#endif
	struct ixgbevf_ring_container rx, tx;
	struct rcu_head rcu;    /* to avoid race with update stats on free */
	char name[IFNAMSIZ + 9];
	bool netpoll_rx;

#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned int state;
#define IXGBEVF_QV_STATE_IDLE		0
#define IXGBEVF_QV_STATE_NAPI		1    /* NAPI owns this QV */
#define IXGBEVF_QV_STATE_POLL		2    /* poll owns this QV */
#define IXGBEVF_QV_STATE_DISABLED	4    /* QV is disabled */
#define IXGBEVF_QV_OWNED (IXGBEVF_QV_STATE_NAPI | IXGBEVF_QV_STATE_POLL)
#define IXGBEVF_QV_LOCKED (IXGBEVF_QV_OWNED | IXGBEVF_QV_STATE_DISABLED)
#define IXGBEVF_QV_STATE_NAPI_YIELD	8    /* NAPI yielded this QV */
#define IXGBEVF_QV_STATE_POLL_YIELD	16   /* poll yielded this QV */
#define IXGBEVF_QV_YIELD (IXGBEVF_QV_STATE_NAPI_YIELD | IXGBEVF_QV_STATE_POLL_YIELD)
#define IXGBEVF_QV_USER_PEND (IXGBEVF_QV_STATE_POLL | IXGBEVF_QV_STATE_POLL_YIELD)
	spinlock_t lock;
#endif /* CONFIG_NET_RX_BUSY_POLL */

	/* for dynamic allocation of rings associated with this q_vector */
	struct ixgbevf_ring ring[0] ____cacheline_internodealigned_in_smp;
};
#ifdef CONFIG_NET_RX_BUSY_POLL
static inline void ixgbevf_qv_init_lock(struct ixgbevf_q_vector *q_vector)
{

	spin_lock_init(&q_vector->lock);
	q_vector->state = IXGBEVF_QV_STATE_IDLE;
}

/* called from the device poll routine to get ownership of a q_vector */
static inline bool ixgbevf_qv_lock_napi(struct ixgbevf_q_vector *q_vector)
{
	int rc = true;
	spin_lock_bh(&q_vector->lock);

	if (q_vector->state & IXGBEVF_QV_LOCKED) {
		WARN_ON(q_vector->state & IXGBEVF_QV_STATE_NAPI);
		q_vector->state |= IXGBEVF_QV_STATE_NAPI_YIELD;
		rc = false;
#ifdef BP_EXTENDED_STATS
		q_vector->tx.ring->stats.yields++;
#endif
	} else {
		/* we don't care if someone yielded */
		q_vector->state = IXGBEVF_QV_STATE_NAPI;
	}

	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* returns true is someone tried to get the qv while napi had it */
static inline bool ixgbevf_qv_unlock_napi(struct ixgbevf_q_vector *q_vector)
{
	int rc = false;
	spin_lock_bh(&q_vector->lock);
	WARN_ON(q_vector->state & (IXGBEVF_QV_STATE_POLL |
	                           IXGBEVF_QV_STATE_NAPI_YIELD));

	if (q_vector->state & IXGBEVF_QV_STATE_POLL_YIELD)
		rc = true;

	/* reset state to idle, unless QV is disabled */
	q_vector->state &= IXGBEVF_QV_STATE_DISABLED;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* called from ixgbevf_low_latency_poll() */
static inline bool ixgbevf_qv_lock_poll(struct ixgbevf_q_vector *q_vector)
{
	int rc = true;
	spin_lock_bh(&q_vector->lock);

	if ((q_vector->state & IXGBEVF_QV_LOCKED)) {
		q_vector->state |= IXGBEVF_QV_STATE_POLL_YIELD;
		rc = false;
#ifdef BP_EXTENDED_STATS
		q_vector->rx.ring->stats.yields++;
#endif
	} else {
		/* preserve yield marks */
		q_vector->state |= IXGBEVF_QV_STATE_POLL;
	}

	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* returns true if someone tried to get the qv while it was locked */
static inline bool ixgbevf_qv_unlock_poll(struct ixgbevf_q_vector *q_vector)
{
	int rc = false;
	spin_lock_bh(&q_vector->lock);
	WARN_ON(q_vector->state & (IXGBEVF_QV_STATE_NAPI));

	if (q_vector->state & IXGBEVF_QV_STATE_POLL_YIELD)
		rc = true;

	/* reset state to idle, unless QV is disabled */
	q_vector->state &= IXGBEVF_QV_STATE_DISABLED;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* true if a socket is polling, even if it did not get the lock */
static inline bool ixgbevf_qv_busy_polling(struct ixgbevf_q_vector *q_vector)
{
	WARN_ON(!(q_vector->state & IXGBEVF_QV_OWNED));
	return q_vector->state & IXGBEVF_QV_USER_PEND;
}

/* false if QV is currently owned */
static inline bool ixgbevf_qv_disable(struct ixgbevf_q_vector *q_vector)
{
	int rc = true;
	spin_lock_bh(&q_vector->lock);

	if (q_vector->state & IXGBEVF_QV_OWNED)
		rc = false;

	q_vector->state |= IXGBEVF_QV_STATE_DISABLED;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

#endif /* CONFIG_NET_RX_BUSY_POLL */

/*
 * microsecond values for various ITR rates shifted by 2 to fit itr register
 * with the first 3 bits reserved 0
 */
#define IXGBE_MIN_RSC_ITR	24
#define IXGBE_100K_ITR		40
#define IXGBE_20K_ITR		200
#define IXGBE_10K_ITR		400
#define IXGBE_8K_ITR		500

/* ixgbevf_test_staterr - tests bits in Rx descriptor status and error fields */
static inline __le32 ixgbevf_test_staterr(union ixgbe_adv_rx_desc *rx_desc,
        const u32 stat_err_bits)
{
	return rx_desc->wb.upper.status_error & cpu_to_le32(stat_err_bits);
}

static inline u16 ixgbevf_desc_unused(struct ixgbevf_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

#define IXGBEVF_RX_DESC(R, i)	    \
	(&(((union ixgbe_adv_rx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_DESC(R, i)	    \
	(&(((union ixgbe_adv_tx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_CTXTDESC(R, i)	    \
	(&(((struct ixgbe_adv_tx_context_desc *)((R)->desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE        16128

#define OTHER_VECTOR 1
#define NON_Q_VECTORS (OTHER_VECTOR)

#define MAX_Q_VECTORS 2

#define MIN_MSIX_Q_VECTORS 1
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

/* board specific private data structure */
struct ixgbevf_adapter {
#if defined(NETIF_F_HW_VLAN_TX) || defined(NETIF_F_HW_VLAN_CTAG_TX)
#ifdef HAVE_VLAN_RX_REGISTER
	struct vlan_group *vlgrp;
#else
	/* this field must be first, see ixgbevf_process_skb_fields */
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
#endif
#endif /* NETIF_F_HW_VLAN_TX || NETIF_F_HW_VLAN_CTAG_TX */

	struct net_device *netdev;
	struct pci_dev *pdev;

#ifdef DEV_HPCAP
	int core;//new
	int numa_node;
	int work_mode;
	atomic_t dup_mode;
	atomic_t caplen;
	unsigned int bufpages;
	size_t consumers;
	unsigned long long hpcap_client_loss;
	unsigned long long hpcap_client_discard;
#ifdef REMOVE_DUPS
	unsigned long long total_dup_frames;
#endif
	struct hpcap_dev_attrs hpcap_dev_attrs;
#endif /* DEV_HPCAP */

	unsigned long state;

	u32 flags;
#define IXGBE_FLAG_RX_CSUM_ENABLED		(u32)(1)
#define IXGBEVF_FLAG_RESET_REQUESTED		(u32)(1 << 1)

#define IXGBEVF_FLAG_QUEUE_RESET_REQUESTED	(u32)(1 << 3)

	/* Tx hotpath */
	u16 tx_ring_count;
	u16 num_tx_queues;
	u16 tx_itr_setting;

	/* Rx hotpath */
	u16 rx_ring_count;
	u16 num_rx_queues;
	u16 rx_itr_setting;

	/* Rings, Tx first since it is accessed in hotpath */
	struct ixgbevf_ring *tx_ring[MAX_TX_QUEUES]; /* One per active queue */
	struct ixgbevf_ring *rx_ring[MAX_RX_QUEUES]; /* One per active queue */

	/* interrupt vector accounting */
	struct ixgbevf_q_vector *q_vector[MAX_Q_VECTORS];
	int num_q_vectors;
	struct msix_entry *msix_entries;

	/* interrupt masks */
	u32 eims_enable_mask;
	u32 eims_other;

	/* stats */
	u64 tx_busy;
	u64 restart_queue;
	u64 non_eop_descs;
	u64 hw_rx_no_dma_resources;
	u64 hw_csum_rx_error;
	u32 alloc_rx_page_failed;
	u32 alloc_rx_buff_failed;

#ifndef HAVE_NETDEV_STATS_IN_NETDEV
	struct net_device_stats net_stats;
#endif

	u32 tx_timeout_count;

	/* structs defined in ixgbe_vf.h */
	struct ixgbe_hw hw;
	struct ixgbevf_hw_stats stats;

	u32 *config_space;

	u16 bd_number;
	u16 msg_enable;

#ifdef BP_EXTENDED_STATS
	u64 bp_rx_yields;
	u64 bp_rx_cleaned;
	u64 bp_rx_missed;

	u64 bp_tx_yields;
	u64 bp_tx_cleaned;
	u64 bp_tx_missed;
#endif

	u8 __iomem *io_addr;
	u32 link_speed;
	bool link_up;
	bool dev_closed;

	struct timer_list service_timer;
	struct work_struct service_task;

	spinlock_t mbx_lock;
	unsigned long last_reset;
};

struct ixgbevf_info {
	enum ixgbe_mac_type	mac;
	unsigned int		flags;
};

enum ixbgevf_state_t {
	__IXGBEVF_TESTING,
	__IXGBEVF_RESETTING,
	__IXGBEVF_DOWN,
	__IXGBEVF_DISABLED,
	__IXGBEVF_REMOVE,
	__IXGBEVF_SERVICE_SCHED,
	__IXGBEVF_SERVICE_INITED,
};

#ifdef HAVE_VLAN_RX_REGISTER
struct ixgbevf_cb {
	u16 vid;			/* VLAN tag */
};
#define IXGBE_CB(skb) ((struct ixgbevf_cb *)(skb)->cb)
#endif

/* needed by ixgbevf_main.c */
extern void ixgbevf_check_options(struct ixgbevf_adapter *adapter);

/* needed by ixgbevf_ethtool.c */
extern char ixgbevf_driver_name[];
extern const char ixgbevf_driver_version[];

extern void ixgbevf_up(struct ixgbevf_adapter *adapter);
extern void ixgbevf_down(struct ixgbevf_adapter *adapter);
extern void ixgbevf_reinit_locked(struct ixgbevf_adapter *adapter);
extern void ixgbevf_reset(struct ixgbevf_adapter *adapter);
extern void ixgbevf_set_ethtool_ops(struct net_device *netdev);
extern int ixgbevf_setup_rx_resources(struct ixgbevf_ring *);
extern int ixgbevf_setup_tx_resources(struct ixgbevf_ring *);
extern void ixgbevf_free_rx_resources(struct ixgbevf_ring *);
extern void ixgbevf_free_tx_resources(struct ixgbevf_ring *);
extern void ixgbevf_update_stats(struct ixgbevf_adapter *adapter);
extern void ixgbevf_write_eitr(struct ixgbevf_q_vector *);
extern void ixgbevf_reset_interrupt_capability(struct ixgbevf_adapter *adapter);
extern int ixgbevf_init_interrupt_scheme(struct ixgbevf_adapter *adapter);
extern bool ixgbevf_is_ixgbevf(struct pci_dev *pcidev);

#ifdef ETHTOOL_OPS_COMPAT
extern int ethtool_ioctl(struct ifreq *ifr);

#endif
extern void ixgbe_napi_add_all(struct ixgbevf_adapter *adapter);
extern void ixgbe_napi_del_all(struct ixgbevf_adapter *adapter);

static inline u32 __er32(struct ixgbe_hw *hw, unsigned long reg)
{
	return readl(hw->hw_addr + reg);
}

static inline void __ew32(struct ixgbe_hw *hw, unsigned long reg, u32 val)
{
	writel(val, hw->hw_addr + reg);
}
#define er32(reg)	IXGBE_READ_REG(hw, IXGBE_##reg)
#define ew32(reg,val)	IXGBE_WRITE_REG(hw, IXGBE_##reg, (val))
#define e1e_flush()	er32(STATUS)

static inline void ixgbevf_write_tail(struct ixgbevf_ring *ring, u32 value)
{
#ifndef NO_SURPRISE_REMOVE_SUPPORT

	if (unlikely(!*ring->adapter_present))
		return;

#endif /* NO_SURPRISE_REMOVE_SUPPORT */
	writel(value, ring->tail);
}


#ifdef DEV_HPCAP
/*#define IXGBEVF_RX_DESC_ADV(R, i)           \
	( &( ( (union ixgbe_adv_rx_desc *) ( (R).desc) )[i] ) )
#define IXGBEVF_TX_DESC_ADV(R, i)     \
	( &( ( (union ixgbe_adv_tx_desc *) ( (R).desc) )[i] ) )*/

//extern void ixgbevf_irq_disable_queues(struct ixgbevf_adapter *, u64);
//extern void ixgbevf_irq_enable_queues(struct ixgbevf_adapter *, u64);
extern void ixgbevf_release_rx_desc(struct ixgbevf_ring *, u32);
//extern bool ixgbevf_clean_tx_irq(struct ixgbevf_q_vector *, struct ixgbevf_ring *);
#endif /* DEV_HPCAP */


#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0) )
/*
 * Provide __deprecated wrappers for the new interface, avoid flag day changes.
 * We need the ugly external functions to break header recursion hell.
 */
#ifndef smp_mb__before_clear_bit
static inline void __deprecated smp_mb__before_clear_bit(void)
{
	//extern void __smp_mb__before_atomic(void);
	//__smp_mb__before_atomic();
	barrier();
}
#endif

#ifndef smp_mb__after_clear_bit
static inline void __deprecated smp_mb__after_clear_bit(void)
{
	//extern void __smp_mb__after_atomic(void);
	//__smp_mb__after_atomic();
	barrier();
}
#endif
#endif


#endif /* _IXGBEVF_H_ */
