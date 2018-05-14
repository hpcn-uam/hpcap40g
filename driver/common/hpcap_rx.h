/**
 * @brief Functions related to the reception of new data
 * and the corresponding poll management.
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_RX_H
#define HPCAP_RX_H

#include "driver_hpcap.h"
#include "hpcap_types.h"


/**
 * Descriptor for a normal or jumbo frame.
 */
struct  __attribute__((__packed__)) frame_descriptor {
	int           parts;                      /**< Total number of valid parts. 1 represents a single frame, more a jumbo */
	u64           size;                       /**< Size of the packet (include all fragments) */
	u8*           pointer[MAX_DESCRIPTORS];   /**< Pointer to every part of the frame */
	rx_descr_t*   rx_desc[MAX_DESCRIPTORS];   /**< Pointer to every descriptor */
#ifdef HPCAP_MLNX
	rx_descr_t 	  _rxd[MAX_DESCRIPTORS];
#endif
	uint32_t 	  rss_hash;					  /**< RSS hash provided by hardware, used for duplicate detection. */
};

/**
 * Move descriptors filled with data from the NIC RX ring
 * to the HPCAP buffer.
 *
 * @param  rx_ring   IXGBE ring.
 * @param  limit     Max bytes that can be copied to the buffer.
 * @param  pkt_buf   Destination buffer. If NULL, no frames are copied, they're just advanced in the NIC.
 * @param  thi       Thread information.
 * @return           Bytes written to the buffer.
 */
uint64_t hpcap_rx(HW_RING *rx_ring, size_t limit, uint8_t *pkt_buf, struct hpcap_rx_thinfo* thi);

/**
 * Entry point for the polling thread.
 *
 * @param  arg HPCAP RX information (type struct hpcap_rx_thinfo)
 * @return     Always 0
 */
int hpcap_poll(void *arg);

/**
 * Stops the polling threads associated to the given adapter.
 * @param  adapter Hardware adapter.
 * @return         Always 0.
 */
int hpcap_stop_poll_threads(HW_ADAPTER *adapter);

/**
 * Launches the polling threads for the given adapter.
 * @param  adapter Adapter.
 * @return         0 if OK, -1 if error.
 */
int hpcap_launch_poll_threads(HW_ADAPTER *adapter);

rx_descr_t* rxd_get(HW_RING* ring, size_t idx);

/**
 * Resets the read/write offsets of the HPCAP buffer.
 * @param bufp HPCAP handle.
 */
void hpcap_reset_buffer_offsets(struct hpcap_buf* bufp);

/** @} */

#endif
