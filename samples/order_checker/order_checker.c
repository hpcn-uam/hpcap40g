#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <assert.h>

#include "hpcap.h"

volatile sig_atomic_t stop = 0;

void stop_signal(int sign)
{
	stop = 1;
}

int main(int argc, char **argv)
{
	struct hpcap_handle hp;
	int ret = 0;
	int ifindex = 0, qindex = 0;
	struct raw_header* raw_hdr;
	size_t frame_count = 0;
	double prev_ts = 0, ts;
	size_t unordered = 0;

	uint16_t caplen = 0;
	u_char *bp = NULL;
	u_char auxbuf[RAW_HLEN + MAX_PACKET_SIZE];

	if (argc != 3) {
		printf("Uso: %s <adapter index> <queue index>\n", argv[0]);
		return HPCAP_ERR;
	}

	/* Creating HPCAP handle */
	ifindex = atoi(argv[1]);
	qindex = atoi(argv[2]);
	ret = hpcap_open(&hp, ifindex, qindex);

	if (ret != HPCAP_OK) {
		printf("Error when opening the HPCAP handle\n");
		hpcap_close(&hp);
		return HPCAP_ERR;
	}

	/* Map device's memory */
	ret = hpcap_map(&hp);

	if (ret != HPCAP_OK) {
		printf("Error when opening the mapping HPCAP memory\n");
		hpcap_close(&hp);
		hpcap_close(&hp);
		return HPCAP_ERR;
	}

	assert(sizeof(long long) >= 8);

	// signal(SIGINT, stop_signal);

	while (!stop) {
		if (hp.acks == hp.avail)
			hpcap_ack_wait_timeout(&hp, /*BS*/1, 1000000000/*1 sec*/);

		if (hp.acks < hp.avail) {
			hpcap_read_packet(&hp, &bp, auxbuf, &caplen, NULL);

			if (bp) {
				raw_hdr = (struct raw_header*) bp;
				ts = raw_hdr->sec + 10e-9 * raw_hdr->nsec;

				if (frame_count > 0 && ts + 100 * 10e-9 < prev_ts)
					unordered++;

				prev_ts = ts;
				frame_count++;
			}

			if (frame_count >= 10000) {
				printf("Received %zu frames, %zu out of order\n", frame_count, unordered);
				frame_count = 0;
				unordered = 0;
			}
		}
	}


	hpcap_ack(&hp);


	hpcap_unmap(&hp);
	hpcap_close(&hp);

	return 0;
}

