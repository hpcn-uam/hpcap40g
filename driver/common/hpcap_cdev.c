/**
 * @brief Functions related to the management of the characted device of the HPCAP interfaces,
 * the part responsible for communication with userspace.
 */

#include "hpcap_cdev.h"
#include "hpcap_vma.h"
#include "hpcap_debug.h"
#include "hpcap_sysfs.h"

#include <linux/types.h>

static struct file_operations hpcap_fops = {
	.open = hpcap_open,
	.read = hpcap_read,
	.release = hpcap_release,
	.mmap = hpcap_mmap,
	.unlocked_ioctl = hpcap_ioctl,
};

int hpcap_register_chardev(HW_ADAPTER *adapter, u64 size, u64 offset, int ifnum)
{
	int i, ret = 0, major = 0;
	dev_t dev = 0;
	struct hpcap_buf *bufp = NULL;

	major = HPCAP_MAJOR + adapter->bd_number;
	DPRINTK(PROBE, INFO, "hpcap%d has %d rx queues.\n", adapter->bd_number, adapter->num_rx_queues);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		/*registramos un dispositivo por cola*/
		bufp = kzalloc(sizeof(struct hpcap_buf), GFP_KERNEL);

		if (!bufp) {
			DPRINTK(PROBE, ERR, "HPCAP: Error allocating hpcap_buf struct for hpcap%dq%d\n", adapter->bd_number, i);
			goto err;
		}

		dev = MKDEV(major, i);
		ret = register_chrdev_region(dev, 1, HPCAP_NAME) ;

		if (ret != 0) {
			DPRINTK(PROBE, ERR, "HPCAP: Error allocating (major,minor) region for hpcap%dq%d\n", adapter->bd_number, i);
			goto err;
		}

		hpcap_buf_init(bufp, adapter, i, size, offset);
		cdev_init(&bufp->chard, &hpcap_fops);
		bufp->chard.owner = THIS_MODULE;
		bufp->chard.ops = &hpcap_fops;
		ret = cdev_add(&bufp->chard, dev, 1);

		if (ret < 0) {
			DPRINTK(PROBE, ERR, "HPCAP: Error %d adding char device \"hpcap%dq%d\"", ret, adapter->bd_number, i);
			goto err_region;
		}

#ifdef HPCAP_CONSUMERS_VIA_RINGS

		for (j = 0; j < adapter->consumers; j++)
			adapter->rx_ring[i + j]->bufp = bufp;

		BUG_ON(adapter->num_rx_queues > 1);
#else
		adapter_dbg(DBG_DRV, "Ring structure is at 0x%p", adapter->rx_ring);

		if (adapter->rx_ring && adapter->rx_ring[i]) {
			adapter_dbg(DBG_DRV, "Enabling HPCAP on ring %d at 0x%p\n", i, adapter->rx_ring[i]);
			adapter->rx_ring[i]->bufp = bufp;
		} else {
			DPRINTK(PROBE, ERR, "Ring %d is NULL", i);
			goto err_region;
		}

#endif

		continue;
err_region:
		unregister_chrdev_region(dev, 1);
err:
		ret = -1;
	}

	if (ret == -1)
		hpcap_unregister_chardev(adapter);

	adapter_dbg(DBG_NET, "HPCAP device initialized, return value is %d\n", ret);

	return ret;
}

void hpcap_unregister_chardev(HW_ADAPTER *adapter)
{
	int i, major;
	struct hpcap_buf *bufp;
	dev_t dev;

	adapter_dbg(DBG_NET, "Unregistering chardev for the adapter\n");

	major = HPCAP_MAJOR + adapter->bd_number;
	hpcap_stop_poll_threads(adapter);

	adapter_dbg(DBG_NET, "Poll threads stoppped, removing queues.\n");

	for (i = 0; i < adapter->num_rx_queues; i++) {
		bufp = adapter->rx_ring[i]->bufp;
		BPRINTK(INFO, "Unregistering buffer at ring %d with address 0x%p", i, bufp);

		if (bufp) {
			if (hpcap_buf_clear(bufp) != 0)
				continue;

			hpcap_huge_release(bufp);

			cdev_del(&bufp->chard);
			dev = MKDEV(major, i);
			unregister_chrdev_region(dev, 1);
			kfree(bufp);
			adapter->rx_ring[i]->bufp = NULL;
		}
	}
}

