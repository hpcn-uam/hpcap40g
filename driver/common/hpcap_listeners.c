/**
 * @brief Functions related to the management of listeners, clients
 * that read from the HPCAP buffer.
 */

#include "hpcap_listeners.h"
#include "hpcap_debug.h"
#include "driver_hpcap.h"

#include <linux/spinlock.h>

void hpcap_rst_listener(struct hpcap_listener *list)
{
	atomic_set(&list->id, HPCAP_LISTENER_EMPTY);
	atomic_set(&list->kill, 0);
	list->bufferWrOffset = 0;
	list->bufferRdOffset = 0;
}

void hpcap_update_listener_bufsizes(struct hpcap_buffer_listeners* lstnr, size_t bufsize)
{
#if MAX_LISTENERS > 1
	int i;

	for (i = 0; i < MAX_LISTENERS; i++)
		lstnr->listeners[i].bufsz = bufsize;

#endif
	lstnr->global.bufsz = bufsize;

	printdbg(DBG_LSTNR, "Listeners updated with buffer of size %zu bytes.\n", bufsize);
}

void hpcap_init_listeners(struct hpcap_buffer_listeners* lstnr, size_t bufsize)
{
	int i;

	printdbg(DBG_LSTNR, "Initializing listeners (%d MAX_LISTENERS)\n", MAX_LISTENERS);
	spin_lock_init(&lstnr->lock);
	atomic_set(&lstnr->listeners_count, 0);
	atomic_set(&lstnr->already_popped, 0);
	atomic_set(&lstnr->force_killed_listeners, 0);

	hpcap_rst_listener(&lstnr->global);

	for (i = 0; i < MAX_LISTENERS; i++)
		hpcap_rst_listener(&lstnr->listeners[i]);

	hpcap_update_listener_bufsizes(lstnr, bufsize);
}

void hpcap_push_listener(struct hpcap_buffer_listeners* lstnr, int index, u64 count)
{
	struct hpcap_listener* list = hpcap_get_listener_by_index(lstnr, index);

	if (!list) {
		printdbg(DBG_LSTNR, "Trying to push on a listener that does not exist (index %d)\n", index);
		return;
	}

	if (list->bufsz == 0) {
		BPRINTK(ERR, "Listener with index %d (handle ID %d) has buffer size 0. Aborting push.\n", index, atomic_read(&list->id));
		return;
	}

	if (avail_bytes(list) < count) {
		BPRINTK(ERR, "Erroneous push_listener: wants to push %llu bytes but only %zu bytes available to read. RD: %zu WR: %zu\n",
				count, avail_bytes(list), list->bufferRdOffset, list->bufferWrOffset);
		count = avail_bytes(list) - 1;
	}

	if (index == HPCAP_GLOBAL_LISTENER_IDX || atomic_read(&list->id) != HPCAP_LISTENER_EMPTY)
		list->bufferWrOffset = (list->bufferWrOffset + count) % list->bufsz; //written by producer
}

void hpcap_push_all_listeners(struct hpcap_buffer_listeners* lstnr, u64 count)
{
	int i;

	if (count > 0)
		hpcap_push_listener(lstnr, HPCAP_GLOBAL_LISTENER_IDX, count);

#if MAX_LISTENERS > 1

	for (i = 0; i < MAX_LISTENERS; i++) {
		if (atomic_read(&lstnr->listeners[i].id) != HPCAP_LISTENER_EMPTY)
			hpcap_push_listener(lstnr, i, count);
	}

#endif
}

void hpcap_pop_listener(struct hpcap_listener *list, u64 count)
{
	size_t bufsize = list->bufsz;

	if (used_bytes(list) < count) {
		BPRINTK(ERR, "[POP] Error => RD:%zu WR:%zu used:%zu count:%llu\n", list->bufferRdOffset, list->bufferWrOffset, used_bytes(list), count);
		count = used_bytes(list);
	}

	list->bufferRdOffset = (list->bufferRdOffset + count) % bufsize; // written by consumer
}

