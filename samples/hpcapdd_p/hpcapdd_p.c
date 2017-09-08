#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>

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

int main(int argc, char **argv)
{
	int fd = 1;
	struct hpcap_handle hp;
	int ret = 0;
	unsigned long int i = 0;
	int ifindex = 0, qindex = 0;
	short use_stdout = 0;
	size_t packet_count = 0;

	//struct timeval init, end;
	struct timeval initwr;
	struct raw_header* raw_hdr;
	size_t frame_count = 0;

	uint16_t caplen = 0;
	u_char *bp = NULL;
	u_char auxbuf[RAW_HLEN + MAX_PACKET_SIZE];
	char hexdump_desc[1024];

	char filename[512];


	//gettimeofday(&init, NULL);
	if (argc != 4) {
		printf("Uso: %s <adapter index> <queue index> <output basedir | null | stdout>\n", argv[0]);
		return HPCAP_ERR;
	}

	if (strcmp(argv[3], "null") == 0) {
		printf("Warning: No output will be generated (dumb receiving)\n");
		fd = 0;
	} else if (strcmp(argv[3], "stdout") == 0) {
		printf("Warning: printing each frame through stdout\n");
		fd = 0;
		use_stdout = 1;
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

	signal(SIGINT, capturaSenial);

	while (!stop) {
		if (fd) {
			gettimeofday(&initwr, NULL);
			sprintf(filename, "%s/%d", argv[3], ((int)initwr.tv_sec / DIRFREQ)*DIRFREQ); //a directory created every 1/2 hour
			mkdir(filename, S_IWUSR);//if the dir already exists, it returns -1
			sprintf(filename, "%s/%d/%d_hpcap%d_%d.raw", argv[3], ((int)initwr.tv_sec / DIRFREQ)*DIRFREQ, (int)initwr.tv_sec, ifindex, qindex);
			/* Opening output file */
			fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, 00666);
			printf("filename:%s (fd=%d)\n", filename, fd);

			if (fd == -1) {
				printf("Error when opening output file\n");
				return HPCAP_ERR;
			}
		}

		i = 0;

		while ((!stop) && (i < HPCAP_FILESIZE)) {
			if (hp.acks == hp.avail)
				hpcap_ack_wait_timeout(&hp, /*BS*/1, 1000000000/*1 sec*/);

			if (hp.acks < hp.avail) {
				hpcap_read_packet(&hp, &bp, auxbuf, &caplen, NULL);

				if (bp) { //not padding
					if (fd)
						ret = write(fd, bp, caplen + RAW_HLEN);
					else if (use_stdout) {
						raw_hdr = bp;

						sprintf(hexdump_desc, "Frame %zu: %hu bytes (%hu captured). Timestamp: %u.%u",
								frame_count, raw_hdr->len, raw_hdr->caplen,
								raw_hdr->sec, raw_hdr->nsec);
						hex_dump(hexdump_desc, bp + sizeof(struct raw_header), raw_hdr->caplen);
					}

					i += caplen + RAW_HLEN;
					frame_count++;
					packet_count++;
				}
			}
		}

		printf("Block end: %zu packets received\n", packet_count);
		packet_count = 0;

		hpcap_ack(&hp);

		if (fd)
			close(fd);
	}

	printf("Received %zu frames (%d bytes)\n", frame_count, i);

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

