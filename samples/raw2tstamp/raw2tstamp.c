#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>

#include "hpcap.h"

#define CAPLEN 65535
#define MAX_PACKET_LEN 2048

//#define PKT_LIMIT 15000000
//#define DEBUG
//#define DEBUG2
#define NSECS_PER_SEC (1000000000)
#define DUMP_PCAP


int main(int argc, char **argv)
{
	FILE *fraw, *fout;
	u_char buf[4096];
	uint32_t secs, nsecs;
	uint64_t tstamp;
	uint64_t epoch = 0;
	uint16_t len, caplen;
	int i = 0, j = 0, ret = 0;

	if (argc != 3) {
		printf("Uso: %s <fichero_RAW_de_entrada> <fichero_PCAP_de_salida>\n", argv[0]);
		exit(-1);
	}


	fraw = fopen(argv[1], "r");

	if (!fraw) {
		perror("fopen");
		exit(-1);
	}

	fout = fopen(argv[2], "w");

	if (!fout) {
		perror("fopen");
		fclose(fout);
		exit(-1);
	}

	i = 0;

	while (1) {

		/* Lectura de info asociada a cada paquete */
		if (fread(&secs, 1, sizeof(uint32_t), fraw) != sizeof(uint32_t)) {
			printf("Segundos\n");
			break;
		}

		if (fread(&nsecs, 1, sizeof(uint32_t), fraw) != sizeof(uint32_t)) {
			printf("Nanosegundos\n");
			break;
		}

		if (nsecs >= NSECS_PER_SEC) {
			printf("Wrong NS value (file=%d,pkt=%d)\n", j, i);
			//break;
		}

		if ((secs == 0) && (nsecs == 0)) {
			if (fread(&caplen, 1, sizeof(uint16_t), fraw) != sizeof(uint16_t))
				fprintf(stderr, "Failure reading caplen\n");

			if (fread(&len, 1, sizeof(uint16_t), fraw) != sizeof(uint16_t))
				fprintf(stderr, "Failure reading len\n");

			if (len != caplen)
				printf("Wrong padding format [len=%d,caplen=%d]\n", len, caplen);
			else
				printf("Padding de %d bytes\n", caplen);

			break;
		}

		if (epoch == 0)
			epoch = secs;

		if (fread(&caplen, 1, sizeof(uint16_t), fraw) != sizeof(uint16_t)) {
			printf("Caplen\n");
			break;
		}

		if (fread(&len, 1, sizeof(uint16_t), fraw) != sizeof(uint16_t)) {
			printf("Longitud\n");
			break;
		}


		/* Escritura de cabecera */
		tstamp = secs - epoch;
		tstamp *= 1000000000ul;
		tstamp += nsecs;

		/* Lectura del paquete */
		if (len > 0) {
			ret = fread(buf, 1, len, fraw);

			if (ret != len) {
				printf("Lectura del paquete\n");
				break;
			}

			/*for(j=0;j<64;j+=8)
			{
				printf( "\t%02x %02x %02x %02x\t%02x %02x %02x %02x\n", buf[j], buf[j+1], buf[j+2], buf[j+3], buf[j+4], buf[j+5], buf[j+6], buf[j+7]);
			}*/

		}

		/* Escribir a fichero */
		fprintf(fout, "%lu\t%d\t%d\n", tstamp, len, caplen);
		i++;

#ifdef PKT_LIMIT

		if (i >= PKT_LIMIT)
			break;

#endif
	}

	printf("%d paquetes leidos\n", i);
	fclose(fout);
	fclose(fraw);

	return 0;
}