static int _hpcap_set_private_data(struct file* filp, struct hpcap_buf* bufp, int handle_id)
{
	struct hpcap_file_info* info = kmalloc(sizeof(struct hpcap_file_info), GFP_KERNEL);

	if (!info)
		return -ENOMEM;

	info->bufp = bufp;
	info->handle_id = handle_id;
	filp->private_data = info;

	hpcap_get_listener(&bufp->lstnr, handle_id)->filp = filp;

	return 0;
}

static void _hpcap_release_private_data(struct file* filp)
{
	kfree(filp->private_data);
	filp->private_data = NULL;
}

int hpcap_open(struct inode *inode, struct file *filp)
{
	struct hpcap_buf *bufp = container_of(inode->i_cdev, struct hpcap_buf, chard);
	int ret;
	int open_count, handle_id;

	if (!bufp) {
		printk("HPCAP: trying to open undefined chardev\n");
		return -1;
	}

	bufp_dbg(DBG_LSTNR, "Chardev open with PID %d. %d listeners before open.\n", current->pid, atomic_read(&bufp->lstnr.listeners_count));

	open_count = atomic_inc_return(&bufp->opened);

	if (open_count > bufp->max_opened) {
		printk("HPCAP%d-%d: already opened %d times (max:%d), can't be re-opened\n", bufp->adapter, bufp->queue, open_count, bufp->max_opened);
		atomic_dec(&bufp->opened);
		return -EUSERS;
	}

	handle_id = atomic_inc_return(&bufp->last_handle);

	HPRINTK(INFO, "PID %d gets assigned handle ID %d\n", current->pid, handle_id);

	ret = hpcap_add_listener(&bufp->lstnr, handle_id);

	if (_hpcap_set_private_data(filp, bufp, handle_id) < 0) {
		BPRINTK(ERR, "Can't allocate memory for private data.\n");
		hpcap_del_listener(&bufp->lstnr, handle_id);
		atomic_dec(&bufp->opened);
		return -ENOMEM;
	}

	return 0;
}

int hpcap_release(struct inode *inode, struct file *filp)
{
	struct hpcap_buf *bufp = hpcap_buffer_of(filp);

	if (!bufp) {
		printk("HPCAP: trying to close undefined chardev\n");
		return -1;
	}

	bufp_dbg(DBG_LSTNR, "Chardev close with PID %d and handle %llu. %d listeners before close.\n",
			 current->pid, hpcap_handleid_of(filp), atomic_read(&bufp->lstnr.listeners_count));

	hpcap_del_listener(&bufp->lstnr, hpcap_handleid_of(filp));
	_hpcap_release_private_data(filp);

	atomic_dec(&bufp->opened);

	return 0;
}

