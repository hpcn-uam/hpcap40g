/**
 * @brief Definitions of internal types used by HPCAP.
 *
 * @see hpcap.h for types used externally in other's driver code and userspace.
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_TYPES_H
#define HPCAP_TYPES_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/ktime.h>

#include "hpcap.h"

#include "hpcap_latency.h"

#if defined(HPCAP_IXGBE) || defined(HPCAP_IXGBEN)
#include "ixgbe.h"
#define HPCAP_INTEL
#elif defined(HPCAP_IXGBEVF)
#include "ixgbevf.h"
#define HPCAP_INTEL
#elif defined(HPCAP_MLNX)
#include "mlx4_en.h"
#define HPCAP_40G
#elif defined(HPCAP_I40E)
#include "i40e.h"
#define HPCAP_INTEL
#define HPCAP_40G
#elif defined(HPCAP_I40EVF)
#include "i40evf.h"
#define HPCAP_INTEL
#define HPCAP_40G
#endif

#ifdef HPCAP_INTEL

#ifndef HPCAP_40G
typedef union ixgbe_adv_rx_desc rx_descr_t;
#else
typedef union i40e_rx_desc rx_descr_t;
#endif

// Just some redefinitions, renaming structures that are the
// same but have different names in the ixgbe driver variants.
#define adapter_netdev(a) (a)->netdev

#define ring_get_next_rxd(ring) ((ring)->next_to_clean)
#define ring_size(ring) 		(ring->count)

#ifdef HPCAP_40G
#define rxd_hash(rx_desc) 		le32_to_cpu(rx_desc->wb.lower.hi_dword.rss)
#define rxd_length(rx_desc) 	((le64_to_cpu(rx_desc->wb.qword1.status_error_len) & I40E_RXD_QW1_LENGTH_PBUF_MASK) >> I40E_RXD_QW1_LENGTH_PBUF_SHIFT)
#define rxd_status(rx_desc)		((uint32_t)( \
									(((uint64_t) le64_to_cpu((rx_desc)->wb.qword1.status_error_len)) \
									 	& I40E_RXD_QW1_STATUS_MASK) >> I40E_RXD_QW1_STATUS_SHIFT))
#define rxd_has_data(rx_desc) 	(rxd_status(rx_desc) & BIT(I40E_RX_DESC_STATUS_DD_SHIFT))
#define rxd_has_error(rx_desc) 	(le32_to_cpu((rx_desc)->wb.upper.status_error) & IXGBE_RXDADV_ERR_FRAME_ERR_MASK)
#define rxd_is_jumbo(rx_desc) 	(!(le32_to_cpu((rx_desc)->wb.upper.status_error) & IXGBE_RXD_STAT_EOP))
#else
#define rxd_hash(rx_desc) 		le32_to_cpu(rx_desc->wb.lower.hi_dword.rss)
#define rxd_length(rx_desc) 	le16_to_cpu(rx_desc->wb.upper.length)
#define rxd_has_data(rx_desc) 	(le32_to_cpu((rx_desc)->wb.upper.status_error) & IXGBE_RXD_STAT_DD)
#define rxd_has_error(rx_desc) 	(le32_to_cpu((rx_desc)->wb.upper.status_error) & IXGBE_RXDADV_ERR_FRAME_ERR_MASK)
#define rxd_is_jumbo(rx_desc) 	(!(le32_to_cpu((rx_desc)->wb.upper.status_error) & IXGBE_RXD_STAT_EOP))
#endif

#define ring_has_hw_tstamp(R) (0)

#if defined(HPCAP_IXGBE)

#define ring_get_rxd(R,i) IXGBE_RX_DESC((R), (i))
#define ring_get_buffer(rxd,ring,i) ( (u8 *) ( ring->window[(i) >> IXGBE_SUBWINDOW_BITS] + ((i) & IXGBE_SUBWINDOW_MASK)*MAX_DESCR_SIZE ) )
#define packet_dma(ring,i) ( (u64) ( ring->dma_window[(i) >> IXGBE_SUBWINDOW_BITS] + ((i) & IXGBE_SUBWINDOW_MASK) * MAX_DESCR_SIZE ) )

typedef struct ixgbe_option DRIVER_OPTION;
typedef struct ixgbe_adapter HW_ADAPTER;
typedef struct ixgbe_ring HW_RING;

typedef int rxd_idx_t;

#define DRIVER_VALIDATE_OPTION(a,b) ixgbe_validate_option(a,b)
#define ring_rxd_release(R,i) ixgbe_release_rx_desc((R), (i))

#elif defined(HPCAP_IXGBEVF)

#define ring_get_rxd(R,i) IXGBEVF_RX_DESC((R), (i))
#define ring_get_buffer(rxd,ring,i) ( (u8 *) ( ring->window[i >> IXGBEVF_SUBWINDOW_BITS] + (i & IXGBEVF_SUBWINDOW_MASK)*MAX_DESCR_SIZE ) )
#define packet_dma(ring,i) ( (u64) ( ring->dma_window[i >> IXGBEVF_SUBWINDOW_BITS] + (i & IXGBEVF_SUBWINDOW_MASK) * MAX_DESCR_SIZE ) )

typedef struct ixgbevf_option DRIVER_OPTION;
typedef struct ixgbevf_adapter HW_ADAPTER;
typedef struct ixgbevf_ring HW_RING;

typedef int rxd_idx_t;

#define DRIVER_VALIDATE_OPTION(a,b) ixgbevf_validate_option(a,b)
#define ring_rxd_release(R,i) ixgbevf_release_rx_desc((R), (i))

#elif defined(HPCAP_40G)

#define ring_get_rxd(R,i) I40E_RX_DESC((R), (i))
#define ring_get_buffer(rxd,ring,i) ( (u8 *) ( ring->window[i >> I40E_SUBWINDOW_BITS] + (i & I40E_SUBWINDOW_MASK)*MAX_DESCR_SIZE ) )
#define packet_dma(ring,i) ( (u64) ( ring->dma_window[i >> I40E_SUBWINDOW_BITS] + (i & I40E_SUBWINDOW_MASK) * MAX_DESCR_SIZE ) )

#ifdef HPCAP_I40E
typedef struct i40e_vsi HW_ADAPTER;
#else
typedef struct i40evf_adapter HW_ADAPTER;
#endif

typedef struct i40e_ring HW_RING;

typedef uint16_t rxd_idx_t;

#define ring_rxd_release(R,i) i40e_release_rx_desc((R), (i))

#endif /* HPCAP_IXGBEVF */
#elif defined(HPCAP_MLNX)  /* HPCAP_IXGBEVF || HPCAP_IXGBE */
#define HPCAP_HWTSTAMP

