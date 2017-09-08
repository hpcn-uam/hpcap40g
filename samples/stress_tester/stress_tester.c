#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdint.h>

#include "hpcap.h"

#define MEGA (1024*1024)
#define DIRFREQ 1800

volatile sig_atomic_t stop = 0;

void signal_stop(int sig)
{
	stop = 1;
}

static char* readable_fs(double size/*in bytes*/)
{
	int i = 0;
	static char buf[1024];
	const char* units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};

	while (size > 1024) {
		size /= 1024;
		i++;
	}

	sprintf(buf, "%.*f %s", i, size, units[i]);
	return buf;
}

static short prepare_hpcap(int ifindex, int qindex, struct hpcap_handle* hp)
{
	int ret;

	ret = hpcap_open(hp, ifindex, qindex);

	if (ret != HPCAP_OK) {
		printf("Error when opening the HPCAP handle\n");
		hpcap_close(hp);
		return HPCAP_ERR;
	}

	/* Map device's memory */
	ret = hpcap_map(hp);

	if (ret != HPCAP_OK) {
		printf("Error when opening the mapping HPCAP memory\n");
		hpcap_close(hp);
		return HPCAP_ERR;
	}

	if (hp->bufoff != 0)
		printf("Write performance will not be optimal\n");
	else
		printf("Write performance will be optimal\n");

	return HPCAP_OK;
}

int main(int argc, char **argv)
{
	struct hpcap_handle hp_1, hp_2;
	int ret = 0;
	int ifindex = 0, qindex = 0;
	size_t total_rx = 0;

	if (argc != 3) {
		printf("usage: %s <adapter index> <queue index>\n", argv[0]);
		return HPCAP_ERR;
	}

	/* Creating HPCAP handle */
	ifindex = atoi(argv[1]);
	qindex = atoi(argv[2]);

	if (prepare_hpcap(ifindex, qindex, &hp_1) != HPCAP_OK)
		return HPCAP_ERR;

	if (prepare_hpcap(ifindex, qindex, &hp_2) != HPCAP_OK)
		return HPCAP_ERR;

	signal(SIGINT, signal_stop);

	printf("Test start\n");

	while (!stop) {
		total_rx = 0;

		printf("Waiting to receive a full HPCAP file\n");

		while (!stop && total_rx < HPCAP_FILESIZE) {
			hpcap_ack_wait_timeout(&hp_1, HPCAP_BS, 1000000000);
			hpcap_ack_wait_timeout(&hp_2, HPCAP_BS, 1000000000);

			printf("1/2 Avail: %zu / %zu, HPCAP_BS is %zu, avail > HPCAP_BS %d / %d\n",
				   hp_1.avail, hp_2.avail, HPCAP_BS, hp_1.avail > HPCAP_BS, hp_2.avail > HPCAP_BS);

			if (hp_1.avail < HPCAP_BS || hp_2.avail < HPCAP_BS) {
				printf("Timeout expired, aborting.\n");
				abort();
			}

			hp_1.acks += HPCAP_BS;
			hp_2.acks += HPCAP_BS;

			total_rx += HPCAP_BS;
		}

		hpcap_ack(&hp_1);
		hpcap_ack(&hp_2);

		printf("Full HPCAP file received. Waiting for more data...\n");

		usleep(100 * 1000);

		if (stop)
			break;

		printf("Stalling handle 1, advancing handler 2\n");

		hpcap_wait(&hp_2, 1);

		printf("Waiting...\n");
		fflush(stdout);

		sleep(10);
	}

	printf("\nEnd\n");

	hpcap_unmap(&hp_1);
	hpcap_close(&hp_1);
	hpcap_unmap(&hp_2);
	hpcap_close(&hp_2);

	return 0;
}
