/**
 * @brief Functions related to the reception of new data
 * and the corresponding poll management.
 */

#include "hpcap_rx.h"
#include "driver_hpcap.h"
#include "hpcap_types.h"
#include "hpcap_debug.h"
#include "hpcap_types.h"
#include "hpcap_listeners.h"
#include "hpcap_rx_profile.h"
#include "hpcap_dups.h"

#include <linux/kthread.h>
#include <linux/sched.h>
#include <asm/atomic.h>

#define CALC_CAPLEN(cap,len) ( (cap==0) ? (len) : (minimo(cap,len)) )
#define previous_consumer(bufp, idx) (((idx) + (bufp)->consumers - 1) % (bufp)->consumers)
#define next_consumer(bufp, idx) (((idx) + 1) % (bufp)->consumers)

#define HPCAP_RX_BUFFER_WRITE 32

#ifdef HPCAP_MLNX
extern void mlx4_en_refill_rx_buffers(struct mlx4_en_priv *priv,
									  struct mlx4_en_rx_ring *ring);

extern void mlx4_en_free_frag(struct mlx4_en_priv *priv,
							  struct mlx4_en_rx_alloc *frags,
							  int i);

extern void mlx4_en_inline_scatter(struct mlx4_en_rx_ring *ring,
								   struct mlx4_en_rx_alloc *frags,
								   struct mlx4_en_rx_desc *rx_desc,
								   struct mlx4_en_priv *priv,
								   unsigned int length);

/** Build the rx_descr_t structure for Mellanox descriptors */
inline rx_descr_t* ring_get_rxd(HW_RING* ring, uint32_t qidx, rx_descr_t* _rxd)
{
	int i;

	i = qidx & ring->size_mask;

	_rxd->cqe = mlx4_en_get_cqe(ring->cq->buf, i, ring->priv->cqe_size) + ring->priv->cqe_factor;
	_rxd->rx_desc = ring->buf + (i << ring->log_stride);
	_rxd->frags = ring->rx_info + (i << ring->priv->log_rx_info);
	_rxd->cq_mask = qidx & ring->cq->size;
	_rxd->fcs_del = ring->fcs_del;

	if (_rxd->cqe->owner_sr_opcode & MLX4_CQE_IS_RECV_MASK)
		mlx4_en_inline_scatter(ring, _rxd->frags, _rxd->rx_desc, ring->priv, rxd_length(_rxd));

	return _rxd;
}

inline uint8_t* ring_get_buffer(rx_descr_t* rx_desc, HW_RING* ring, size_t i)
{
	dma_addr_t dma;

	dma = be64_to_cpu(rx_desc->rx_desc->data[0].addr);

	dma_sync_single_for_cpu(ring->priv->ddev, dma, ring->priv->frag_info[0].frag_size,
							DMA_FROM_DEVICE);
	return page_address(rx_desc->frags[0].page) + rx_desc->frags[0].page_offset;
}

void hpcap_mlx4_release_rx_buffers(HW_RING* ring)
{
	*(ring->cq->mcq.set_ci_db) = cpu_to_be32(ring->cq->mcq.cons_index & 0xffffff); // mlx4_cq_set_ci
	wmb(); /* ensure HW sees CQ consumer before we post new buffers */
	ring->cons = ring->cq->mcq.cons_index;
	// mlx4_en_refill_rx_buffers(ring->priv, ring);
	ring->prod += ((uint32_t)(ring->actual_size + ring->cons - ring->prod));
	*ring->wqres.db.db = cpu_to_be32(ring->prod & 0xffff); // mlx4_en_update_rx_prod_db
	wmb();
}

inline void rxd_get_tstamp(rx_descr_t* rxd, struct timespec* tv, HW_RING* ring)
{
	struct mlx4_en_dev* mdev = ring->priv->mdev;
	unsigned long flags;
	u64 nsec, timestamp;

	timestamp = mlx4_en_get_cqe_ts(rxd->cqe);

	// read_lock_irqsave(&mdev->clock_lock, flags);
	nsec = timecounter_cyc2time(&mdev->clock, timestamp);
	// read_unlock_irqrestore(&mdev->clock_lock, flags);

	*tv = ns_to_timespec(nsec);
}

#endif

#ifdef HPCAP_I40E
static inline void i40e_release_rx_desc(struct i40e_ring *rx_ring, u32 val)
{
	rx_ring->next_to_use = val;
	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();
	writel(val, rx_ring->tail);
}
#endif

static inline uint32_t get_last_descriptor_of_consumer(HW_RING* rx_ring, size_t consumer)
{
	return (((consumer + 1) * rx_ring->bufp->descr_per_consumer - 1) % ring_size(rx_ring));
}

