/**
 * @brief Management of memory mappings to and from userspace
 */


#include "hpcap_vma.h"
#include "driver_hpcap.h"
#include "hpcap_debug.h"

#include <linux/kernel.h>


void hpcap_vma_open(struct vm_area_struct *vma)
{
	struct file *filp = vma->vm_file;
	struct hpcap_buf *bufp;

	if (filp) {
		bufp = hpcap_buffer_of(filp);
		bufp_dbg(DBG_MEM, "VMA open, virt %lx, phys %lx\n", vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
	}
}

void hpcap_vma_close(struct vm_area_struct *vma)
{
	struct file *filp = vma->vm_file;
	struct hpcap_buf *bufp;
	unsigned long len;
#ifndef DO_BUF_ALLOC
	unsigned long kaddr;
#endif

	if (filp) {
		bufp = hpcap_buffer_of(filp);
		bufp_dbg(DBG_MEM, "VMA close\n");

#ifndef DO_BUF_ALLOC

		if (!has_hugepages(bufp)) {
			kaddr = (unsigned long) bufp->bufferCopia;
			kaddr = (kaddr >> PAGE_SHIFT) << PAGE_SHIFT;
			len = vma->vm_end - vma->vm_start;

			for (; len > 0; kaddr += PAGE_SIZE, len -= PAGE_SIZE)
				ClearPageReserved(vmalloc_to_page((void *) kaddr));
		}

#endif
	}
}

static struct vm_operations_struct hpcap_vm_ops = {
	.open = hpcap_vma_open,
	.close = hpcap_vma_close,
};

int get_buf_offset(void *buf)
{
	unsigned long int phys, pfn;

	phys = __pa(buf);
	pfn = phys >> PAGE_SHIFT;

	return (phys - (pfn << PAGE_SHIFT));
}

int hpcap_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct hpcap_buf *bufp = hpcap_buffer_of(filp);
	unsigned long len;
	unsigned long int phys, pfn;
#ifndef DO_BUF_ALLOC
	unsigned long mapaddr, kaddr;
	struct page *page;
	int npag, err = 0;
#endif

	if (!bufp) {
		printk(KERN_CRIT "HPCAP: mmapping undefined char device\n");
		return -1;
	}

	/* Avoid two simultaneous mmap() calls from different threads/applications  */
	// TODO: Change to a spinlock.
	if (atomic_inc_return(&bufp->mmapCount) != 1) {
		while (atomic_read(&bufp->mmapCount) != 1);
	}

	bufp_dbg(DBG_MEM, "Mapping request received.\n");

	len = vma->vm_end - vma->vm_start;

#ifdef DO_BUF_ALLOC

	if (1) {
		phys = virt_to_phys((void *)bufp->bufferCopia);
#else

	if (has_hugepages(bufp)) {
		bufp_dbg(DBG_MEM, "HPCAP: Remapping hugepages.\n");
		phys = virt_to_phys((void *) page_address(bufp->huge_pages[0]));
#endif

		pfn = phys >> PAGE_SHIFT;

		if (remap_pfn_range(vma, vma->vm_start, pfn, len, vma->vm_page_prot)) {
			printk(KERN_ERR "HPCAP: Error when trying to remap_pfn_range: size:%lu hpcap_buf_Size%lu\n", len, HPCAP_BUF_SIZE);
			return -EAGAIN;
		}

		bufp_dbg(DBG_MEM, "Buffer mapped at 0x%08lx, sized %lu bytes [offset=%lu] [ALLOC]\n", vma->vm_start, len, phys - (pfn << PAGE_SHIFT));
	} else {
		kaddr = (unsigned long) bufp->bufferCopia;
		kaddr = (kaddr >> PAGE_SHIFT) << PAGE_SHIFT;
		npag = 0;

		for (mapaddr = vma->vm_start; mapaddr < vma->vm_end; mapaddr += PAGE_SIZE) {
			page = vmalloc_to_page((void *) kaddr);
			SetPageReserved(page);
			err = vm_insert_page(vma, mapaddr, page);

			if (err)
				break;

			kaddr += PAGE_SIZE;
			npag++;
		}

		if (err) {
			kaddr = (unsigned long) bufp->bufferCopia;
			kaddr = (kaddr >> PAGE_SHIFT) << PAGE_SHIFT;

			for (; len > 0; kaddr += PAGE_SIZE, len -= PAGE_SIZE)
				ClearPageReserved(vmalloc_to_page((void *) kaddr));
		}

		bufp_dbg(DBG_MEM, "Buffer mapped as %d different pages\n", npag);
	}

	vma->vm_ops = &hpcap_vm_ops;
	hpcap_vma_open(vma);
	atomic_dec(&bufp->mmapCount);

	return 0;
}

int hpcap_buffer_check(struct hpcap_buf* bufp)
{
	void* addr = bufp->bufferCopia;
	size_t block_size = PAGE_SIZE;
	size_t total_blocks = bufp->bufSize / block_size;
	size_t offset;

	bufp_dbg(DBG_MEM, "Buffer writability check. Start at %p, size %llu. Block size is %zu, total blocks to write %zu\n",
			 addr, bufp->bufSize, block_size, total_blocks);

	for (offset = 0; offset < bufp->bufSize; offset += block_size) {
		memset(addr + offset, 1, block_size);
		bufp_dbg(DBG_MEM, "Block %p - %p is ok\n", addr + offset, addr + offset + block_size);
	}

	return 0;
}
