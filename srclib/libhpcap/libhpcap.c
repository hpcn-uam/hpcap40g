#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <pcap.h>

#include "../include/hpcap.h"

int hpcap_open(struct hpcap_handle *handle, int adapter_idx, int queue_idx)
{
	char devname[100] = "";
	memset(handle, 0, sizeof(struct hpcap_handle));

	sprintf(devname, "/dev/hpcap_%d_%d", adapter_idx, queue_idx);
	handle->fd = open(devname, O_RDWR);

	if (handle->fd == -1) {
		fprintf(stderr, "Error when opening device %s\n", devname);
		return HPCAP_ERR;
	}

	handle->queue_idx = queue_idx;
	handle->adapter_idx = adapter_idx;
	handle->buf = NULL;
	handle->avail = 0;
	handle->rdoff = 0;
	handle->file_offset = 0;
	//handle->lastrdoff = 0;
	handle->acks = 0;
	handle->page = NULL;
	handle->bufoff = 0;
	handle->bufSize = 0;
	handle->size = 0;

	return HPCAP_OK;
}

void hpcap_close(struct hpcap_handle *handle)
{
	if (handle->fd != -1) {
		close(handle->fd);
		handle->fd = 0;
		handle->queue_idx = 0;
		handle->adapter_idx = 0;
		handle->avail = 0;
		handle->rdoff = 0;
		handle->acks = 0;
		//handle->lastrdoff = 0;
		handle->page = NULL;
		handle->bufoff = 0;
		handle->size = 0;
	}
}

/**
 * @internal
 * Get the pointer for the buffer backed by the given hugepage file.
 * @param 	handle  HPCAP handle.
 * @param	bufinfo Structure with the HP buffer information.
 * @return          HPCAP_OK or HPCAP_ERR.
 */
static int _hpcap_mmap_hugetlb(struct hpcap_handle* handle, struct hpcap_buffer_info* bufinfo)
{
	/* Open the file passed as argument, create it if it doesn't exist */
	handle->hugepage_fd = open(bufinfo->file_name, O_CREAT | O_RDWR, 0755);

	if (handle->hugepage_fd < 0) {
		fprintf(stderr, "hpcap_map_huge/open: open(%s) failed: %s\n", handle->hugepage_name, strerror(errno));
		return HPCAP_ERR;
	}

	handle->hugepage_addr = mmap(NULL, bufinfo->size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->hugepage_fd, 0);

	if (handle->hugepage_addr == MAP_FAILED) {
		fprintf(stderr, "hpcap_map_huge/mmap: mmapping for a region of size %zu failed: %s\n", bufinfo->size, strerror(errno));
		close(handle->hugepage_fd);
		return HPCAP_ERR;
	}

	return HPCAP_OK;
}

int hpcap_map(struct hpcap_handle *handle)
{
	struct hpcap_buffer_info bufinfo;
	int ret = 0;
	int size = 0, pagesize = 0;

	/*
	 * First, check all the buffer information.
	 * We have to get the buffer offsets and, if there are
	 * hugepages mapped in the buffer, get their name to get
	 * the backing file.
	 */
	ret = ioctl(handle->fd, HPCAP_IOC_BUFINFO, &bufinfo);

	if (ret < 0) {
		perror("ioctl");
		return HPCAP_ERR;
	}

	handle->bufoff = bufinfo.offset;
	handle->bufSize = bufinfo.size;

	if (!bufinfo.has_hugepages) {
		pagesize = sysconf(_SC_PAGESIZE);
		size = handle->bufSize + handle->bufoff;

		if ((size % pagesize) != 0)
			size = ((size / pagesize) + 1) * pagesize;

		handle->size = size;
		handle->page = (u_char *)mmap(NULL, handle->size, PROT_READ , MAP_SHARED | MAP_LOCKED, handle->fd, 0);

#ifdef DEBUG
		printf("MMAP's - offset: %"PRIu64", size: %"PRIu64" (pagesize: %d)\n", handle->bufoff, handle->bufSize, pagesize);
#endif

		if ((long)handle->page == -1) {
			perror("mmap");
			return HPCAP_ERR;
		}

		handle->buf = &(handle->page[ handle->bufoff ]);
	} else {
		/* The buffer is backed by hugepages. Map the corresponding file */
		if (_hpcap_mmap_hugetlb(handle, &bufinfo))
			return HPCAP_ERR;

		handle->page = handle->hugepage_addr;
		handle->buf = ((uint8_t*) handle->hugepage_addr) + handle->bufoff;
	}

	return HPCAP_OK;
}