static inline short is_last_descriptor_of_consumer(HW_RING* rx_ring, uint32_t last_read_idx, size_t consumer)
{
	return (last_read_idx % ring_size(rx_ring)) == get_last_descriptor_of_consumer(rx_ring, consumer);
}

static inline short is_first_descriptor_of_consumer(HW_RING* rx_ring, uint32_t qidx, size_t consumer)
{
	return (qidx % ring_size(rx_ring)) == consumer * rx_ring->bufp->descr_per_consumer;
}

static inline size_t rxd_consumer_of(HW_RING* rx_ring, uint32_t rxd_index)
{
	return (rxd_index % ring_size(rx_ring)) / rx_ring->bufp->descr_per_consumer;
}

static inline short rxd_belongs_to_consumer(HW_RING* rx_ring, size_t qidx, size_t consumer)
{
#ifdef HPCAP_CONSUMERS_VIA_RINGS
	return 1; // Each ring belong to one consumer so yes.
#else
	return rxd_consumer_of(rx_ring, qidx) == consumer;
#endif
}

static inline uint32_t rxd_calc_next(HW_RING* rx_ring, uint32_t qidx, size_t consumer)
{
	uint32_t next;
#ifdef DEBUG
	struct hpcap_buf* bufp = rx_ring->bufp;
#endif

#ifdef HPCAP_MLNX
	next = qidx + 1;
#else
	next = (qidx + 1) % ring_size(rx_ring);
#endif

	if (!rxd_belongs_to_consumer(rx_ring, next, consumer)) {
		// If we have surpassed our segment, return the first descriptor in our segment again.
		next += rx_ring->bufp->descr_per_consumer * (rx_ring->bufp->consumers - 1);

#ifndef HPCAP_MLNX
		next = next % ring_size(rx_ring);
#endif

		bufp_dbg(DBG_RXEXTRA, "Consumer %zu RXD increment (current = %zu) out of segment: next one is %zu\n", consumer, qidx, next);
	}

	return next;
}

static inline void hpcap_advance_read_descriptors(HW_RING* rx_ring, uint32_t last_read_idx, struct hpcap_rx_thinfo* thi)
{
	struct hpcap_buf* bufp = rx_ring->bufp;
	size_t consumer = thi->th_index;
	uint32_t next_rxd_idx;

	next_rxd_idx = rxd_calc_next(rx_ring, last_read_idx, thi->th_index);

	bufp_dbg(DBG_RXEXTRA, "Thread %zu called to release buffers until %zu. Is last = %d\n", consumer, last_read_idx, is_last_descriptor_of_consumer(rx_ring, last_read_idx, consumer));

#ifndef HPCAP_CONSUMERS_VIA_RINGS

	// No need to atomic test & set. This thread is the only one reading the freed_last_rdx
	// variable, so there's no benefit in the possible locking/extra instruction of the
	// atomic test & set.
	if (!bufp->can_free[consumer] && atomic_read(&bufp->freed_last_rxd[previous_consumer(bufp, consumer)])) {
		atomic_set(&bufp->freed_last_rxd[previous_consumer(bufp, consumer)], 0);

		// We can only start freeing our descriptors if the previous consumer as freed theirs
		bufp->can_free[consumer] = 1;

		bufp_dbg(DBG_RX, "Thread %zu: last descriptor of previous consumer is released, starting free in our segment\n", thi->th_index);

		//rmb(); /* Ensure we read other data (rx_ring->prod, rx_ring->cons) when the other thread is ready */
	}

#else
	bufp->can_free[consumer] = 1;
#endif

	if (bufp->can_free[consumer]) {
#ifdef HPCAP_MLNX
		bufp_dbg(DBG_RXEXTRA, "Thread %zu: Calling driver release. Prod %u cons %u cons_index %u qidx %u next %u\n",
				 consumer, rx_ring->prod, rx_ring->cons, rx_ring->cq->mcq.cons_index, last_read_idx, next_rxd_idx);
		rx_ring->cq->mcq.cons_index = last_read_idx + 1;
		hpcap_mlx4_release_rx_buffers(rx_ring);
		bufp_dbg(DBG_RXEXTRA, "Thread %zu: After driver release. Prod %u cons %u cons_index %u qidx %u next %u\n",
				 consumer, rx_ring->prod, rx_ring->cons, rx_ring->cq->mcq.cons_index, last_read_idx, next_rxd_idx);

#else
		rx_ring->queued = (last_read_idx + 1) % ring_size(rx_ring);
		rx_ring->next_to_clean = (last_read_idx + 1) % ring_size(rx_ring);

#ifdef HPCAP_I40E
		// TODO: Use bitmask
		rx_ring->next_to_use = (last_read_idx / 8) * 8; // The XL710 only supports bumps of multiples of 8 descriptors.
#else
		rx_ring->next_to_use = last_read_idx;
#endif

		ring_rxd_release(rx_ring, rx_ring->next_to_use);
		bufp_dbg(DBG_RXEXTRA, "Thread %zu: after release, next idx %u, next_to_clean %d, next_to_use %d\n", consumer, next_rxd_idx, rx_ring->next_to_clean, rx_ring->next_to_use);
#endif

		bufp_dbg(DBG_RXEXTRA, "Thread %zu: next rxd to read is %u\n", consumer, thi->rxd_idx);

		hpcap_profile_mark_rxd_return(&thi->prof, last_read_idx);

#ifndef HPCAP_CONSUMERS_VIA_RINGS

		if (is_last_descriptor_of_consumer(rx_ring, last_read_idx, consumer)) {
			/**
			 * We have released all of our descriptors, mark it so the next consumer
			 * can free theirs.
			 *
			 * Do a write barrier before to ensure correct ordering of operations.
			 */
			wmb();
			atomic_set(&bufp->freed_last_rxd[consumer], 1);
			bufp->can_free[consumer] = 0;

			bufp_dbg(DBG_RX, "Thread %zu: freed last descriptor in the segment\n", consumer);
		}

#endif
	}

	thi->rxd_idx = next_rxd_idx;
}

