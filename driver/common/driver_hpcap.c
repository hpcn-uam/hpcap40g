/**
 * @brief Code for initialization of the HPCAP buffer structures.
 */


#include "hpcap_debug.h"
#include "driver_hpcap.h"
#include "hpcap_hugepages.h"
#include "hpcap_rx.h"
#include "hpcap_dups.h"
#include "hpcap_vma.h"
#include "hpcap_sysfs.h"

/* Las siguientes dos variables se rellenan en ixgbe[vf]_probe() */
int adapters_found = 0;
HW_ADAPTER * adapters[HPCAP_MAX_NIC];

int hpcap_buf_clear(struct hpcap_buf *bufp)
{
	if ((atomic_read(&bufp->created) == 1) || (atomic_read(&bufp->mapped) == 1) || (atomic_read(&bufp->opened) != 0))
		printk("[HPCAP] Error: trying to unregister cdev in use (if%d,q%d)  (created=%d, mapped=%d, opened=%d)\n", bufp->adapter, bufp->queue, atomic_read(&bufp->created), atomic_read(&bufp->mapped), atomic_read(&bufp->opened));

#ifdef DO_BUF_ALLOC

	if (bufp->bufferCopia && !has_hugepages(bufp))
		kfree(bufp->bufferCopia);

#endif
	bufp->bufferCopia = NULL;

#ifdef REMOVE_DUPS

	if (bufp->dupTable) {
		kfree(bufp->dupTable[0]);
		kfree(bufp->dupTable);
		bufp->dupTable = NULL;
	}

#endif


	return 0;
}

#ifndef DO_BUF_ALLOC
char auxBufs[PAGE_SIZE + HPCAP_BUF_SIZE]; //align to 4KB page size
#endif

int hpcap_buf_init(struct hpcap_buf *bufp, HW_ADAPTER *adapter, int queue, u64 size, u64 bufoffset)
{
	u64 offset = PAGE_SIZE - get_buf_offset(auxBufs);

	atomic_set(&bufp->readCount, 0);
	atomic_set(&bufp->mmapCount, 0);
	memset(bufp->consumer_threads, 0, sizeof(struct task_struct*) * MAX_CONSUMERS_PER_Q);
	bufp->adapter = adapter->bd_number;
	bufp->queue = queue;
	atomic_set(&bufp->created, 0);
	atomic_set(&bufp->mapped, 0);
	atomic_set(&bufp->opened, 0);
	atomic_set(&bufp->last_handle, 0);
	atomic_set(&bufp->enabled_filter, 0);
	bufp->max_opened = MAX_LISTENERS + 1;
	sprintf(bufp->name, "hpcapPoll%dq%d", adapter->bd_number, queue);
	bufp->huge_pages = NULL;
	bufp->huge_pages_num = 0;

#ifdef DO_BUF_ALLOC
	bufp->bufferCopia = kmalloc_node(sizeof(char) * HPCAP_BUF_SIZE, GFP_KERNEL, adapter->numa_node);
	bufp->bufSize = HPCAP_BUF_SIZE;
#else /* DO_BUF_ALLOC */

	if (size == 0) {
		bufp_dbg(DBG_MEM, "Dynamic allocation of size %zu for the buffer", HPCAP_BUF_DSIZE);
		bufp->bufferCopia = kmalloc_node(sizeof(char) * HPCAP_BUF_DSIZE, GFP_KERNEL, adapter->numa_node);
		bufp->bufSize = HPCAP_BUF_DSIZE;
	} else {
		bufp->bufSize = size;
		bufp->bufferCopia = &auxBufs[ offset + bufoffset ];
	}

	bufp_dbg(DBG_MEM, "Buffer allocation. Offset %llu, size %llu. Total %lu.\n", bufoffset, bufp->bufSize, HPCAP_BUF_SIZE);
#endif /* DO_BUF_ALLOC */

	if (!(bufp->bufferCopia)) {
		DPRINTK(DRV, ERR, "Error when allocating bufferCopia-%d.%d [size=%llu]\n", adapter->bd_number, queue, bufp->bufSize);
		return -1;
	}

	bufp_dbg(DBG_MEM, "Success when allocating bufferCopia-%d.%d [size=%llu]\n", adapter->bd_number, queue, bufp->bufSize);
	bufp_dbg(DBG_MEM, "\tvirt_addr_valid(): %d\n", virt_addr_valid(bufp->bufferCopia));

	hpcap_init_listeners(&bufp->lstnr, bufp->bufSize);

#ifdef REMOVE_DUPS
	hpcap_allocate_duptable(adapter, bufp);
#endif

	return 0;
}

int hpcap_register_adapters()
{
	size_t i, j;
	unsigned int hpcap_adapters = 0;
	ssize_t adapter_bufsize;
	ssize_t adapter_bufsize_per_rxq;
	ssize_t offset = 0;
	int ret;

	for (i = 0; i < adapters_found; i++) {
		if (is_hpcap_adapter(adapters[i]))
			hpcap_adapters++;
	}

	BPRINTK(INFO, "HPCAP Adapters: %u out of %d total IXGBE.\n", hpcap_adapters, adapters_found);

	for (j = 0, i = 0; i < adapters_found; i++) {
		HW_ADAPTER* adapter = adapters[i];

		if (is_hpcap_adapter(adapter)) {
			adapter_bufsize = adapter->bufpages * PAGE_SIZE;
			adapter_bufsize_per_rxq = adapter_bufsize / adapter->num_rx_queues;

			if (adapter_bufsize + offset > HPCAP_BUF_SIZE) {
				BPRINTK(ERR, "Out of memory when allocating for adapter %zu\n", i);
				BPRINTK(ERR, "Allocated memory: %zu. Requested memory: %zu. HPCAP_BUF_SIZE: %zu\n", offset, adapter_bufsize, HPCAP_BUF_SIZE);
				return -1;
			}

			ret = hpcap_register_chardev(adapter, adapter_bufsize_per_rxq, offset, j++);

			if (ret) {
				BPRINTK(ERR, "Error when installing HPCAP devices\n");
				return -1;
			}

			offset += adapter_bufsize;

#ifdef HPCAP_SYSFS
			hpcap_sysfs_init(adapter);
#endif /* HPCAP_SYSFS */

		}
	}

	return 0;
}

void hpcap_get_buffer_info(struct hpcap_buf* bufp, struct hpcap_buffer_info* info)
{
	hpcap_huge_info(bufp, info);
	info->offset = get_buf_offset(bufp->bufferCopia);
	info->size = bufp->bufSize;
	info->addr = bufp->bufferCopia;
}
