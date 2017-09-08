/**
 * @brief Functions related to the management of listeners, clients
 * that read from the HPCAP buffer.
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_LISTENERS_H
#define HPCAP_LISTENERS_H

#include <linux/types.h>
#include "hpcap_types.h"

/** The index that should be used when referring to the global listener. */
#define HPCAP_GLOBAL_LISTENER_IDX -1

/** ID that denotes that there is not listener in the structure. */
#define HPCAP_LISTENER_EMPTY 0

/**
 * Push the listener to notify it has new data available.
 * @param ls   	  Listener structure
 * @param index   Index of the listener to push, or HPCAP_GLOBAL_LISTENER_IDX if it's the global listener.
 * @param count   Number of bytes of the new data available.
 */
void hpcap_push_listener(struct hpcap_buffer_listeners* lstnr, int index, u64 count);

/**
 * Push new data to all the listeners.
 * @param lstnr Listener structure
 * @param count New data count.
 */
void hpcap_push_all_listeners(struct hpcap_buffer_listeners* lstnr, u64 count);

/**
 * Acknowledge that the given listener has read _count_ bytes
 * and update the pointers accordingly.
 * @param list  Listener.
 * @param count Bytes read.
 */
void hpcap_pop_listener(struct hpcap_listener *list, u64 count);

/**
 * Update the global listener, moving its read offset to the first
 * read offset of all the listeners.
 *
 * That is, if listener 2 has only read the data to the position 8
 * but listener 3 has advanced more, until the position 16, the read offset
 * of the global listener will be set to 8.
 * @param  lstnr     Listeners structure.
 * @return 			 Pending buffer of the slowest listener.
 */
int hpcap_pop_global_listener(struct hpcap_buffer_listeners* lstnr);

/**
 * Reset the listener to initial parameters.
 * @param list Listener.
 */
void hpcap_rst_listener(struct hpcap_listener *list);

/**
 * Preinitialize the listeners to default parameters.
 * @param bufp HPCAP buffer.
 */
void hpcap_init_listeners(struct hpcap_buffer_listeners* lstnr, size_t bufsize);

/**
 * Update the stored buffer size in all the listeners according
 * to the current buffer size of the hpcap_buf structure.
 * @param lstnr 	Listener structure
 * @param bufsize 	New buffer size.
 */
void hpcap_update_listener_bufsizes(struct hpcap_buffer_listeners* lstnr, size_t bufsize);

/**
 * Add a listener to the list.
 * @param  lstnr Listeners structure
 * @param  id    ID of the file handle.
 * @return       0 if everything OK. -EALREADY if the ID is already
 *                	registered, -EUSERS if there are no more slots.
 */
int hpcap_add_listener(struct hpcap_buffer_listeners *lstnr, int id);

/**
 * Retrieve the listener with the given ID.
 * @param  list Listener structure
 * @param  id   ID
 * @return      Listener or NULL if it wasn't found.
 */
struct hpcap_listener * hpcap_get_listener(struct hpcap_buffer_listeners *lstnr, int id);

/**
 * Get the listener with the given index. The index can be HPCAP_GLOBAL_LISTENER_IDX
 * to denote the global index.
 * @param  lstnr Listener structure.
 * @param  index Index to retrieve.
 * @return       Listener or NULL if the index is invalid.s
 */
struct hpcap_listener* hpcap_get_listener_by_index(struct hpcap_buffer_listeners* lstnr, int index);

/**
 * Retrieves the listener with the given id or, if MAX_LISTENER = 1,
 * then returns the global listener.
 * @param  lstnr Listeners structure.
 * @return       Pointer to the listener.
 */
struct hpcap_listener* hpcap_get_listener_or_global(struct hpcap_buffer_listeners* lstnr, int id);

/**
 * Unregister a listener with the given ID:
 * @param  list Listener.
 * @param  id   ID.
 * @return      0 if OK, -EIDRM if the id wasn't registered.
 */
int hpcap_del_listener(struct hpcap_buffer_listeners* lstnr, int id);

/**
 * Block until the given listener has enough bytes available to read.
 * @param  list    Listener.
 * @param  desired Number of bytes that should be available before returning.
 * @return         Number of bytes available or -1 if the listener was killed.
 */
int hpcap_wait_listener(struct hpcap_listener *list, int desired);

/**
 * Block until the given listener has enough bytes available to read, or until
 * a timeout is reached.
 * @param  list  Listener.
 * @param  lstop Structure with the expected bytes and timeout in ns.
 * @return       Available bytes or -1 if the listener was killed.
 */
u64 hpcap_wait_listener_user(struct hpcap_listener *list, struct hpcap_listener_op* lstop);

/**
 * Signal to all the listeners that they must stop.
 * @param lstnr Listeners.
 */
void hpcap_killall_listeners(struct hpcap_buffer_listeners* lstnr);

/**
 * Get the number of listeners. Use this and not direct access to the structure
 * to ensure safe concurrent processes.
 * @param  lstnr Listener structure.
 * @return       Count of listeners.
 */
#define hpcap_listener_count(lstnr) (atomic_read(&(lstnr)->listeners_count))

/**
 * Dumps to the kernel log the status of the given listener.
 *
 * Useful mainly for debugging.
 * @param list Listener
 */
void hpcap_print_listener_status(struct hpcap_listener* list);

void hpcap_global_listener_reset_offset(struct hpcap_buffer_listeners* lstnr);

/** @} */

#endif