int hpcap_unmap(struct hpcap_handle *handle)
{
	int ret;

	ret = munmap(handle->page, handle->size);
	handle->buf = NULL;
	handle->page = NULL;
	handle->bufoff = 0;

	return ret ? HPCAP_ERR : HPCAP_OK;
}

/**
 * Advances the read offset of the handle, keeping track of the offset in the file.
 *
 * @param handle     HPCAP handle
 * @param read_bytes Number of bytes to advance the offset by
 */
void _hpcap_advance_rdoff_by(struct hpcap_handle* handle, size_t read_bytes)
{
	handle->rdoff = (handle->rdoff + read_bytes) % handle->bufSize;
	handle->file_offset = (handle->file_offset + read_bytes) % HPCAP_FILESIZE;
}

/**
 * Advances the read offset of the handle, keeping track of the offset in the file.
 *
 * @param handle     HPCAP handle
 * @param new_offset New read offset.
 */
void _hpcap_advance_rdoff_to(struct hpcap_handle* handle, size_t new_offset)
{
	size_t read_bytes = 0;

	if (new_offset > handle->rdoff)
		read_bytes = new_offset - handle->rdoff;
	else if (new_offset < handle->rdoff) {
		read_bytes = (handle->bufSize - handle->rdoff) + new_offset;
		// printdbg("Read bytes = %zu (rdoff %zu, new %zu)\n", read_bytes, handle->rdoff, new_offset);
	}

	_hpcap_advance_rdoff_by(handle, read_bytes);
}

/**
 * @internal
 * Common function to execute a listener operation with the driver.
 *
 * @param  handle       HPCAP handle.
 * @param  expect_bytes Bytes to expect. If 0, no "wait" operation will be performed.
 * @param  do_ack       1 if we should acknowledge the read bytes to the driver, 0 if not.
 * @param  timeout_ns   Timeout in nanoseconds for the wait operation. If 0, there's no timeout.
 * @return              HPCAP_OK/HPCAP_ERR. Errno will be set appropiately.
 */
static int _hpcap_do_listener_op(struct hpcap_handle* handle, size_t expect_bytes, short do_ack, uint64_t timeout_ns)
{
	struct hpcap_listener_op lstop;
	int ret;

	if (do_ack && handle->acks > 0)
		lstop.ack_bytes = handle->acks;
	else
		lstop.ack_bytes = 0;

	lstop.expect_bytes = expect_bytes;
	lstop.timeout_ns = timeout_ns;

	//	printdbg("lstop on %d: ack = %zu, expect = %zu, timeout = %lu\n",
	//			 handle->fd, lstop.ack_bytes, lstop.expect_bytes, lstop.timeout_ns);

	ret = ioctl(handle->fd, HPCAP_IOC_LSTOP, &lstop);

	//	printdbg("lstop result: avail %llu, rdoff %llu\n", lstop.available_bytes, lstop.read_offset);

	if (ret < 0) {
		perror("lstop ioctl");

		if (errno == EBADF) {
			fprintf(stderr, "HPCAP client was force killed. Aborting.\n");
			abort();
		}

		return HPCAP_ERR;
	}

	if (do_ack) {
		if (handle->avail < handle->acks) {
			printerr("FATAL: Trying to acknowledge more bytes than available (avail = %zu, acks = %zu). Aborting.\n", handle->avail, handle->acks);
			abort();
		}

		handle->avail -= handle->acks;
		handle->acks = 0;
	}

	if (expect_bytes > 0 && lstop.available_bytes >= expect_bytes) {
		handle->avail = lstop.available_bytes;
		_hpcap_advance_rdoff_to(handle, lstop.read_offset);

		//printdbg("Update avail & rdoff, values %llu & %llu\n", handle->avail, handle->rdoff);
	}

	return HPCAP_OK;
}

