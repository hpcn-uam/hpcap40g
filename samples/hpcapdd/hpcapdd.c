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

int main(int argc, char **argv)
{
	int fd = 1;
	struct hpcap_handle hp;
	int ret = 0;
	int ifindex = 0, qindex = 0;
	uint64_t written = 0;
	int64_t wrret = 0;
	struct timeval initwr, init;
	size_t transfer_count = 0;

#ifdef DEBUG
	struct timeval end;
	struct timeval endwr;
	double running_time, wrtime;
#endif

	char filename[512];

	gettimeofday(&init, NULL);

	if (argc != 4) {
		printf("Uso: %s <adapter index> <queue index> <output basedir | null>\n", argv[0]);
		return HPCAP_ERR;
	}

	if (strcmp(argv[3], "null") == 0) {
		printf("Warning: No output will be generated (dumb receiving)\n");
		fd = 0;
	}

	/* Creating HPCAP handle */
	ifindex = atoi(argv[1]);
	qindex = atoi(argv[2]);

	ret = hpcap_open(&hp, ifindex, qindex);

	if (ret != HPCAP_OK) {
		perror("Error when opening the HPCAP handle");
		hpcap_close(&hp);
		return HPCAP_ERR;
	}

	/* Map device's memory */
	ret = hpcap_map(&hp);

	if (ret != HPCAP_OK) {
		perror("Error when opening the mapping HPCAP memory");

		hpcap_close(&hp);
		return HPCAP_ERR;
	}

	if (hp.bufoff != 0)
		printf("Write performance will not be optimal\n");
	else
		printf("Write performance will be optimal\n");


	signal(SIGINT, capturaSenial);

	while (!stop) {
		if (fd) {
			gettimeofday(&initwr, NULL);
			sprintf(filename, "%s/%d", argv[3], ((int)initwr.tv_sec / DIRFREQ)*DIRFREQ); //a directory created every 1/2 hour
			mkdir(filename, S_IWUSR);//if the dir already exists, it returns -1
			sprintf(filename, "%s/%d/%d_hpcap%d_%d.raw", argv[3], ((int)initwr.tv_sec / DIRFREQ)*DIRFREQ, (int)initwr.tv_sec, ifindex, qindex);

			/* Opening output file */
			if (hp.bufoff == 0) {
				fd = open(filename, O_RDWR | O_TRUNC | O_CREAT | O_DIRECT | O_SYNC, 00666);

				if (fd == -1) {
					printf("Couldn't open file. Retrying without O_DIRECT | O_SYNC\n");
					fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, 00666);
				}
			} else
				fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, 00666);

			printf("Filename: %s (fd=%d)\n", filename, fd);

			if (fd == -1) {
				perror("Error when opening output file");
				return HPCAP_ERR;
			}
		}

		written = 0;

		while ((!stop) && (written < HPCAP_FILESIZE)) {
			/* acumular para escribir un bloque */
			hpcap_ack_wait_timeout(&hp, HPCAP_BS, 1000000000/*1 sec*/);
			printdbg("Available bytes: %lu.\n", hp.avail);

			if (hp.avail >= HPCAP_BS) {
				wrret = hpcap_write_block(&hp, fd, HPCAP_FILESIZE - written);

#ifdef DEBUG
				hpcap_print_listener_status(stderr, &hp);
#endif

				if (wrret < 0)
					printf("[ERR] Error escribiendo a disco\n");
				else
					written += wrret;

				transfer_count++;
			}
		}

		// Write the pending frames in the buffer until the current file is done.
		// Pending:
		if (stop) {
			hpcap_ack_wait_timeout(&hp, 1, 1); // Retrieve all available data
			printf("Writing final block size %lu\n", hp.avail);

			if (hp.avail > 0) {
				wrret = hpcap_write_block(&hp, fd, HPCAP_FILESIZE - written);

				if (wrret < 0)
					printf("[ERR] Error escribiendo a disco\n");
				else
					written += wrret;

				transfer_count++;
			}
		}

		hpcap_ack(&hp);

#ifdef DEBUG
		gettimeofday(&endwr, NULL);
#endif

		if (fd)
			close(fd);

#ifdef DEBUG
		wrtime = endwr.tv_sec - initwr.tv_sec;
		wrtime += (endwr.tv_usec - initwr.tv_usec) * 1e-6;

		printdbg("Transfer time: %lf s (%lu transfers)\n", wrtime, transfer_count);
		printdbg("%.1lf MBytes transfered => %.3lf Mbps\n",
				 (transfer_count * (double)HPCAP_BS) / MEGA, 7 * transfer_count * ((double) HPCAP_BS) / (MEGA * wrtime));
#endif
	}

#ifdef DEBUG
	gettimeofday(&end, NULL);
	running_time = end.tv_sec - init.tv_sec;
	running_time += (end.tv_usec - init.tv_usec) * 1e-6;
	printdbg("Total time: %lfs\n", running_time);
#endif

	hpcap_unmap(&hp);
	hpcap_close(&hp);

	return 0;
}