int hpcap_pop_global_listener(struct hpcap_buffer_listeners* lstnr)
{
	int i;
	struct hpcap_listener* global = &lstnr->global;
	size_t bufsize = global->bufsz;
	u64 minDist = bufsize + 1;
	int dist;


	for (i = 0; i < MAX_LISTENERS; i++) {
		if ((atomic_read(&lstnr->listeners[i].id) != HPCAP_LISTENER_EMPTY)) {
			dist = distance(global->bufferRdOffset, lstnr->listeners[i].bufferRdOffset, bufsize);

			if (dist < minDist)
				minDist = dist;
		}
	}

	if ((minDist <= bufsize) && (minDist > 0)) {
		if (used_bytes(global) < minDist)
			BPRINTK(ERR, "[POPg] Error => RD:%zu WR:%zu used:%zu count:%llu\n", global->bufferRdOffset, global->bufferWrOffset, used_bytes(global), minDist);

		global->bufferRdOffset = (global->bufferRdOffset + minDist) % bufsize;

		return minDist;
	}

	return 0;
}

int hpcap_add_listener(struct hpcap_buffer_listeners *lstnr, int id)
{
	int i = 0, current_id;
	int ret = -EUSERS;

	spin_lock(&lstnr->lock);

	for (i = 0; i < MAX_LISTENERS; i++) {
		current_id = atomic_read(&lstnr->listeners[i].id);

		/* check if that id has already been registered */
		if (current_id == id) {
			printdbg(DBG_LSTNR, "Listener with id %d already registered\n", id);

			ret = -EALREADY;
			goto out;
		} else if (current_id == 0) {
			lstnr->listeners[i].bufferWrOffset = lstnr->global.bufferWrOffset;

			if (hpcap_listener_count(lstnr) == 0 || atomic_read(&lstnr->already_popped) == 0) {
				// If there are no listeners or they didn't read anything, the global read/write offsets have been reset
				// and the read offset points to the beginnning of a frame.
				lstnr->listeners[i].bufferRdOffset = lstnr->global.bufferRdOffset;
			} else {
				// However, if there are more listeners, the read offset of the global listener
				// may point to the middle of a frame. Set the read offset of this new listener
				// to the last known write offset, which will point to the beginning of a frame.
				lstnr->listeners[i].bufferRdOffset = lstnr->global.bufferWrOffset;
			}

			atomic_set(&lstnr->listeners[i].id, id);
			atomic_inc(&lstnr->listeners_count);

			ret = 0;
			goto out;
		}
	}

out:
	spin_unlock(&lstnr->lock);
	return -EUSERS;
}

struct hpcap_listener * hpcap_get_listener(struct hpcap_buffer_listeners *lstnr, int id)
{
	int i = 0;

	for (i = 0; i < MAX_LISTENERS; i++) {
		if (atomic_read(&lstnr->listeners[i].id) == id) {
			printdbg(DBG_LSTNR, "Listener with id %d found at %d (R: %zu, W: %zu)\n", id, i,
					 lstnr->listeners[i].bufferRdOffset, lstnr->listeners[i].bufferWrOffset);

			return &lstnr->listeners[i];
		}
	}

	BPRINTK(WARNING, "Warn: Listener with id %d not found\n", id);
	return NULL;
}

struct hpcap_listener* hpcap_get_listener_by_index(struct hpcap_buffer_listeners* lstnr, int index)
{
	if (index == HPCAP_GLOBAL_LISTENER_IDX)
		return &lstnr->global;

	if (index < 0 || index >= MAX_LISTENERS)
		return NULL;

	return lstnr->listeners + index;
}

int hpcap_del_listener(struct hpcap_buffer_listeners *lstnr, int id)
{
	struct hpcap_listener *list;
	int ret = -EIDRM;

	spin_lock(&lstnr->lock);

	list = hpcap_get_listener(lstnr, id);

	if (list) {
		BPRINTK(INFO, "Listener with handle %d deleted.\n", id);
		hpcap_rst_listener(list);
		atomic_dec(&lstnr->listeners_count);
		ret = 0;
	}

	spin_unlock(&lstnr->lock);

	return ret;
}

