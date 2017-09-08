/**
 * @brief Management of memory mappings to and from userspace
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_VMA_H
#define HPCAP_VMA_H

#include <linux/mm.h>
#include <linux/fs.h>
#include "hpcap_types.h"

void hpcap_vma_open(struct vm_area_struct *vma);
void hpcap_vma_close(struct vm_area_struct *vma);
int get_buf_offset(void *buf);
int hpcap_mmap(struct file *filp, struct vm_area_struct *vma);

/**
 * Checks that the buffer is writable.
 *
 * Will possibly cause a segfault if there's a problem with the buffer.
 * @param bufp Buffer
 * @return     0 if ok, -1 if error.
 */
int hpcap_buffer_check(struct hpcap_buf* bufp);

#endif