#ifdef DO_PREFETCH
static inline void cache_warmup(HW_RING *rx_ring, uint32_t qidx, void *dst, u64 dst_size, u64 dst_offset)
{
#ifdef HPCAP_MLNX
	int i;
	uint8_t* cqe;

	i = qidx & rx_ring->size_mask;
	cqe = mlx4_en_get_cqe(rx_ring->cq->buf, i, rx_ring->priv->cqe_size) + rx_ring->priv->cqe_factor;

	prefetchnta(cqe);
	prefetchnta(cqe + rx_ring->priv->cqe_size);
#endif
}
#endif

static inline short passes_filter(struct hpcap_buf* bufp, struct frame_descriptor* fd)
{
	size_t i;

	if (!atomic_read(&bufp->enabled_filter))
		return 1;

	for (i = 0; i < bufp->filter_count; i++) {
		// Ensure we can use this filter.
		if (bufp->filter_starts[i] + bufp->filter_lengths[i] > fd->size) {
			if (!bufp->filter_reject_on_match[i])
				return 0;  // If the filter needs to be matched and there's not enough length, stop.
			else
				continue;
		}

		if (memcmp(fd->pointer[0] + bufp->filter_starts[i], bufp->filter_strings[i], bufp->filter_lengths[i]) == 0) {
			if (bufp->filter_reject_on_match[i])
				return 0;
		} else
			return 0;
	}

	return 1;
}

#ifdef BUF_DEBUG
static inline size_t set_circular_buffer_to(void *dst, u64 dst_size, u64 dst_offset, char c, u32 nbytes)
{
	u64 aux;

	if ((dst_offset + nbytes) > dst_size) {
		aux = dst_size - dst_offset;
		memset(dst + dst_offset, c, aux);
		memset(dst, c, nbytes - aux);
	} else
		memset(dst + dst_offset, c, nbytes);

	return nbytes;
}
#endif

static inline size_t copy_to_circular_buffer(void *dst, size_t dst_size, size_t dst_offset, void *src, size_t nbytes)
{
	size_t new_offset;
	size_t aux;

	printdbg(DBG_RXEXTRA, "Copy to buffer 0x%p (size %zu) from 0x%p (size %zu) at offset %zu\n",
			 dst, dst_size, src, nbytes, dst_offset);

	if ((dst_offset + nbytes) > dst_size) {
		aux = dst_size - dst_offset;
		memcpy(dst + dst_offset, src, aux);
		memcpy(dst, &(((u8 *)src)[aux]), nbytes - aux);

		new_offset = (dst_offset + nbytes) % dst_size;
	} else {
		memcpy(dst + dst_offset, src, nbytes);
		new_offset = (dst_offset + nbytes) % dst_size;
	}

	return new_offset;
}

static inline short can_lock_ring(HW_RING* rx_ring, uint32_t rxd_to_read, size_t consumer)
{
	struct hpcap_buf* bufp = rx_ring->bufp;

	if (!bufp->can_free[consumer] && !atomic_read(&bufp->freed_last_rxd[previous_consumer(bufp, consumer)]))
		if (get_last_descriptor_of_consumer(rx_ring, consumer) - rxd_to_read <= 8)
			return 1;

	return 0;
}