int hpcap_wait_listener(struct hpcap_listener *list, int desired)
{
	int avail = 0;

	avail = used_bytes(list);

	while (!atomic_read(&list->kill) && (avail < desired)) {
		schedule_timeout(ns(100000));  //200us
		avail = used_bytes(list);
	}

	if (atomic_read(&list->kill))
		return -1;

	return avail;
}

#define SLEEP_QUANT 200
u64 hpcap_wait_listener_user(struct hpcap_listener *list, struct hpcap_listener_op* lstop)
{
	u64 avail = 0;
	u64 desired = lstop->expect_bytes;
	u64 timeout_ns = lstop->timeout_ns;
	int num_loops = (timeout_ns / SLEEP_QUANT); //max_loops, if negative -> infinite loop

	avail = used_bytes(list);

	while (!atomic_read(&list->kill) && (avail < desired) && ((num_loops > 0) || (timeout_ns < 0))) {
		schedule_timeout(ns(SLEEP_QUANT));
		avail = used_bytes(list);
		num_loops--;
	}

	if (atomic_read(&list->kill))
		return -1;

	lstop->read_offset = list->bufferRdOffset;
	lstop->available_bytes = avail;

	return avail;
}

struct hpcap_listener* hpcap_get_listener_or_global(struct hpcap_buffer_listeners* lstnr, int id)
{
#if MAX_LISTENERS > 1
	return hpcap_get_listener(lstnr, id);
#else
	return &lstnr->global;
#endif
}

void hpcap_killall_listeners(struct hpcap_buffer_listeners* lstnr)
{
	int i;

#if MAX_LISTENERS > 1

	for (i = 0; i < MAX_LISTENERS; i++) {
		printdbg(DBG_LSTNR, "Killing listener %d\n", i);
		atomic_set(&lstnr->listeners[i].kill, 1);
	}

#endif

	atomic_set(&lstnr->global.kill, 1);
}

void hpcap_print_listener_status(struct hpcap_listener* list)
{
	printdbg(DBG_LSTNR, "Listener ID %d. Offset: R = %zu, W = %zu. Bufsz = %zu (%zu used, %zu available)\n",
			 atomic_read(&list->id), list->bufferRdOffset, list->bufferWrOffset, list->bufsz, used_bytes(list), avail_bytes(list));
}

// Only to be called when no other listeners exist. TODO: Ensure this condition.
void hpcap_global_listener_reset_offset(struct hpcap_buffer_listeners* lstnr)
{
	lstnr->global.bufferWrOffset = 0;
	lstnr->global.bufferRdOffset = 0;
	atomic_set(&lstnr->already_popped, 0);
}

int hpcap_kill_listener(struct hpcap_buffer_listeners* lstnr, int id)
{
	size_t force_killed_listeners;
	struct hpcap_listener* l = hpcap_get_listener(lstnr, id);

	if (l == NULL)
		return -EIDRM;

	atomic_set(&l->kill, 1);
	schedule_timeout(ns(SLEEP_QUANT * 10)); // Allow time for any locked thread to get out.

	filp_close(l->filp, NULL);

	// No synchronization for the increment as we suppose that no one is going to do
	// two simultaneous calls to this function.
	force_killed_listeners = atomic_read(&lstnr->force_killed_listeners);

	if (force_killed_listeners < MAX_FORCE_KILLED_LISTENERS) {
		lstnr->force_killed_listener_ids[force_killed_listeners] = id;
		atomic_set(&lstnr->force_killed_listeners, force_killed_listeners + 1);
	}

	schedule_timeout(ns(SLEEP_QUANT * 10)); // Again, leave some time for threads to get out...
	hpcap_del_listener(lstnr, id);

	BPRINTK(WARNING, "Successfully killed listener with id %d\n", id);

	return 0;
}

int hpcap_is_listener_force_killed(struct hpcap_buffer_listeners* lstnr, int id)
{
	size_t i, force_killed = atomic_read(&lstnr->force_killed_listeners);

	for (i = 0; i < force_killed; i++) {
		if (id == lstnr->force_killed_listener_ids[i])
			return 1;
	}

	return 0;
}
