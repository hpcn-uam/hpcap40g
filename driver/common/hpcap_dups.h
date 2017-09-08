/**
 * @brief Code for checking duplicate frames in the RX function
 *
 * Mostly unused.
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_DUPS_H
#define HPCAP_DUPS_H

#ifdef REMOVE_DUPS

#include "hpcap.h"
#include "hpcap_rx.h"

/**
 * Checks for a duplicate.
 * @param  fd 		Frame descriptor of the received packet.
 * @param  tv       Reception timestamp.
 * @param  duptable Table with the duplicate information.
 * @return          1 if the packet is a duplicate, 0 if not.
 */
int hpcap_check_duplicate(struct frame_descriptor* fd, struct timespec* tv, struct hpcap_dup_info ** duptable);

/**
 * Allocate, if the compiler option is active, the duptable for the given adapter.
 * @param  adapter Hardware adapter.
 * @param  bufp    HPCAP buffer.
 * @return         0 if OK, -1 if not.
 */
int hpcap_allocate_duptable(HW_ADAPTER* adapter, struct hpcap_buf* bufp);

/** @} */

#endif
#endif