void hpcap_print_listener_status(FILE* out, struct hpcap_handle* handle)
{
	fprintf(out, "Status for hpcap%dq%d (fd %d): %"PRIu64" available bytes, %"PRIu64" bytes with pending ack. Read offset is %"PRIu64"\n",
			handle->adapter_idx, handle->queue_idx, handle->fd, handle->avail, handle->acks, handle->rdoff);
}

int hpcap_wait(struct hpcap_handle *handle, uint64_t count)
{
	return _hpcap_do_listener_op(handle, count, 0, 0);
}

int hpcap_ack(struct hpcap_handle *handle)
{
	return _hpcap_do_listener_op(handle, 0, 1, 0);
}

int hpcap_ack_wait(struct hpcap_handle *handle, uint64_t waitcount)
{
	return _hpcap_do_listener_op(handle, waitcount, 1, 0);
}

int hpcap_ack_wait_timeout(struct hpcap_handle *handle, uint64_t waitcount, uint64_t timeout_ns)
{
	return _hpcap_do_listener_op(handle, waitcount, 1, timeout_ns);
}

int hpcap_wroff(struct hpcap_handle *handle)
{
	int ret;
	struct hpcap_listener_op lstop;

	ret = ioctl(handle->fd, HPCAP_IOC_OFFSETS, &lstop);

	if (ret >= 0)
		return lstop.write_offset;

	return ret;
}

int hpcap_rdoff(struct hpcap_handle *handle)
{
	int ret;
	struct hpcap_listener_op lstop;

	ret = ioctl(handle->fd, HPCAP_IOC_OFFSETS, &lstop);

	if (ret >= 0)
		return lstop.read_offset;

	return ret;
}

int hpcap_ioc_killwait(struct hpcap_handle *handle)
{
	int ret;

	ret = ioctl(handle->fd, HPCAP_IOC_KILLWAIT);

	return ret < 0 ? HPCAP_ERR : HPCAP_OK;
}

int hpcap_ioc_kill(struct hpcap_handle* handle, int listener_id)
{
	return ioctl(handle->fd, HPCAP_IOC_KILL_LST, listener_id);
}

int hpcap_status_info(struct hpcap_handle* handle, struct hpcap_ioc_status_info* info)
{
	int ret = ioctl(handle->fd, HPCAP_IOC_STATUS_INFO, info);

	return ret < 0 ? HPCAP_ERR : HPCAP_OK;
}

size_t hpcap_ioc_listener_info_available_bytes(struct hpcap_ioc_status_info_listener* l)
{
	if (l->bufferRdOffset <= l->bufferWrOffset)
		return l->bufferWrOffset - l->bufferRdOffset;
	else
		return (l->buffer_size - l->bufferRdOffset) + l->bufferWrOffset;
}

double hpcap_ioc_listener_occupation(struct hpcap_ioc_status_info_listener* l)
{
	size_t available_bytes = hpcap_ioc_listener_info_available_bytes(l);

	return ((double)(l->buffer_size - available_bytes)) / l->buffer_size;
}

#ifdef REMOVE_DUPS
int hpcap_dup_table(struct hpcap_handle *handle)
{
	int ret;
	struct hpcap_dup_info *tabla = NULL;
	int i = 0, j = 0;

	tabla = (struct hpcap_dup_info *)malloc(sizeof(struct hpcap_dup_info) * DUP_WINDOW_SIZE);

	ret = ioctl(handle->fd, HPCAP_IOC_DUP, tabla);

	if (ret >= 0) {
		for (i = 0; i < DUP_WINDOW_SIZE; i++) {
			printf("[%04d] %ld , %d , ", i, tabla[i].tstamp, tabla[i].len);

			for (j = 0; j < DUP_CHECK_LEN; j++)
				printf("%02x ", tabla[i].data[j]);

			printf("\n");
		}
	} else
		printf("Interface hpcap%dq%d does NOT check for duplicated packets.\n", handle->adapter_idx, handle->queue_idx);

	return ret >= 0 ? HPCAP_OK : HPCAP_ERR;
}
#endif