static inline int check4packet(HW_RING *rx_ring, rxd_idx_t *qidx, char *dst_buf, struct frame_descriptor *fd, size_t consumer)
{
	rx_descr_t* rx_desc;
	uint8_t *buffer;
	struct hpcap_buf* bufp = rx_ring->bufp;

	fd->parts        = 0;
	fd->size         = 0;

#ifdef JUMBO

	do {
#endif
#ifdef HPCAP_MLNX
		rx_desc = ring_get_rxd(rx_ring, (*qidx), fd->_rxd + fd->parts);
#else
		rx_desc = ring_get_rxd(rx_ring, (*qidx));
#endif

		buffer = ring_get_buffer(rx_desc, rx_ring, (*qidx));
		fd->pointer[fd->parts] = buffer;
		fd->rx_desc[fd->parts] = rx_desc;

#ifdef DO_PREFETCH
		prefetchnta(buffer + MAX_DESCR_SIZE * 4);
		prefetchnta(rx_desc + 2);
#endif

#ifdef HPCAP_I40E

		if (can_lock_ring(rx_ring, *qidx, consumer))
			return 0;

#endif

		if (unlikely(!rxd_has_data(rx_desc)))
			return 0;

		printdbg(DBG_RXEXTRA, "Found data in descriptor %zu\n", *qidx);

		fd->size += rxd_length(rx_desc);
		fd->parts++;

		*qidx = rxd_calc_next(rx_ring, *qidx, consumer);

		if (!dst_buf)
			return -1;

#ifdef JUMBO

		if (rxd_is_jumbo(rx_desc))
			printk(KERN_INFO "[HPCAP] a jumboframe appeared (len: %zu)\n", rxd_length(rx_desc));

#endif

#ifdef REMOVE_DUPS

		if (fd->parts == 1)
			fd->rss_hash = rxd_hash(rx_desc);

#endif

#ifdef RX_DEBUG

		if (unlikely(rxd_has_error(rx_desc))) {
			printk(KERN_INFO "found error frames\n");
			return -1;
		}

#endif

#ifdef JUMBO
	} while (unlikely(rxd_is_jumbo(rx_desc)));

	if (fd->parts > 1)
		printk(KERN_INFO "[HPCAP] LAST descriptor - %d BYTES (%d fragments)\n", len, jd->parts);

#endif

	return fd->size;
}

static inline size_t set_padding(uint8_t* dst_buf, size_t bufsize, size_t offset, size_t padlen)
{
	/******************************************
	 Packet format in the RAW stream:
	   ... | Seconds 32b | Nanosec 32b | Capframelen 16b | Length 16b | ... data ... |
	 NOTE:
		if( secs==0 && nsecs==0 ) ==> there is a padding block of 'length' bytes
	******************************************/
	struct raw_header rawh;

	rawh.sec    = 0;
	rawh.nsec   = 0;
	rawh.caplen = padlen - RAW_HLEN; //for padding, it is a special case
	rawh.len    = padlen - RAW_HLEN; //for padding, it is a special case

	printdbg(DBG_RXEXTRA, "Setting padding in 0x%p (size %zu): offset %zu, length %zu\n",
			 dst_buf, bufsize, offset, padlen);

	if (padlen < RAW_HLEN)
		return offset;

	// Write the padding header into the buffer
	offset = copy_to_circular_buffer(dst_buf, bufsize, offset, &rawh, RAW_HLEN);

#ifdef BUF_DEBUG // In debug mode, fill the padding data with zeros.

	if (padlen > 0)
		set_circular_buffer_to(dst_buf, bufsize, offset, 0, padlen);

#endif

	offset = (offset + padlen) % bufsize;

	return offset;
}

// Macros to convert a offset to the corresponding offset inside the buffer or inside the file.
#define as_buffer_offset(offset) ((offset) % bufsize)
#define as_file_offset(offset) ((offset) % HPCAP_FILESIZE)

