/**
 * @brief Header types for the initialization of HPCAP data structures.
 *
 * TODO: Needs a small refactor, placing the macros probably in hpcap_types.h
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef _HPCAP_IXGBE_H_
#define _HPCAP_IXGBE_H_

#include <linux/cdev.h>

#include "hpcap.h"
#include "hpcap_types.h"

#define RX_MODE_READ 1
#define RX_MODE_MMAP 2

#define distance( primero, segundo, size) ( (primero<=segundo) ? (segundo-primero) : ( (size-primero)+segundo) )
#define used_bytes(plist) ( distance( (plist)->bufferRdOffset, (plist)->bufferWrOffset, (plist)->bufsz ) )
//#define avail_bytes(plist) ( distance( (plist)->bufferWrOffset, (plist)->bufferRdOffset, (plist)->bufsz ) )
#define avail_bytes(plist) ( ( ((plist)->bufsz) - used_bytes(plist) ) - 1 )

#define is_hpcap_mode(mode) (mode == 2)
#define is_hpcap_adapter(adapter) (is_hpcap_mode(adapter->work_mode))

#define hpcap_buffer_of(filp) (((struct hpcap_file_info* )((filp)->private_data))->bufp)
#define hpcap_handleid_of(filp) (((struct hpcap_file_info* )((filp)->private_data))->handle_id)

#include "hpcap_rx.h"
#include "hpcap_listeners.h"
#include "hpcap_types.h"
#include "hpcap_cdev.h"

extern int adapters_found;
extern HW_ADAPTER * adapters[HPCAP_MAX_NIC];

int hpcap_buf_init(struct hpcap_buf *bufp, HW_ADAPTER *adapter, int queue, u64 size, u64 bufoffset);
int hpcap_buf_clear(struct hpcap_buf*);


int get_buf_offset(void *buf);

/**
 * Register the adapters found calling to hpcap_register_chardev
 * on each one of them and checking the page configuration.
 * @return 0 if ok, -1 if error.
 */
int hpcap_register_adapters(void);

/**
 * Run a precheck of the options passed to the driver, to detect if
 * there's something that warrants an abort when initializing the module.
 *
 * This should be called from the module_init functions.
 * @return  0 if everything is OK, negative if not. Currently, can only
 *            return ENOMEM if the pages are not consistent.
 *
 * @note This function is defined in hpcap_params_1.c, basically because it needs to
 * access the option variables, declared static.
 */
int hpcap_precheck_options(void);

/**
 * Fill the bufinfo structure with all the data of the buffer.
 * @param  bufp    HPCAP buffer.
 * @param  bufinfo Buffer information.
 * @return         Always 0 (OK).
 */
void hpcap_get_buffer_info(struct hpcap_buf* bufp, struct hpcap_buffer_info* bufinfo);

/** @} */

#endif