inline void hpcap_pcap_header(void *header, u32 secs, u32 nsecs, u16 len, u16 caplen)
{
	struct pcap_pkthdr *head = (struct pcap_pkthdr *) header;

	head->ts.tv_sec = secs;
	head->ts.tv_usec = nsecs / 1000; //noseconds to useconds
	head->caplen = caplen;
	head->len = len;
}
inline void hpcap_pcap_header_ns(void *header, u32 secs, u32 nsecs, u16 len, u16 caplen)
{
	struct pcap_pkthdr *head = (struct pcap_pkthdr *) header;

	head->ts.tv_sec = secs;
	head->ts.tv_usec = nsecs;
	head->caplen = caplen;
	head->len = len;
}

uint8_t* hpcap_get_memory_at_readable_offset(struct hpcap_handle* handle, size_t offset)
{
	size_t real_offset = handle->rdoff + offset;

	real_offset = real_offset % handle->bufSize;

	return handle->buf + real_offset;
}

/**
 * Copies data from a circular buffer.
 *
 * @param src        Source buffer.
 * @param src_offset Offset of the source buffer, from which the copy will start.
 * @param src_size   Sice of the source buffer.
 * @param dst        Destination buffer. Must have enough space.
 * @param to_copy    Number of bytes to copy.
 *
 * @returns 		 Offset to the first byte after the copied data.
 */
inline size_t copy_from_circ_buffer(const uint8_t* src, size_t src_offset, size_t src_size, uint8_t* dst, size_t to_copy)
{
	size_t aux;
	size_t new_offset;

	if (unlikely(src_offset + to_copy > src_size)) {
		aux = src_size - src_offset;
		memcpy(dst, src + src_offset , aux);
		memcpy(dst, src, to_copy - aux);

		new_offset = to_copy - aux;
	} else {
		memcpy(dst, src + src_offset, to_copy);

		new_offset = (src_offset + to_copy) % src_size;
	}

	return new_offset;
}

uint64_t hpcap_read_packet(struct hpcap_handle *handle, u_char **pbuffer, u_char *auxbuf, void *header, void (* read_header)(void *, u32, u32, u16, u16))
{
	u64 offs = handle->rdoff;
	size_t acks = 0;
	struct raw_header rawh;
	short has_padding;
	size_t header_begin;

	if (unlikely(handle->acks >= handle->avail)) {
		printerr("Bad situation: too many acks (%lu acks, %lu available)\n", handle->acks, handle->avail);
		*pbuffer = NULL;
		return UINT64_MAX;
	} else if (unlikely(handle->avail < RAW_HLEN)) {
		printerr("Bad situation: not enough bytes available (only %lu, need at least RAW_HLEN = %lu)\n",
				 handle->avail, RAW_HLEN);
		*pbuffer = NULL;
		return 0;
	}

	int num_paddings = 0;

	// If there's padding, we want to read another header. Prepare for it.
	do {
		header_begin = offs;

		if (num_paddings >= 4)
			abort();

		offs = copy_from_circ_buffer(handle->buf, offs, handle->bufSize, (uint8_t*) &rawh, sizeof(struct raw_header));
		acks += sizeof(struct raw_header);

		if (unlikely(handle->avail < (handle->acks + acks + rawh.caplen))) {
			printdbg("Wrong situation at offset %zu: available < RAW_HLEN + caplen (avail=%lu, acks=%lu, caplen=%u, RAW_HLEN = %zu)\n",
					 header_begin, handle->avail, handle->acks + acks, rawh.caplen, RAW_HLEN);
			*pbuffer = NULL;
			abort();
			return 0;
		}

		if (rawh.sec == 0 && rawh.sec == 0) {
			has_padding = 1;
			offs = (offs + rawh.caplen) % handle->bufSize;
			acks += rawh.caplen;
			printdbg("Padding of length %u at offset %zu (%.2lf %% in the buffer), avail %zu, acks %zu\n", rawh.caplen, header_begin, (100.0 * header_begin) / handle->bufSize,
					 handle->avail, handle->acks + acks);

			_hpcap_advance_rdoff_to(handle, offs); // Make sure that the file offset is up to date.
		} else
			has_padding = 0;

		num_paddings++;
	} while (has_padding && handle->avail >= (handle->acks + acks + RAW_HLEN));

	if (has_padding && handle->avail < (handle->acks + acks + RAW_HLEN)) {
		printerr("No more space at %p for %zu acks + padding\n", auxbuf, acks);
		*pbuffer = NULL;
		abort();
		return 0;
	}

	/* Packet data */
	if (!read_header) {
		if (unlikely(handle->rdoff + RAW_HLEN + rawh.caplen > handle->bufSize)) {
			copy_from_circ_buffer(handle->buf, header_begin, handle->bufSize, auxbuf, RAW_HLEN + rawh.caplen);
			*pbuffer = auxbuf;
		} else
			*pbuffer = (u_char*) handle->buf + header_begin;

		*((u16*)header) = rawh.caplen;
	} else {
		if (unlikely(offs + rawh.caplen > handle->bufSize)) {
			copy_from_circ_buffer(handle->buf, offs, handle->bufSize, auxbuf, rawh.caplen);
			*pbuffer = auxbuf;
		} else
			*pbuffer = ((u_char*)handle->buf) + offs;

		read_header(header, rawh.sec, rawh.nsec, rawh.len, rawh.caplen);
	}


	offs = (offs + rawh.caplen) % handle->bufSize;
	acks += rawh.caplen;

	_hpcap_advance_rdoff_to(handle, offs);
	handle->acks += acks;

	return (((u64)rawh.sec) * ((u64)1000000000ULL) + ((u64)rawh.nsec));
}