uint64_t hpcap_rx(HW_RING *rx_ring, size_t limit, uint8_t *dst_buf, struct hpcap_rx_thinfo* thi)
{
	size_t fraglen, capl;
	u64 cnt;
	struct timespec tv;
	struct raw_header rawh;
	size_t read_descriptors = 0;
	int i;
	int ret;
	struct frame_descriptor fd;
	struct hpcap_buf *bufp = rx_ring->bufp;
	size_t bufsize = bufp->bufSize;
	atomic_t* wr_offset = &bufp->consumer_write_off;
	atomic_t* rd_offset = &bufp->consumer_read_off;
	size_t available, to_write;
	size_t offset, offset_dst, file_final_offset, file_dst_offset, buffer_dst_offset = 0;
	size_t padding_end, padding_begin;
	size_t caplen = adapters[bufp->adapter]->caplen;
	size_t padlen;
	size_t padlen_noheader;
	short owns_next_rxd = 1;
	short out_of_space = 0;

#ifdef REMOVE_DUPS
	struct hpcap_dup_info** duptable = bufp->dupTable;
#endif

	rxd_idx_t next_qidx  	= thi->rxd_idx;
	rxd_idx_t qidx    		= 0;
	rxd_idx_t last_read_idx = 0;

	size_t total_rx_packets  = 0;
	size_t total_rx_bytes    = 0;
	HW_ADAPTER *adapter   = adapters[bufp->adapter];

#ifdef REMOVE_DUPS
	u32 total_dup_packets = 0;
#endif

#ifdef RX_DEBUG
	int r_idx             = rx_ring->reg_idx;
#endif

	for (cnt = 0, qidx = next_qidx;             // We have not received anything. Start by next_to_clean ring
		 cnt < limit && !out_of_space;          // While our buffer presents more free space
		 cnt += fd.size, qidx = next_qidx) {    // Increments the total number of bytes received and the ring

#ifdef DO_PREFETCH
		// If we have to prefetch the data, use the source (NIC ring) and the current buffer specify
		// by upper levels
		cache_warmup(rx_ring, qidx, dst_buf, bufsize, atomic_read(offset) % bufsize);
#endif

		bufp_dbg(DBG_RXEXTRA, "Thread %zu is checking rxd %lu\n", thi->th_index, qidx);

		if ((ret = check4packet(rx_ring, &next_qidx, dst_buf, &fd, thi->th_index)) == 0) {
			// We have consumed all the packets in the card. Do not update qidx.
			owns_next_rxd = !is_first_descriptor_of_consumer(rx_ring, qidx, thi->th_index);
			break;
		}

		last_read_idx = qidx;
		read_descriptors++;

		total_rx_packets++;
		total_rx_bytes += fd.size;

		if (!passes_filter(bufp, &fd))
			goto ignore;

#ifdef HPCAP_HWTSTAMP
		rxd_get_tstamp(fd.rx_desc[0], &tv, rx_ring);
#else
		getnstimeofday(&tv);
#endif

#ifdef HPCAP_MEASURE_LATENCY
		hpcap_latency_measure(&thi->lm, &tv, fd.pointer[0], fd.size);
#endif

		if (ret < 0) {
			// We do not own a buffer to write the data. Ignore it.
			adapter->hpcap_client_discard++;
			goto ignore;
		}


		bufp_dbg(DBG_RX, "Thread %zu found data in frame %d, length %llu\n", thi->th_index, qidx, fd.size);

#ifdef REMOVE_DUPS

		if (duptable && hpcap_check_duplicate(&fd, &tv, duptable)) {
			total_dup_packets++;
			goto ignore;
		}

#endif

#ifdef DEBUG
		hpcap_print_listener_status(&bufp->lstnr.global);
#endif
		// Update the size: minimum between current length and caplen (input argument)
		capl = CALC_CAPLEN(caplen, fd.size);
		to_write = capl + RAW_HLEN;

		do {
			padlen = 0;

			available = limit - cnt;

			if (available < to_write + RAW_HLEN) {
				// No space available. Discard this frame, finish this RX loop.
				out_of_space = 1;
				adapter->hpcap_client_loss++;
				goto ignore;
			}

			/**
			 * Allocate space for the frame in the buffer.
			 * We trust on the usual unsigned overflow to do the modulo for us.
			 * This requires that the HPCAP_FILESIZE and the buffer size divide the maximum
			 * value of the integer (4GB).
			 */
			offset = atomic_add_return(to_write, wr_offset); // offset is now to_write + wr_offset.
			offset_dst = offset - to_write; // Calculate the original value (before the add) of offset.

			/**
			 * Update the offset in the file (for the padding check) and in the buffer (for the write)
			 * It's necessary to do it this way, because we don't know whether the file is bigger than
			 * the buffer of viceversa.
			 */
			file_final_offset = as_file_offset(offset);
			file_dst_offset = as_file_offset(offset_dst);
			buffer_dst_offset = as_buffer_offset(offset_dst);

			cnt += to_write;

			if (unlikely(file_dst_offset + to_write + RAW_HLEN > HPCAP_FILESIZE)) {
				bufp_dbg(DBG_RX, "Thread %zu overwriting the file (file_dst_offset = %zu, %zu to write). Write padding\n",
						 thi->th_index, file_dst_offset, to_write);

				/**
				 * We need padding: there's no space in the file for this frame and an additional
				 * RAW header that would mark the padding.
				 *
				 * Note that if HPCAP_FILESIZE - file_dst_offset is less than RAW_HLEN, set_padding
				 * will not set any padding. In this case, we will only write the padding at the
				 * beginning of the file.
				 */
				padlen = HPCAP_FILESIZE - file_dst_offset;

				// Set first the padding to fill the space until the end of the file
				set_padding(dst_buf, bufsize, buffer_dst_offset, padlen);

				/**
				 * If possible, fill the beginning of the file with padding too.
				 * Remember to calculate correctly the buffer offset, from offset_dst + padlen, because
				 * if the filesize is less than the buffer, the padding may not go to the buffer offset 0.
				 */
				if (file_final_offset >= RAW_HLEN)
					set_padding(dst_buf, bufsize, as_buffer_offset(offset_dst + padlen), file_final_offset);

			} else if (unlikely(file_dst_offset > 0 && file_dst_offset < RAW_HLEN)) {
				bufp_dbg(DBG_RX, "Thread %zu writing padding of previous write (file_dst_offset = %zu)\n", thi->th_index, file_dst_offset);

				/**
				 * The previous frame required padding, but it couldn't fill it at the
				 * beginning of the file. Fill it now from the start of the file until
				 * the position we reserved.
				 */
				padlen = as_buffer_offset(offset_dst + file_final_offset);
				set_padding(dst_buf, bufsize, as_buffer_offset(offset_dst - file_dst_offset), padlen);
			}
		} while (unlikely(padlen > 0)); // Repeat to allocate space for the frame

		bufp_dbg(DBG_RXEXTRA, "Received frame of length %llu (caplen %zu), write to 0x%p + %zu (offset in file is %zu)\n",
				 fd.size, capl, dst_buf, buffer_dst_offset, file_dst_offset);

		// Every time that there is a packet: write the header into the buffer
		rawh.sec    = tv.tv_sec;
		rawh.nsec   = tv.tv_nsec;
		rawh.caplen = capl;
		rawh.len    = fd.size;

		buffer_dst_offset = copy_to_circular_buffer(dst_buf, bufsize, buffer_dst_offset, &rawh, RAW_HLEN);

		// write the payload into the buffer
#ifdef JUMBO

		for (i = 0; (i < fd.parts) && (capl > 0); i++) {
#else
		i = 0;
#endif
			fraglen = minimo(capl, MAX_DESCR_SIZE);
			offset  = copy_to_circular_buffer(dst_buf, bufsize, buffer_dst_offset, fd.pointer[i], fraglen);
#ifdef JUMBO
			capl -= fraglen;
		}

#endif

ignore:

#ifndef HPCAP_MLNX
		fd.rx_desc[0]->read.pkt_addr = fd.rx_desc[0]->read.hdr_addr = cpu_to_le64(packet_dma(rx_ring, qidx));

#ifdef HPCAP_I40E
		fd.rx_desc[0]->wb.qword1.status_error_len = 0;
#endif
#else

		// for (i = 0; i < rx_ring->priv->num_frags; i++)
		// 	mlx4_en_free_frag(rx_ring->priv, fd.rx_desc[0]->frags, i);

#endif

		/**
		 * Return some buffers to hardware, one at a time is too slow.
		 * Also, make sure that we always return the last descriptor so other consumers can return theirs.
		 */
		if (read_descriptors % HPCAP_RX_BUFFER_WRITE == 0 || is_last_descriptor_of_consumer(rx_ring, last_read_idx, thi->th_index)) {
			bufp_dbg(DBG_RXEXTRA, "Thread %zu returning descriptor block\n", thi->th_index);
			hpcap_advance_read_descriptors(rx_ring, last_read_idx, thi);
		}
	}

	if (likely(read_descriptors > 0))
	{
		bufp_dbg(DBG_RXEXTRA, "Thread %zu RX loop finished, returning the %zu descriptors read (qidx = %lu)\n",
				 thi->th_index, read_descriptors, (unsigned long) last_read_idx);
		hpcap_advance_read_descriptors(rx_ring, last_read_idx, thi);
	}

#ifdef HPCAP_MLNX
	// mlx4_en_arm_cq(rx_ring->priv, rx_ring->cq);
#endif

#ifdef REMOVE_DUPS

	if (likely(total_rx_packets > 0 || total_dup_packets > 0))
#else
	if (likely(total_rx_packets > 0))
#endif
	{
#ifndef HPCAP_MLNX
		rx_ring->stats.packets += total_rx_packets;
		rx_ring->stats.bytes += total_rx_bytes;
		rx_ring->total_packets += total_rx_packets;
		rx_ring->total_bytes += total_rx_bytes;
#else
		rx_ring->packets += total_rx_packets;
		rx_ring->bytes += total_rx_bytes;
#endif

#ifdef REMOVE_DUPS
		adapter->total_dup_frames += total_dup_packets;
#endif
	}

	hpcap_profile_mark_batch(&thi->prof, cnt, total_rx_packets, read_descriptors, owns_next_rxd, cnt >= limit || out_of_space);

	return cnt;
}

