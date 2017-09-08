#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>

#include "../../include/hpcap.h"

#define MEGA (1024*1024)
#define DIRFREQ 1800

/*
 Función que se ejecuta cuando se genera la señal generada por Control+C. La idea es
 realizar una salida "ordenada".
 Parametros de entrada:
	-int nSenial: identificador de la señal capturada.
*/
int stop = 0;
void capturaSenial(int nSenial)
{
	if (stop == 1)
		return;

	stop = 1;
	return;
}

void hex_dump(const char* description, const uint8_t* addr, size_t len);

static void running_avg_var(size_t* n, double* avg, double* var, double newval)
{
	*n += 1;
	double delta = newval - *avg;
	*avg += delta / *n;
	double delta2 = newval - *avg;
	*var += delta * delta2;
}

long tsdiff(struct timespec* ts_start, struct timespec* ts_end)
{
	return (ts_end->tv_sec - ts_start->tv_sec) * 1000000000 + (ts_end->tv_nsec - ts_start->tv_nsec);
}

void busywait(unsigned long ns)
{
	struct timespec ts_start, ts_end;

	clock_gettime(CLOCK_REALTIME, &ts_start);

	do {
		clock_gettime(CLOCK_REALTIME, &ts_end);
	} while (tsdiff(&ts_start, &ts_end) < ns);
}

int main(int argc, char **argv)
{
	struct hpcap_handle hp;
	int ret = 0;
	int ifindex = 0, qindex = 0;
	size_t frame_size = 0;
	size_t unwritten_packets = 0;
	size_t frame_in_buffer_size = 0;

	//struct timeval init, end;
	struct raw_header* raw_hdr;
	size_t frame_count = 0;
	uint8_t *buffer;
	size_t buffer_size;
	size_t prev_acks, prev_rdoff;
	double end_dist_avg = 0;
	double end_dist_std = 0;
	double avail_avg = 0;
	double avail_std = 0;
	double sleep_avg = 0;
	double sleep_std = 0;
	size_t ack_count = 0;
	size_t last_readable = 0;
	size_t refresh_stats_each = 50 * 1000000;
	short last_unwritten = 0;

	u_char auxbuf[RAW_HLEN + MAX_PACKET_SIZE];
	struct timespec ts_start, ts_end, wait;

	//gettimeofday(&init, NULL);
	if (argc != 4) {
		printf("Usage: %s <adapter index> <queue index> <frame size>\n", argv[0]);
		return HPCAP_ERR;
	}

	/* Creating HPCAP handle */
	ifindex = atoi(argv[1]);
	qindex = atoi(argv[2]);
	frame_size = atoi(argv[3]);
	frame_in_buffer_size = frame_size + RAW_HLEN;
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
		return HPCAP_ERR;
	}

	signal(SIGINT, capturaSenial);

	buffer = hp.buf;
	buffer_size = hp.bufSize;

	wait.tv_nsec = 1000;
	wait.tv_sec = 0;

	printf("Consistency check: Frame size %zu (%zu in buffer)\n", frame_size, frame_in_buffer_size);

	while (!stop) {
		if (hp.acks >= hp.avail) {
			hpcap_ack_wait_timeout(&hp, 1, 1000000000 /* 1 second */);
			last_readable = (hp.rdoff + hp.avail) % hp.bufSize;
			clock_gettime(CLOCK_REALTIME, &ts_start);
			busywait(30000);
			clock_gettime(CLOCK_REALTIME, &ts_end);

			double diff = tsdiff(&ts_start, &ts_end);

			running_avg_var(&ack_count, &sleep_avg, &sleep_std, diff);
			ack_count--;
			running_avg_var(&ack_count, &avail_avg, &avail_std, hp.avail);
		}

		if (hp.acks < hp.avail) {
			prev_acks = hp.acks;
			prev_rdoff = hp.rdoff;
			_hpcap_read_next(&hp, &raw_hdr, NULL, auxbuf);

			if ((hpcap_is_header_padding(raw_hdr) && hp.rdoff >= frame_in_buffer_size)
				|| (!hpcap_is_header_padding(raw_hdr) && raw_hdr->caplen != frame_size)) {
				hp.rdoff = prev_rdoff;
				hp.acks = prev_acks;

				if (!last_unwritten) {
					double dist_to_last;

					if (prev_rdoff > last_readable)
						dist_to_last = (last_readable + hp.bufSize) - prev_rdoff;
					else
						dist_to_last = last_readable - prev_rdoff;

					running_avg_var(&unwritten_packets, &end_dist_avg, &end_dist_std, dist_to_last);
				}
			} else
				last_unwritten = 0;

			frame_count++;
		}

		if ((frame_count % refresh_stats_each) == 0) {
			printf("Received %zu frames, %zu unwritten (%.2lf %%)\n", frame_count, unwritten_packets, (100.0 * unwritten_packets) / frame_count);
			printf("Average distance to last: %.2lf +- %.2lf \n", end_dist_avg, sqrt(end_dist_std / (unwritten_packets - 1)));
			printf("%zu acks, avail %.2lf +- %.2lf, sleep after %.2lf +- %.2lf ns\n", ack_count, avail_avg, sqrt(avail_std / (ack_count - 1)), sleep_avg, sqrt(sleep_std / (ack_count - 1)));
		}
	}


	printf("Received %zu frames, %zu unwritten (%.2lf %%)\n", frame_count, unwritten_packets, (100.0 * unwritten_packets) / frame_count);
	printf("Average distance to last: %.2lf +- %.2lf \n", end_dist_avg, sqrt(end_dist_std / (unwritten_packets - 1)));
	printf("%zu acks, avail %.2lf +- %.2lf, sleep after %.2lf +- %.2lf ns\n", ack_count, avail_avg, sqrt(avail_std / (ack_count - 1)), sleep_avg, sqrt(sleep_std / (ack_count - 1)));
	hpcap_unmap(&hp);
	hpcap_close(&hp);

	return 0;
}

void hex_dump(const char* description, const uint8_t* addr, size_t len)
{
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*)addr;

	printf("%s (%zu bytes):\n", description ? description : "-", len);

	// Process every byte in the data.
	for (i = 0; i < len; i++) {
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0) {
			// Just don't print ASCII for the zeroth line.
			if (i != 0)
				printf("  %s\n", buff);

			// Output the offset.
			printf("  %04X ", i);
		}

		// Now the hex code for the specific character.
		printf(" %02X", pc[i]);

		if ((i + 1) % 8 == 0 && (i % 16) != 0)
			printf(" ");

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];

		buff[(i % 16) + 1] = '\0';
	}

	// Pad out last line if not exactly 16 characters.
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}

	// And print the final ASCII bit.
	printf("  %s\n", buff);
}

