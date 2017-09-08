/**
 * @brief Definitions of debugging macros.
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef IXGBE_COMMON_H
#define IXGBE_COMMON_H

#include <linux/kernel.h>
#include <linux/netdevice.h>

#include "hpcap_types.h"


#if defined(IXGBE)
#define PFX "ixgbe: "
#elif defined(IXGBEVF)
#define PFX "ixgbevf: "
#elif defined(HPCAP_MLNX)
#define PFX "mlnx: "
#else
#define PFX
#endif

/* Some definitions to control what gets out in debugging statements */
#define DBG_DRV 	1	// For general, unspecific debugging
#define DBG_IOCTL 	2	// ioctl calls. Useful to deactivate them when there's a lot of output.
#define DBG_MEM 	4	// Memory related messages.
#define DBG_LSTNR 	8	// Listener register/unregister operations.
#define DBG_RX		16	// Traffic reception operations.
#define DBG_NET		32	// Network operations (ifup/down, ethtool calls...)
#define DBG_TX		64 	// Traffic transmission operations (mostly to know if there's someone trying to send frames through HPCAP interfaces)
#define DBG_RXEXTRA 128 // For extra RX debugging operations. Warning: This is lots, lots of output.
#define DBG_DUPS 	256	// Duplicate detection
#define DBG_PADDING 512 // Padding control

#define DEBUG_FACILITIES (DBG_DRV | DBG_MEM | DBG_NET | DBG_PADDING)
#define DEBUG_FACILITY_ENABLED(t) ((t) & DEBUG_FACILITIES)

#ifdef DEBUG

/**
 * Uncomment to enable slowdowns for debugging.
 *
 * This increases the sleep time for the RX function so the debug log output
 * is readable.
 * Obviously, it should only be used for small rates, ~1pps, if you want something useful.
 */
// #define DEBUG_SLOWDOWN

#define printdbg(type, fmt, args...) do { \
	if (DEBUG_FACILITY_ENABLED(type)) { \
		printk(KERN_INFO PFX "(D) %s: " fmt , __FUNCTION__ , ## args); \
	} } while(0)

#define bufp_dbg(type, fmt, args...) do { \
	if (DEBUG_FACILITY_ENABLED(type)) { \
		printk(KERN_INFO PFX "(D) hpcap%dq%d: %s: " fmt , \
			bufp->adapter, bufp->queue, __FUNCTION__ , ## args); \
	} } while(0)

#define adapter_dbg(type, fmt, args...) do { \
	if (DEBUG_FACILITY_ENABLED(type)) { \
		printk(KERN_INFO PFX "(D) %s: %s: " fmt, adapter_netdev(adapter)->name, \
			__FUNCTION__ , ## args); \
	} } while(0)

#else
#define printdbg(type, fmt, args...) do {} while (0)
#define adapter_dbg(type, fmt, args...) do {} while (0)
#define bufp_dbg(type, fmt, args...) do {} while (0)
#endif

/**
 * Basic kernel printing with the IXGBE prefixes.
 */
#define BPRINTK(klevel, fmt, args...) \
  printk(KERN_##klevel PFX "%s: " fmt, __FUNCTION__ , ## args)

/**
 * Kernel printing that includes adapter name and prints only if
 * the messaging is enabled in the interface.
 *
 * TODO: Readd the previous condition of ((void)((NETIF_MSG_##nlevel & adapter->msg_enable) &&
 * that was removed for i40e.
 */
#define DPRINTK(nlevel, klevel, fmt, args...) \
	printk(KERN_##klevel PFX "%s: %s: " fmt, adapter_netdev(adapter)->name, \
		__FUNCTION__ , ## args)

/**
 * Kernel printing that includes adapter and queue ID.
 */
#define HPRINTK(klevel, fmt, args...) printk(KERN_##klevel PFX "hpcap%dq%d: %s: " fmt , \
	bufp->adapter, bufp->queue, __FUNCTION__ , ## args)

/** @} */

#endif