extern HW_ADAPTER * adapters[HPCAP_MAX_NIC];

int hpcap_poll(void *arg)
{
	struct hpcap_rx_thinfo* thinfo = arg;
	HW_RING *rx_ring = thinfo->rx_ring;
	struct hpcap_buf *bufp = rx_ring->bufp;
	u64 retval = 0;
	size_t avail = 0;
	size_t limit;
	uint8_t *rxbuf = NULL;
	size_t num_list;
	size_t new_bytes, new_offset;
	size_t batch = 0, sleep_each_batches = 50000000;
	size_t bufsize = bufp->bufSize;
	size_t i;

#ifdef DEBUG_SLOWDOWN
	sleep_each_batches = 0;
#endif

	//set_current_state(TASK_UNINTERRUPTIBLE);//new
	HPRINTK(INFO, "Poll thread %zu start.\n", thinfo->th_index);

	while (!kthread_should_stop()) {
		num_list = hpcap_listener_count(&bufp->lstnr);

		if (unlikely(num_list <= 0)) {
			rxbuf = NULL;
			hpcap_global_listener_reset_offset(&bufp->lstnr);
			hpcap_reset_buffer_offsets(bufp);
		} else
			rxbuf = bufp->bufferCopia;

		if (bufp->lstnr.global.bufferWrOffset < 0 || bufp->lstnr.global.bufferWrOffset >= bufp->bufSize)
			HPRINTK(WARNING, "Wrong value for bufferWrOffset: %zu\n", bufp->lstnr.global.bufferWrOffset);

		avail = avail_bytes(&bufp->lstnr.global);
		limit = avail / bufp->consumers;

		retval = hpcap_rx(rx_ring, limit, rxbuf, thinfo);

		// Notify the kernel that this thread is active and not locked.
		touch_softlockup_watchdog();

		if (retval > avail)
			bufp_dbg(DBG_RX, "Warning: Overreading %llu bytes. avail=%zu, BUF=%llu, bufcount=%zu)\n", retval, avail, bufp->bufSize, used_bytes(&bufp->lstnr.global));


		// Only sleep after a certain amount of reception calls, to avoid losing frames.
		if (retval == 0 && batch >= sleep_each_batches) {
			hpcap_profile_mark_schedtimeout_start(&thinfo->prof);
#ifdef DEBUG_SLOWDOWN

			// Sleep a lot so the debug output is readable
			for (i = 0; i < 1000000 && !kthread_should_stop(); i++)
				schedule_timeout(ns(200));

#else
			schedule_timeout(ns(50));
#endif
			hpcap_profile_mark_schedtimeout_end(&thinfo->prof);

			batch = 0;
		}

#ifdef HPCAP_MEASURE_LATENCY

		if (thinfo->lm.measurements >= 100000)
			hpcap_latency_print(&thinfo->lm, 1);

#endif

		batch++;

		hpcap_profile_try_print(&thinfo->prof, thinfo);

		// Only the first thread updates the pointers.
		if (thinfo->th_index == 0) {

#if MAX_LISTENERS > 1
			/* Update RdPointer according to the slowest listener */
			hpcap_pop_global_listener(&bufp->lstnr);
#endif

			new_offset = as_buffer_offset(atomic_read(&bufp->consumer_write_off));
			new_bytes = distance(bufp->lstnr.global.bufferWrOffset, new_offset, bufp->bufSize);

			if (new_bytes > 0) {
				bufp_dbg(DBG_RX, "Thread 0 pushing listeners: offset %zu -> %zu, %zu new bytes\n",
						 bufp->lstnr.global.bufferWrOffset, new_offset, new_bytes);

				hpcap_push_all_listeners(&bufp->lstnr, new_bytes);
			}

			bufp_dbg(DBG_RXEXTRA, "Thread 0 updating read offset: %d -> %zu\n",
					 atomic_read(&bufp->consumer_read_off), bufp->lstnr.global.bufferRdOffset);

			atomic_set(&bufp->consumer_read_off, bufp->lstnr.global.bufferRdOffset);
		}
	}

	HPRINTK(INFO, "Poll thread %zu stop.\n", thinfo->th_index);

	return 0;
}

