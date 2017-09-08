/**
 * @brief Code for checking duplicate frames in the RX function
 */

#include "hpcap_types.h"
#include "hpcap_dups.h"

#ifdef REMOVE_DUPS
int hpcap_check_duplicate(struct frame_descriptor* fd, struct timespec* tv, struct hpcap_dup_info ** duptable)
{
	u32 pos = fd->rss_hash % DUP_WINDOW_SIZE;
	struct hpcap_dup_info *p = NULL;
	u64 tstamp = (tv->tv_sec * 1000ul * 1000ul * 1000ul) + tv->tv_nsec;
	int ret = 0, i = 0;
	u16 minim = min(fd->size, (u16)DUP_CHECK_LEN);
	u64 dif = 0, dif2 = 0;
	u64 k = 0;

	for (i = 0; i < DUP_WINDOW_LEVELS; i++) {
		p = &((duptable[i])[pos]);

		if (p->tstamp != 0) {
			dif2 = tstamp - p->tstamp;

			if (dif2 <= DUP_TIME_WINDOW) {
				if ((fd->size == p->len) && (memcmp(p->data, fd->pointer[0], minim) == 0))
					ret = 1;
			}

			if (dif2 > dif) {
				dif = dif2;
				k = i;
			}
		} else {
			dif = tstamp;
			k = i;
		}
	}

	(duptable[k])[pos].tstamp = tstamp;
	(duptable[k])[pos].len = fd->size;
	memcpy((duptable[k])[pos].data, fd->pointer[0], minim);

	return ret;
}
#endif

int hpcap_allocate_duptable(HW_ADAPTER* adapter, struct hpcap_buf* bufp)
{
#ifdef REMOVE_DUPS
	int i;
	struct hpcap_dup_info* aux;

	if (adapter->dup_mode == 0)
		bufp->dupTable = NULL;
	else {
		int i = 0;
		struct hpcap_dup_info *aux = NULL;

		aux = (struct hpcap_dup_info *) kzalloc_node(sizeof(struct hpcap_dup_info) * DUP_WINDOW_SIZE * DUP_WINDOW_LEVELS, GFP_KERNEL, adapter->numa_node);
		bufp->dupTable = (struct hpcap_dup_info **) kzalloc_node(sizeof(struct hpcap_dup_info *)*DUP_WINDOW_LEVELS, GFP_KERNEL, adapter->numa_node);

		if (!aux || !(bufp->dupTable)) {
			HPRINTK(ERR, "Error when allocating dupTable");

			if (aux)
				kfree(aux);

			if (bufp->dupTable)
				kfree(bufp->dupTable);

#ifdef DO_BUF_ALLOC

			if (!has_hugepages(bufp))
				kfree(bufp->bufferCopia);

#endif
			return -1;
		} else
			bufp_dbg(DBG_DUPS, "Success allocating %lu Bytes for dup buffer\n", sizeof(struct hpcap_dup_info)*DUP_WINDOW_SIZE * DUP_WINDOW_LEVELS);

		for (i = 0; i < DUP_WINDOW_LEVELS; i++)
			bufp->dupTable[i] = &aux[i * DUP_WINDOW_SIZE];
	}

#endif /* REMOVE_DUPS */
	return 0;
}