ssize_t hpcap_read(struct file *filp, char __user *dstBuf, size_t count, loff_t *f_pos)
{
	ssize_t retval = -EFAULT; // Default error.
	int avail, offset, aux;
	struct hpcap_buf *bufp = hpcap_buffer_of(filp);
	struct hpcap_listener *list = NULL;
	int pid = hpcap_handleid_of(filp);
	size_t to_copy;


	if (!bufp) {
		printk("HPCAP: trying to read from undefined chardev\n");
		return -1;
	}

	/* Avoid two simultaneous read() calls from different threads/applications  */
#if MAX_LISTENERS <= 1

	if (atomic_inc_return(&bufp->readCount) != 1) {
		while (atomic_read(&bufp->readCount) != 1);
	}

#endif

	list = hpcap_get_listener_or_global(&bufp->lstnr, hpcap_handleid_of(filp));

	if (!list) {
		HPRINTK(WARNING, "Unregistered listener with PID = %d\n", pid);
		return -1;
	}

	avail = hpcap_wait_listener(list, count);
	to_copy = minimo(count, avail);
	offset = list->bufferRdOffset;

	if (offset + to_copy > bufp->bufSize) {
		aux = bufp->bufSize - offset;

		if (copy_to_user(dstBuf, &bufp->bufferCopia[offset], aux) > 0)
			goto out;

		if (copy_to_user(&dstBuf[aux], bufp->bufferCopia, to_copy - aux) > 0)
			goto out;
	} else {
		if (copy_to_user(dstBuf, &bufp->bufferCopia[offset], to_copy) > 0)
			goto out;
	}

	hpcap_pop_listener(list, to_copy);
	retval = to_copy;

out:
#if MAX_LISTENERS <= 1
	atomic_dec(&bufp->readCount);
#endif

	return retval;
}