uint64_t hpcap_write_block(struct hpcap_handle * handle, int fd, uint64_t max_bytes_to_write)
{
	uint64_t aux = 0, ready = minimo(handle->avail, max_bytes_to_write);

	if (likely(ready >= HPCAP_BS))
		ready = HPCAP_BS;
	else {
		ready = 0;
		goto fin;
	}

	if (likely(fd && (ready > 0))) {
		if (unlikely((handle->rdoff + ready) > handle->bufSize)) {
			printf("Entra en el bloque de escritura multiple\n");

			if (handle->rdoff > handle->bufSize) {
				printf("Error grave en hpcap_write_block (rdoff=%lu, bufsize=%lu)\n", handle->rdoff, handle->bufSize);
				exit(-1);
			}

			aux = handle->bufSize - handle->rdoff;

			if (write(fd, &handle->buf[ handle->rdoff ], aux) != aux) {
				ready = -1;
				printf("Error en escritura 1\n");
				goto fin;
			}

			if (write(fd, handle->buf, ready - aux) != (ready - aux)) {
				ready = -1;
				printf("Error en escritura 2\n");
				goto fin;
			}
		} else {
			if (write(fd, &handle->buf[ handle->rdoff ], ready) != ready) {
				//perror("hpcap_write_block/write error");
				//Comentado para evitar llenar todo el disco de errores
				ready = -1;
				goto fin;
			}
		}
	}

	_hpcap_advance_rdoff_by(handle, ready);
	handle->acks += ready;
fin:
	return ready;
}