int hpcap_stop_poll_threads(HW_ADAPTER * adapter)
{
	size_t i, j;

	adapter_dbg(DBG_NET, "Stopping polling threads\n");

	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct hpcap_buf *bufp = adapter->rx_ring[i]->bufp;

		if (bufp == NULL) {
			adapter_dbg(DBG_NET, "HPCAP structures not initialized.\n");
			continue;
		}

		if (atomic_xchg(&bufp->created, 0) == 1) {
			adapter_dbg(DBG_NET, "Sent stop request to RXQ %zu\n", i);

			for (j = 0; j < adapter->consumers; j++)
				kthread_stop(bufp->consumer_threads[j]);

			HPRINTK(INFO, "Polling threads stopped\n");
		}
	}

	return 0;
}

int hpcap_launch_poll_threads(HW_ADAPTER * adapter)
{
	size_t i, j;
	struct hpcap_rx_thinfo* thinfo;
	struct hpcap_buf  *bufp;
	size_t core_count = 0;
	size_t starting_consumer;
	uint32_t first_rxd;

	adapter_dbg(DBG_NET, "Preparing to launch poll threads");

	if (adapter == NULL) {
		BPRINTK(WARNING, "hpcap_launch_poll_threads received a null adapter ¿?");
		return 0;
	}

	hpcap_check_naming(adapter);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter_dbg(DBG_NET, "Configuring queue %zu out of %d total\n", i, adapter->num_rx_queues);

		bufp = adapter->rx_ring[i]->bufp;

		if (!bufp) {
			adapter_dbg(DBG_NET, "This adapter has not been registered yet with HPCAP. Cannot launch poll thread.\n");
			return 0;
		}

#ifdef DEBUG_SLOWDOWN
		HPRINTK(WARNING, "DEBUG SLOWDOWN ENABLED\n");
#endif

		// Set to 1, and return the previous value
		if (atomic_xchg(&bufp->created, 1) == 0) {

#ifdef BUF_DEBUG
			memset(bufp->bufferCopia, 0, bufp->bufSize);
#endif

#ifdef HPCAP_CONSUMERS_VIA_RINGS
			bufp->consumers = 1;
#else
			bufp->consumers = adapter->consumers;
#endif
			bufp->descr_per_consumer = ring_size(adapter->rx_ring[i]) / bufp->consumers;

			adapter_dbg(DBG_NET, "Poll threads not created on rxq %zu, starting %zu with %zu descriptors each\n", i, adapter->consumers, bufp->descr_per_consumer);

			first_rxd = ring_get_next_rxd(adapter->rx_ring[i]);
			starting_consumer = rxd_consumer_of(adapter->rx_ring[i], first_rxd);

			hpcap_reset_buffer_offsets(bufp);

			for (j = 0; j < adapter->consumers; j++) {
				thinfo = &bufp->consumers_thinfo[j];
				thinfo->th_index = j;
				thinfo->write_offset = &bufp->consumer_write_off;
				thinfo->read_offset = &bufp->consumer_read_off;
				atomic_set(&bufp->freed_last_rxd[j], 0);

#ifdef HPCAP_CONSUMERS_VIA_RINGS
				// In this case, each consumer gets assigned a different ring.
				thinfo->rx_ring = adapter->rx_ring[i + j];
				thinfo->rxd_idx = ring_get_next_rxd(thinfo->rx_ring);
				bufp->can_free[j] = 1;
#else
				thinfo->rx_ring = adapter->rx_ring[i];

#ifdef HPCAP_MEASURE_LATENCY
				hpcap_latency_init(&thinfo->lm);
#endif

				if (j == starting_consumer) {
					/**
					 * If the next rxd to read is in the consumer's segment, set that one as the
					 * first descriptor of this thread. Also, allow it to free its descriptors.
					 */
					thinfo->rxd_idx = first_rxd;
					bufp->can_free[j] = 1;
				} else {
					/**
					 * Else, next to read is the first in the segment, and this thread should
					 * wait to be signaled for freeing its segments.
					 */
					thinfo->rxd_idx = j * bufp->descr_per_consumer;
					bufp->can_free[j] = 0;
				}

#endif

				adapter_dbg(DBG_NET, "Preparing to launch thread %zu, first rxd is %lu, starting consumer? %d\n", j, first_rxd, starting_consumer == j);

				bufp->consumer_threads[j] = kthread_create(hpcap_poll, (void *) thinfo, bufp->name);

				if (bufp->consumer_threads[j] == NULL || bufp->consumer_threads[j] == ERR_PTR(-ENOMEM)) {
					HPRINTK(ERR, "Thread %zu could not be launched! Please restart the driver (and brace for a crash)", j);
					return 0;
				} else {
					kthread_bind(bufp->consumer_threads[j], adapter->core + core_count);
					wake_up_process(bufp->consumer_threads[j]);
				}

				core_count++;
			}
		}
	}

	return 0;
}

void hpcap_reset_buffer_offsets(struct hpcap_buf* bufp)
{
	atomic_set(&bufp->consumer_read_off, 0);
	atomic_set(&bufp->consumer_write_off, 0);
}