long hpcap_ioctl(struct file * filp, unsigned int cmd, unsigned long arg2)
{
	void* arg = (void*) arg2;
	int ret = 0;
	struct hpcap_buf *bufp = hpcap_buffer_of(filp);
	struct hpcap_listener *list = NULL;
	struct hpcap_listener_op lstop;
	struct hpcap_buffer_info bufinfo;
	struct hpcap_ioc_status_info status_info;
	int arg_as_int = (int)(uintptr_t) arg;   // Just to avoid compiler warnings

	if (!bufp) {
		printk(KERN_ERR "HPCAP: ioctl-ing undefined char device\n");
		return -1;
	}

	if (hpcap_is_listener_force_killed(&bufp->lstnr, hpcap_handleid_of(filp)))
		return -EBADF; // Exit silently to avoid kernel log spam for calls of force-killed listeners.

	bufp_dbg(DBG_IOCTL, "Ioctl from handle %llu, cmd %u\n", hpcap_handleid_of(filp), cmd);

	list = hpcap_get_listener_or_global(&bufp->lstnr, hpcap_handleid_of(filp));

	if (!list) {
		HPRINTK(WARNING, "Warning: listener structure is null for PID %d. Aborting ioctl.", current->pid);
		return -EINVAL;
	}

	switch (cmd) {
		case HPCAP_IOC_LSTOP:
			if (copy_from_user(&lstop, arg, sizeof(struct hpcap_listener_op)) > 0) {
				HPRINTK(WARNING, "Bad argument pointer %p\n", arg);
				return -EFAULT;
			}

			bufp_dbg((DBG_LSTNR | DBG_IOCTL), "lstop, ack_bytes = %zu, expect_bytes = %zu\n", lstop.ack_bytes, lstop.expect_bytes);

			if (lstop.ack_bytes > 0) {
				atomic_set(&bufp->lstnr.already_popped, 1);
				hpcap_pop_listener(list, lstop.ack_bytes);
			}

			if (likely(lstop.expect_bytes > 0))
				hpcap_wait_listener_user(list, &lstop);

			bufp_dbg(DBG_LSTNR, "lstop, %zu available bytes from offset %llu\n", lstop.available_bytes, lstop.read_offset);
			bufp_dbg(DBG_LSTNR, "lstop, global R: %zu W: %zu\n", bufp->lstnr.global.bufferRdOffset, bufp->lstnr.global.bufferWrOffset);

			if (copy_to_user(arg, &lstop, sizeof(struct hpcap_listener_op)) > 0) {
				HPRINTK(WARNING, "Could not copy back %p\n", arg);
				return -EFAULT;
			}

			break;

		case HPCAP_IOC_OFFSETS:
			if (copy_from_user(&lstop, arg, sizeof(struct hpcap_listener_op)) > 0) {
				HPRINTK(WARNING, "Bad argument pointer %p\n", arg);
				return -EFAULT;
			}

			bufp_dbg(DBG_IOCTL, "offsets, R: %llu, W: %llu\n", list->bufferRdOffset, list->bufferWrOffset);

			lstop.read_offset = list->bufferRdOffset;
			lstop.write_offset = list->bufferWrOffset;

			if (copy_to_user(arg, &lstop, sizeof(struct hpcap_listener_op)) > 0) {
				HPRINTK(WARNING, "Could not copy back %p\n", arg);
				return -EFAULT;
			}

			break;

		case HPCAP_IOC_KILLWAIT:
			HPRINTK(WARNING, "Killwait request from PID %llu\n", hpcap_handleid_of(filp));
			hpcap_killall_listeners(&bufp->lstnr);
			break;

		case HPCAP_IOC_BUFINFO:
			if (copy_from_user(&bufinfo, arg, sizeof(struct hpcap_buffer_info)) > 0) {
				HPRINTK(WARNING, "Bad argument pointer %p\n", arg);
				return -EFAULT;
			}

			hpcap_get_buffer_info(bufp, &bufinfo);
			bufp_dbg(DBG_IOCTL, "bufinfo, %zu bytes at 0x%p + %zu, hugepages = %hd",
					 bufinfo.size, bufinfo.addr, bufinfo.offset, bufinfo.has_hugepages);

			if (copy_to_user(arg, &bufinfo, sizeof(struct hpcap_buffer_info)) > 0) {
				HPRINTK(WARNING, "Could not copy back %p\n", arg);
				return -EFAULT;
			}

			break;

#ifdef REMOVE_DUPS

		case HPCAP_IOC_DUP:
			if (bufp->dupTable)
				copy_to_user((void *)arg, bufp->dupTable[0], sizeof(struct hpcap_dup_info)*DUP_WINDOW_SIZE * DUP_WINDOW_LEVELS);

			break;
#endif

		case HPCAP_IOC_HUGE_MAP:
			if (copy_from_user(&bufinfo, arg, sizeof(struct hpcap_buffer_info)) > 0) {
				HPRINTK(WARNING, "Bad argument pointer %p\n", arg);
				return -EFAULT;
			}

			bufp_dbg(DBG_IOCTL, "huge_map");
			ret = hpcap_huge_use(bufp, &bufinfo);

			if (copy_to_user(arg, &bufinfo, sizeof(struct hpcap_buffer_info)) > 0) {
				HPRINTK(WARNING, "Could not copy back %p\n", arg);
				return -EFAULT;
			}

			break;

		case HPCAP_IOC_HUGE_UNMAP:
			bufp_dbg(DBG_IOCTL, "huge_unmap");
			ret = hpcap_huge_release(bufp);
			break;

		case HPCAP_IOC_STATUS_INFO:
			if (copy_from_user(&status_info, arg, sizeof(struct hpcap_ioc_status_info)) > 0) {
				HPRINTK(WARNING, "Bad argument pointer %p\n", arg);
				return -EFAULT;
			}

			hpcap_check_status(bufp, &status_info);

			if (copy_to_user(arg, &status_info, sizeof(struct hpcap_ioc_status_info)) > 0) {
				HPRINTK(WARNING, "Could not copy back %p\n", arg);
				return -EFAULT;
			}

			break;

		case HPCAP_IOC_BUFCHECK:
			bufp_dbg(DBG_IOCTL, "Buffer check result");

			if (hpcap_buffer_check(bufp) < 0)
				ret = -EINVAL;

			break;

		case HPCAP_IOC_KILL_LST:
			HPRINTK(WARNING, "Kill request for listener %d\n", arg_as_int);
			ret = hpcap_kill_listener(&bufp->lstnr, arg_as_int);
			break;

		default:
			HPRINTK(WARNING, "Unrecognized ioctl from handle %llu, cmd %u\n", hpcap_handleid_of(filp), cmd);
			ret = -ENOTTY;
			break;
	};

	bufp_dbg(DBG_IOCTL, "ioctl result: %d\n", ret);

	return ret;
}