int hpcap_map_huge(struct hpcap_handle * handle, const char* hugetlbfs_path, size_t bufsize)
{
	char trailing_slash[2] = { 0, 0 };
	int pagefile_len;
	struct hpcap_buffer_info bufinfo;

	if (handle->hugepage_addr != MAP_FAILED && handle->hugepage_addr != NULL && handle->hugepage_fd > 0) {
		fprintf(stderr, "hpcap_map_huge: Hugepage already mapped\n");
		return HPCAP_ERR;
	}

	handle->hugepage_addr = MAP_FAILED;

	if (hugetlbfs_path[strlen(hugetlbfs_path) - 1] != '/')
		trailing_slash[0] = '/'; /* Ensure the path is slash-terminated */

	/* Create the file name for this buffer */
	pagefile_len = snprintf(handle->hugepage_name, MAX_HUGETLB_FILE_LEN, "%s%shpcap%dq%d_buf",
							hugetlbfs_path, trailing_slash, handle->adapter_idx, handle->queue_idx);

	if (pagefile_len >= MAX_HUGETLB_FILE_LEN) {
		fprintf(stderr, "hpcap_map_huge: hugetlbfs path too long\n");
		return HPCAP_ERR;
	}

	bufinfo.size = bufsize;
	strncpy(bufinfo.file_name, handle->hugepage_name, MAX_HUGETLB_FILE_LEN);

	/* Create the file and get the pointer for the buffer */
	if (_hpcap_mmap_hugetlb(handle, &bufinfo))
		goto error;

	bufinfo.addr = handle->hugepage_addr;

	fprintf(stderr, "hpcap_map_huge: mapped buffer starts at %p\n", bufinfo.addr);

	/* Communicate the buffer to the HPCAP driver */
	if (ioctl(handle->fd, HPCAP_IOC_HUGE_MAP, &bufinfo) < 0) {
		fprintf(stderr, "hpcap_map_huge/ioctl: error communicating assigned buffer to driver: %s\n", strerror(errno));
		goto error;
	}

	return HPCAP_OK;

error:
	/* Undo everything without notifying the driver. */
	hpcap_unmap_huge(handle, 0);

	return HPCAP_ERR;
}

int hpcap_unmap_huge(struct hpcap_handle * handle, short notify_driver)
{
	struct hpcap_buffer_info bufinfo;
	int retval;

	if (handle->hugepage_addr != MAP_FAILED && handle->hugepage_addr != NULL) {
		munmap(handle->hugepage_addr, handle->hugepage_len);
		handle->hugepage_addr = MAP_FAILED;
		fprintf(stderr, "hpcap_unmap_huge: unmapping\n");
	}

	if (handle->hugepage_fd > 0)
		close(handle->hugepage_fd);

	retval = ioctl(handle->fd, HPCAP_IOC_BUFINFO, &bufinfo);

	if (retval < 0)
		perror("ioctl");

	if (retval == 0 && bufinfo.has_hugepages) {
		unlink(bufinfo.file_name);
		fprintf(stderr, "hpcap_unmap_huge: released hugetlb file (%s)\n", handle->hugepage_name);
		handle->hugepage_fd = -1;
	}

	if (notify_driver) {
		if (ioctl(handle->fd, HPCAP_IOC_HUGE_UNMAP) < 0) {
			fprintf(stderr, "hpcap_unmap_huge/ioctl: error notifying driver of huge-buffer unmapping. Stability not guaranteed: %s", strerror(errno));
			return HPCAP_ERR;
		}
	}

	return HPCAP_OK;
}

inline short hpcap_is_header_padding(struct raw_header* header)
{
	return header->nsec == 0 && header->sec == 0;
}

short _hpcap_read_next(struct hpcap_handle* handle, struct raw_header** rawh, uint8_t** frame, uint8_t* for_copy)
{
	size_t copy_buffer_offset = 0;
	size_t frame_size = 0;

	if (handle->avail < RAW_HLEN
		|| handle->avail <= handle->acks
		|| handle->avail - RAW_HLEN < handle->acks)
		return 0;

	if (handle->rdoff + RAW_HLEN > handle->bufSize && for_copy != NULL) {
		copy_from_circ_buffer(handle->buf, handle->rdoff, handle->bufSize, for_copy, RAW_HLEN);
		copy_buffer_offset += RAW_HLEN;
		*rawh = (struct raw_header*) for_copy;
	} else
		*rawh = (struct raw_header*)(handle->buf + handle->rdoff);

	_hpcap_advance_rdoff_by(handle, RAW_HLEN);
	frame_size = (*rawh)->caplen;

	if (frame != NULL && !hpcap_is_header_padding(*rawh)) {
		if (handle->rdoff + frame_size > handle->bufSize && for_copy != NULL) {
			copy_from_circ_buffer(handle->buf, handle->rdoff, handle->bufSize, for_copy + copy_buffer_offset, frame_size);
			*frame = for_copy + copy_buffer_offset;
		} else
			*frame = handle->buf + handle->rdoff;
	}

	_hpcap_advance_rdoff_by(handle, frame_size);
	handle->acks += RAW_HLEN + frame_size;

	return 1;
}
