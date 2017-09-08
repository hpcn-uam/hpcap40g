/**
 * @author Guillermo Julián (guillermo.julian@estudiante.uam.es)
 * @brief Functions to manage and include the hugepages
 *        in the HPCAP buffers.
 */

#include "hpcap_hugepages.h"
#include "hpcap_debug.h"

#include <linux/hugetlb.h>
#include <linux/page-flags.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/**
 * Configures the driver to use hugepages allocated in userspace.
 * @param buf  HPCAP descriptor.
 * @param huge Structure holding the hugepage buffer information.
 * @return     0 if OK, negative if error.
 */
int hpcap_huge_use(struct hpcap_buf* bufp, struct hpcap_buffer_info* bufinfo)
{
	struct page** pages;
	int retval;
	unsigned long npages;
	unsigned long buffer_start = (unsigned long) bufinfo->addr;
	void* remapped;
	int nid;

	/* Calculate the number of pages to allocate the neccessary
	 * number of struct page* */
	npages = 1 + ((bufinfo->size - 1) / PAGE_SIZE); /* Ceil of division bufinfo->size / PAGE_SIZE */

	bufp_dbg(DBG_MEM, "Mapping %lu pages into kernel space. Backing file is %s\n", npages, bufinfo->file_name);

	pages = vmalloc(npages * sizeof(struct page *));

	if (pages == NULL)
		return -EFAULT;

	/* Get the pages supporting the buffer passed from userspace */
	down_read(&current->mm->mmap_sem);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0))
	retval = get_user_pages(buffer_start, npages, 1, pages, NULL);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0))
	retval = get_user_pages(current, current->mm, buffer_start, npages,
							1 /* Write enable */, 0 /* Force */, pages, NULL);
#else
	retval = get_user_pages(buffer_start, npages,
							1 /* Write enable */, 0 /* Force */, pages, NULL);
#endif
	up_read(&current->mm->mmap_sem);

	bufp_dbg(DBG_MEM, "get_user_pages: return value %d for request of %lu pages starting at %p\n", retval, npages, (void*) buffer_start);

	if (retval < 0) {
		vfree(pages);
		return retval;
	}

	if (retval != npages) {
		/* TODO: What do we do here? */
		bufp_dbg(DBG_MEM, "get_user_pages: returned less pages (%d) than asked (%lu).\n", retval, npages);
		npages = retval;
	}

	bufp->old_buffer = bufp->bufferCopia; /* Save the current buffer, so it can be reused when the HP are unmapped */
	bufp->old_bufSize = bufp->bufSize;

	nid = page_to_nid(pages[0]);

	bufp_dbg(DBG_MEM, "Requesting linear mapping of pages in NUMA node %d\n", nid);

	remapped = vm_map_ram(pages, npages, nid, PAGE_KERNEL);

	if (!remapped) {
		HPRINTK(WARNING, "Remapping failed.\n");
		return 0;
	}

	bufp_dbg(DBG_MEM, "Remapping succeeded. New address is %p\n", remapped);

	bufp->bufferCopia = remapped;
	bufp->bufSize = bufinfo->size;
	bufp->huge_pages = pages;
	bufp->huge_pages_num = npages;
	strncpy(bufp->huge_pages_file, bufinfo->file_name, MAX_HUGETLB_FILE_LEN);

	hpcap_update_listener_bufsizes(&bufp->lstnr, bufp->bufSize);

	HPRINTK(INFO, "Hugepage buffer %s of size %zu correctly set up.\n", bufinfo->file_name, bufp->bufSize);

	return 0;
}

/**
 * Frees the allocated hugepages.
 *
 * @notes This function assumes an existing lock over hpcap_buf struct to avoid
 *        multiple concurrent modifications. If the lock doesn't exist, this function
 *        ** is not secure for concurrency**.
 *
 * @param buf HPCAP descriptor.
 * @return     0 if OK, or -EIDRM if there are no hugepages.
 */
int hpcap_huge_release(struct hpcap_buf* bufp)
{
	int i;

	if (!has_hugepages(bufp))
		return -EIDRM;

	for (i = 0; i < bufp->huge_pages_num; i++) {
		kunmap(bufp->huge_pages[i]);

		down_read(&current->mm->mmap_sem);

		if (!PageReserved(bufp->huge_pages[i]))
			SetPageDirty(bufp->huge_pages[i]);

		put_page(bufp->huge_pages[i]);

		up_read(&current->mm->mmap_sem);
	}

	bufp->bufferCopia = bufp->old_buffer;

	vfree(bufp->huge_pages);
	bufp->huge_pages_num = 0;
	bufp->huge_pages = NULL;
	memset(bufp->huge_pages_file, 0, MAX_HUGETLB_FILE_LEN);

	bufp_dbg(DBG_MEM, "Released mapping.\n");

	return 0;
}

/**
 * Get the information (buffer size and file name) for the hugepages in this descriptor.
 *
 * If there are no hugepages, huge->size will be 0.
 * @param buf  HPCAP descriptor.
 * @param huge Hugepage information structure.
 */
void hpcap_huge_info(struct hpcap_buf* buf, struct hpcap_buffer_info* info)
{
	info->has_hugepages = has_hugepages(buf);

	if (!info->has_hugepages)
		memset(info->file_name, 0, MAX_HUGETLB_FILE_LEN);
	else {
		info->size = buf->bufSize;
		strncpy(info->file_name, buf->huge_pages_file, MAX_HUGETLB_FILE_LEN);
	}
}
