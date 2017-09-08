#ifndef HPCAP_LATENCY_H
#define HPCAP_LATENCY_H

#include <linux/types.h>
#include <linux/time.h>

struct hpcap_latency_measurements {
	long long variance;
	long long mean_sum;
	long long square_sum;
	long long mean;
	long long max;
	long long min;
	long long measurements;
	long long discard;
	uint64_t prev_recv_tstamp;
	uint64_t prev_send_tstamp;
};

void hpcap_latency_init(struct hpcap_latency_measurements* lm);
void hpcap_latency_measure(struct hpcap_latency_measurements* lm, struct timespec* tv, uint8_t* frame, size_t frame_size);
void hpcap_latency_print(struct hpcap_latency_measurements* lm, short reset);

#endif