void hpcap_check_naming(HW_ADAPTER * adapter)
{
	size_t max_name = 30;
	char expected_name[max_name];
	char* actual_name;
	struct net_device* netdev = adapter ? adapter_netdev(adapter) : NULL;

	if (adapter == NULL || netdev == NULL) {
		BPRINTK(ERR, "Cannot check naming: adapter %p, netdev %p\n", adapter, netdev);
		return;
	}

	actual_name = netdev->name;

	snprintf(expected_name, max_name, "hpcap%d", adapter->bd_number);

	if (strncmp(expected_name, actual_name, max_name) != 0)
		BPRINTK(WARNING, "Found interface rename: renamed network interface %s to %s\n", expected_name, actual_name);
}

void hpcap_check_status(struct hpcap_buf * bufp, struct hpcap_ioc_status_info * status_info)
{
	int i, hpcap_adapters = 0;

	//Extern adapters array
	extern HW_ADAPTER * adapters[HPCAP_MAX_NIC];
	extern int adapters_found;

	//Adapter and devices
	HW_ADAPTER * adapter;
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct  hpcap_ioc_status_info_adapter info_adapter;

	//Buffer
	struct hpcap_buffer_info bufinfo;

	//Listener
	struct hpcap_buffer_listeners lstnr;
	struct hpcap_listener *listeners;
	struct hpcap_listener listener;
	int listeners_count;

	//Hilo de recepción
	struct task_struct *thread;

	//Iterate over the adapters array
	for (i = 0; i < adapters_found; i++) {
		adapter = adapters[i];

		if (adapter != NULL && is_hpcap_adapter(adapter)) {
			//Network interface
			netdev = adapter_netdev(adapter);

			//PCI bus ID where it's connected
			pdev = adapter->pdev;

			if (netdev != NULL && netdev->name != NULL &&
				pdev != NULL && pdev->bus != NULL) {
				strncpy(info_adapter.netdev_name, netdev->name, MAX_NETDEV_NAME);
				strncpy(info_adapter.pdev_name, pci_name(pdev), MAX_PCI_BUS_NAME_LEN);
				status_info->adapters[hpcap_adapters] = info_adapter;

				hpcap_adapters++;
			}
		}
	}

	status_info->num_adapters = hpcap_adapters;


	//Buffer info. Bufinfo output variable
	hpcap_get_buffer_info(bufp, &bufinfo);
	status_info->bufinfo = bufinfo;

	//Listeners
	lstnr = bufp->lstnr;

	//Global listener
	listener = lstnr.global;
	status_info->global_listener = hpcap_build_ioc_listener_from_hpcap_listener(listener);

	//Loop over the array of active listeners
	listeners = lstnr.listeners;
	listeners_count = atomic_read(&lstnr.listeners_count);
	status_info->num_listeners = listeners_count;

	for (i = 0; i < listeners_count; i++)
		status_info->listeners[i] = hpcap_build_ioc_listener_from_hpcap_listener(listeners[i]);

	status_info->consumer_write_off = atomic_read(&bufp->consumer_write_off);
	status_info->consumer_read_off = atomic_read(&bufp->consumer_read_off);

	//Reception thread
	/* thread = bufp->hilo;
	status_info->thread_state = -1;

	if (thread != NULL)
		status_info->thread_state = thread->state;

	 */
}

struct hpcap_ioc_status_info_listener hpcap_build_ioc_listener_from_hpcap_listener(struct hpcap_listener original)
{
	struct hpcap_ioc_status_info_listener listener;

	listener.id = (long)atomic_read(&original.id);
	listener.kill = (long)atomic_read(&original.kill);
	listener.bufferWrOffset = original.bufferWrOffset;
	listener.bufferRdOffset = original.bufferRdOffset;
	listener.buffer_size = original.bufsz;

	return listener;
}