/**
 * This structure unifies the two data structures used
 * in Mellanox receive descriptors: the rx_desc and the cqe
 * with completion information
 */
struct hpcap_mlx4_rx_desc {
	struct mlx4_en_rx_desc* rx_desc;
	struct mlx4_cqe* cqe;
	struct mlx4_en_rx_alloc* frags;
	size_t fcs_del;
	uint32_t cq_mask;
};

typedef struct mlx4_en_priv HW_ADAPTER;
typedef struct mlx4_en_rx_ring HW_RING;
typedef struct hpcap_mlx4_rx_desc rx_descr_t;

#define ring_get_next_rxd(ring) ((ring)->cq->mcq.cons_index)
#define ring_size(ring) 		((ring)->size)
#define rxd_hash(rx_desc) 		be32_to_cpu(rx_desc->cqe->immed_rss_invalid)
#define rxd_length(rx_desc) 	(be32_to_cpu((rx_desc)->cqe->byte_cnt) - (rx_desc)->fcs_del)
#define rxd_is_jumbo(rx_desc) 	0
#define rxd_has_data(rx_desc) 	XNOR( \
	(rx_desc)->cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK, \
	(rx_desc)->cq_mask)

#define ring_has_hw_tstamp(ring) ((ring)->hwtstamp_rx_filter == HWTSTAMP_FILTER_ALL)

void rxd_get_tstamp(rx_descr_t*, struct timespec*, HW_RING*);
u8* ring_get_buffer(rx_descr_t* rxd, HW_RING* ring, size_t i);

#define adapter_netdev(a) (a)->dev

typedef uint32_t rxd_idx_t;

#endif  /* HPCAP_MLNX */


/**
 * Structure representing a client consuming data from the HPCAP
 * buffer.
 *
 * @see hpcap_listeners.h
 */
struct hpcap_listener {
	atomic_t id;		/**< Unique identification for this listener. */
	atomic_t kill;		/**< Used for signaling stop requests. */
	size_t bufferWrOffset; /**< Offset of the last write in the HPCAP buffer. */
	size_t bufferRdOffset; /**< Offset of the last read from the client in the buffer */
	size_t bufsz;		/**< Size of the HPCAP buffer */
	struct file* filp;	/**< Pointer to the associated file structure */
};

#define MAX_FORCE_KILLED_LISTENERS (3 * MAX_LISTENERS)

/**
 * This structure holds all the information for the listeners linked to a buffer.
 *
 * @see hpcap_listeners.h
 */
struct hpcap_buffer_listeners {
	struct hpcap_listener listeners[MAX_LISTENERS]; /**< Array of listeners */
	struct hpcap_listener global;	/**< Global (master) listener pointer */
	spinlock_t lock;		 		/**< Lock for write access over the array */
	atomic_t listeners_count;	 	/**< Number of active listeners. */
	atomic_t already_popped;		/**< Detects whether the listeners already read some frames. Useful to avoid misaligned accesses. */
	int force_killed_listener_ids[MAX_FORCE_KILLED_LISTENERS]; 	/**< An array with the IDs of the listeners that were force destroyed. */
	atomic_t force_killed_listeners; 	/**< Number of listeners that were force killed. No atomics as we suppose prod */
};

