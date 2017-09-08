#include "hpcap_rx_profile.h"
#include "hpcap_debug.h"

#include <linux/jiffies.h>

#ifdef HPCAP_PROFILING

void hpcap_profile_reset(struct hpcap_profile* prof)
{
	memset(prof, 0, sizeof(struct hpcap_profile));
}

void hpcap_profile_mark_batch(struct hpcap_profile* prof, size_t bytes, size_t frames, size_t descriptors, short owns_next_rxd, short budget_exhausted)
{
	prof->bytes += bytes;
	prof->frames += frames;
	prof->descriptors += descriptors;

	if (budget_exhausted)
		prof->budgets_exhausted++;

	if (descriptors == 0) {
		if (!owns_next_rxd)
			prof->times_rxd_not_owned++;
		else
			prof->times_ring_empty++;

		prof->strikes++;
		prof->strike_duration += prof->_current_strike;
		prof->_current_strike = 0;
	} else
		prof->_current_strike++;

	prof->samples++;
}

void hpcap_profile_mark_rxd_return(struct hpcap_profile* prof, size_t rxd_index)
{
	if (prof->_rxd_marked) {
		prof->num_releases++;

		if (rxd_index > prof->_prev_rxd)
			prof->released_descriptors += (rxd_index - prof->_prev_rxd - 1);
		else
			prof->released_descriptors += (prof->_prev_rxd - rxd_index - 1);
	}

	prof->_prev_rxd = rxd_index;
	prof->_rxd_marked = 1;
}

void hpcap_profile_mark_schedtimeout_start(struct hpcap_profile* prof)
{
	prof->_prev_jiffies = jiffies;
}

void hpcap_profile_mark_schedtimeout_end(struct hpcap_profile* prof)
{
	uint64_t elapsed = jiffies - prof->_prev_jiffies;

	prof->timeout_jiffies += elapsed;
	prof->sleeps++;
}

void hpcap_profile_try_print(struct hpcap_profile* prof, struct hpcap_rx_thinfo* thi)
{
	struct hpcap_buf* bufp = thi->rx_ring->bufp;

	if (!bufp->has_printed_profile_help) {
		// Double documentation!
		printk(KERN_INFO PFX "HPCAP profiling enabled. Will print several stats for each period: \n");
		printk(KERN_INFO PFX "- B: Bytes received\n");
		printk(KERN_INFO PFX "- F: Frames received\n");
		printk(KERN_INFO PFX "- RXD: Descriptors read (usually the same as F, except if we have jumbo frames or something's wrong)\n");
		printk(KERN_INFO PFX "- EMPTY: How many times we went to check the ring and it was empty\n");
		printk(KERN_INFO PFX "- NOWN: How many times we went to check the ring the next descriptor to read was not owned by the current consumer\n");
		printk(KERN_INFO PFX "- EXHS: How many times the RX loop exited because the limit of bytes to write in the buffer was reached\n");
		printk(KERN_INFO PFX "- RXD-REL: Number of descriptors released to the NIC\n");
		printk(KERN_INFO PFX "- REL: Number of times a release operation (of multiple descriptor) was done\n");
		printk(KERN_INFO PFX "- SNS: Number of times the loop went to sleep (no packets to read) and average sleep time (usecs)\n");
		printk(KERN_INFO PFX "- STR: Average cycles of a receive streak (loops without the RX thread going to sleep)\n");
		bufp->has_printed_profile_help = 1;
	}

	if (prof->samples >= PROFILE_SAMPLES) {
		printk(KERN_INFO PFX "hpcap%dq%d: [PROF C%zu] B %llu | F %llu | RXD %llu | EMPTY %llu | NOWN %llu | EXHS %llu | RXD-REL %llu | REL %llu | SNS %llu/%llu | STR %llu/%llu\n",
			   bufp->adapter, bufp->queue,
			   thi->th_index,
			   prof->bytes, prof->frames, prof->descriptors,
			   prof->times_ring_empty, prof->times_rxd_not_owned,
			   prof->budgets_exhausted,
			   prof->released_descriptors, prof->num_releases,
			   prof->sleeps,
			   NSEC_PER_USEC * jiffies_to_usecs(prof->timeout_jiffies) / prof->sleeps,
			   prof->strike_duration / prof->strikes, prof->strikes);

#ifndef HPCAP_MLNX

		if (prof->released_descriptors == 0) {
			printk(KERN_INFO PFX "hpcap%dq%d: [C%zu] rxd_idx = %u, wr_offset = %u, rd_offset = %u, can_free = %u, freed_last = %u, tail = %lu, next_to_{clean,use} = %u,%u\n",
				   bufp->adapter, bufp->queue,
				   thi->th_index, thi->rxd_idx, atomic_read(thi->write_offset), atomic_read(thi->read_offset),
				   bufp->can_free[thi->th_index], atomic_read(&bufp->freed_last_rxd[thi->th_index]),
				   readl(thi->rx_ring->tail), thi->rx_ring->next_to_clean, thi->rx_ring->next_to_use);
		}

#endif

		hpcap_profile_reset(prof);

	}
}

#endif
