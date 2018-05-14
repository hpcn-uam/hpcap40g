#include <linux/kernel.h>

#include "hpcap_latency.h"
#include "hpcap_debug.h"

void hpcap_latency_init(struct hpcap_latency_measurements* lm)
{
	lm->mean = 0;
	lm->variance = 0;
	lm->measurements = 0;
	lm->mean_sum = 0;
	lm->square_sum = 0;
	lm->max = 0;
	lm->min = LONG_MAX;
	lm->discard = 0;
	lm->prev_recv_tstamp = 0;
	lm->prev_send_tstamp = 0;
}

void hpcap_latency_measure(struct hpcap_latency_measurements* lm, struct timespec* tv, uint8_t* frame, size_t frame_size)
{
	long int diff;
	uint16_t tsPacketId;
	uint64_t send_tstamp;
	uint64_t recv_tstamp;
	long long interarrival_send, interarrival_recv;

	tsPacketId = *((uint16_t*)(frame + 36));

	if (tsPacketId != 0xCACA)
		return;

	if (lm->measurements == 0) {
		lm->discard++;

		if (lm->discard <= 1000)
			return;
	}

	send_tstamp = *((uint64_t*)(frame + 40)) * 10;
	recv_tstamp = tv->tv_sec * 1000000000 + tv->tv_nsec;

	lm->measurements++;

	if (lm->measurements > 0) {
		interarrival_recv = recv_tstamp - lm->prev_recv_tstamp;
		interarrival_send = send_tstamp - lm->prev_send_tstamp;

		diff = interarrival_recv - interarrival_send;

		lm->mean_sum += diff;
		lm->mean = lm->mean_sum / lm->measurements;

		lm->square_sum += diff * diff;
		lm->variance = lm->square_sum / lm->measurements - lm->mean * lm->mean;

		if (diff > lm->max)
			lm->max = diff;

		if (diff < lm->min)
			lm->min = diff;
	}

	lm->prev_recv_tstamp = recv_tstamp;
	lm->prev_send_tstamp = send_tstamp;
}

void hpcap_latency_print(struct hpcap_latency_measurements* lm, short reset)
{
	BPRINTK(INFO, "%lld frames: mean latency %lld ns sum %lld, stdev %lu ns (max %lld, min %lld)\n", lm->measurements, lm->mean, lm->mean_sum, int_sqrt(lm->variance), lm->max, lm->min);

	if (reset)
		hpcap_latency_init(lm);
}