struct hpcap_profile {
	uint64_t descriptors;
	uint64_t bytes;
	uint64_t frames;
	uint64_t times_ring_empty;
	uint64_t times_rxd_not_owned;
	uint64_t released_descriptors;
	uint64_t num_releases;
	uint64_t budgets_exhausted;
	uint64_t timeout_jiffies;
	uint64_t sleeps;
	uint64_t strike_duration;
	uint64_t strikes;

	uint64_t _prev_jiffies;
	size_t _prev_rxd;
	size_t _rxd_marked;
	size_t _current_strike;

	size_t samples;

};

/**
 * Structure with the information for each RXQ consumer thread.
 */
struct hpcap_rx_thinfo {
	atomic_t* write_offset;
	atomic_t* read_offset;
	size_t th_index;
	HW_RING* rx_ring;
	struct hpcap_profile prof;
	rxd_idx_t rxd_idx;

#ifdef HPCAP_MEASURE_LATENCY
	struct hpcap_latency_measurements lm;
#endif
};

/**
 * Structure that represents the HPCAP buffer associated to a queue of an
 * adapter.
 */
struct hpcap_buf {
	int adapter; 	/**< Adapter associated to this buffer. */
	int queue;		/**< Queue associated to this buffer. */

	atomic_t opened;	/**< Number of opened handles over this buffer */
	int max_opened;		/**< Maximum count of handles that can open this buffer */
	atomic_t mapped;	/**< Number of handles that have mmap'ed this buffer's memory */
	atomic_t created;	/**< Set to 1 if this buffer is created and initialized. */
	atomic_t last_handle;	/**< An ever-increasing counter to assign correct handle ID's */

	char * bufferCopia;	/**< The buffer holding the received data from the NIC */
	u64 bufSize;		/**< Size of the buffer */

	atomic_t consumer_write_off; /**< Write offset for the consumers (pointer to the first free offset in the buffer) */
	atomic_t consumer_read_off;  /**< Read offset for the consumer (next position to be read by userspace) */

	struct task_struct* consumer_threads[MAX_CONSUMERS_PER_Q]; /**< Pointer to the consumer threads' managers */
	struct hpcap_rx_thinfo consumers_thinfo[MAX_CONSUMERS_PER_Q]; /**< Pointer to the consumer threads' information */
	atomic_t freed_last_rxd[MAX_CONSUMERS_PER_Q]; /**< Atomic markers to check that a consumer has freed its last rx descriptor */
	short can_free[MAX_CONSUMERS_PER_Q]; /**< Marker for whether a consumer can free its descriptors. It can only free them when the descriptors from the previous sector have been already freed */
	size_t descr_per_consumer;			 /**< Number of descriptors for each consumer */
	size_t consumers;					 /**< Number of consumers for the buffer */

	struct hpcap_buffer_listeners lstnr; /**< Structure controlling the listeners for this buffer */

	atomic_t readCount;			/**< TODO: Use spinlocks! */
	atomic_t mmapCount;			/**< TODO: Use spinlocks! */

	char name[100]; 	/**< Name of this buffer */
	struct cdev chard; 	/**< Char device structure */

#ifdef REMOVE_DUPS
	struct hpcap_dup_info ** dupTable;	/**< Table for duplicate checking */
#endif

	struct page** huge_pages;	/**< Pointer to the first page of the hugepage buffer, or NULL if no buffer exists */
	char huge_pages_file[MAX_HUGETLB_FILE_LEN]; /**< File backing the hugepage buffer. */
	int huge_pages_num;			/**< Number of hugepages assigned to the buffer */
	char * old_buffer;			/**< Pointer to the HPCAP buffer present before mapping the hugepages. It will be recovered when unmapping hugepages. */
	u64 old_bufSize;			/**< Size of the old HPCAP buffer */

	atomic_t enabled_filter;
	size_t filter_count;
	uint8_t filter_strings[HPCAP_MAX_FILTERS][HPCAP_MAX_FILTER_STRLEN];
	size_t filter_starts[HPCAP_MAX_FILTERS];
	size_t filter_lengths[HPCAP_MAX_FILTERS];
	short filter_reject_on_match[HPCAP_MAX_FILTERS];

#ifdef HPCAP_PROFILING
	short has_printed_profile_help;
#endif
};

/**
 * Structure that holds the information for a file handle of a HPCAP buffer.
 */
struct hpcap_file_info {
	uint64_t handle_id;			/**< Unique ID of this handle */
	struct hpcap_buf* bufp;		/**< Pointer to the HPCAP buffer of the handle */
};

/** @} */

#endif
