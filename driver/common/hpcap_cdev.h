/**
 * @brief Functions related to the management of the characted device of the HPCAP interfaces,
 * the part responsible for communication with userspace.
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_CDEV_H
#define HPCAP_CDEV_H

#include "hpcap_types.h"
#include "hpcap_hugepages.h"

#include <linux/types.h>
#include <linux/cdev.h>

/**
 * Register all the character devices associated to the given adapter.
 * @param  adapter Hardware adapter.
 * @param  bufsize Size of the buffer associated to each queue.
 * @param  offset  Offset of the buffer in the global buffer space.
 * @param  ifnum   Interface number.
 * @return         0 if OK, negative if error.
 */
int hpcap_register_chardev(HW_ADAPTER *adapter, u64 size, u64 offset, int ifnum);

/**
 * Unregister all the characted devices associated to the given adapter.
 * @param  adapter Hardware adapter.
 */
void hpcap_unregister_chardev(HW_ADAPTER *adapter);

/**
 * Respond to an open() request from userspace.
 * @param  inode Inode opened.
 * @param  filp  File information structure.
 * @return       0 if OK, negative if error.
 */
int hpcap_open(struct inode *inode, struct file *filp);

/**
 * Respond to a close() request from userspace.
 * @param  inode Inode to close.
 * @param  filp  File information structure.
 * @return       0 if OK, negative if error.
 */
int hpcap_release(struct inode *inode, struct file *filp);

/**
 * Respond to a read() request from userspace.
 * @param  filp   File information structure.
 * @param  dstBuf Buffer where the data should be saved.
 * @param  count  Number of bytes to read.
 * @param  f_pos  Unused.
 * @return        Number of bytes read.
 */
ssize_t hpcap_read(struct file *filp, char __user *dstBuf, size_t count, loff_t *f_pos);

/**
 * Respond to an ioctl() request from userspace.
 * @param  filp File information structure.
 * @param  cmd  Command.
 * @param  arg2 Data arguments.
 * @return      0 if OK, -1 if error.
 */
long hpcap_ioctl(struct file *filp, unsigned int cmd, unsigned long arg2);

/**
 * Ensures that the adapter is named correctly. If it isn't, it logs a message
 * to the kernel log so userspace services can override the system rename.
 *
 * @param  adapter Interface adapter structure
 */
void hpcap_check_naming(HW_ADAPTER* adapter);

/**
 * IOCTL auxiliar function, gets the status of the driver. For each interface
 * shows:
 * 	- interface name and PCI bus ID
 	- buffer status,
 	- listener status and info,
 	- reception thread status
 */
void hpcap_check_status(struct hpcap_buf *bufp, struct hpcap_ioc_status_info *status_info);

/**
 * Builds a user space listener struct from a kernel space one.
 */
struct hpcap_ioc_status_info_listener hpcap_build_ioc_listener_from_hpcap_listener(struct hpcap_listener original);

/** @} */

#endif
