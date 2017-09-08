#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>

#include "hpcap.h"


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

int main(int argc, char **argv)
{
	struct hpcap_handle hp;
	int ret = 0;
	char hexdump_desc[256];
	int ifindex = 0, qindex = 0;

	if (argc != 5) {
		//printf("Uso: %s <adapter index> <queue index> <fichero RAW de salida> <bs> <count>\n", argv[0]);
		printf("Uso: %s <adapter index> <queue index> <begin buf> <end buf>\n", argv[0]);
		return HPCAP_ERR;
	}

	printf("HPCAP_BUF_SIZE:%lu\n", HPCAP_BUF_SIZE);

	/* Creating HPCAP handle */
	ifindex = atoi(argv[1]);
	qindex = atoi(argv[2]);
	ret = hpcap_open(&hp, ifindex, qindex);

	if (ret != HPCAP_OK) {
		printf("Error when opening the HPCAP handle\n");
		return HPCAP_ERR;
	}

	/* Map device's memory */
	ret = hpcap_map(&hp);

	if (ret != HPCAP_OK) {
		printf("Error when opening the mapping HPCAP memory\n");
		hpcap_close(&hp);
		return HPCAP_ERR;
	}

	size_t buf_start = atoi(argv[3]);
	size_t buf_end = atoi(argv[4]);
	size_t to_print_size = buf_end - buf_start;

	snprintf(hexdump_desc, sizeof(hexdump_desc), "Buffer %zu - %zu", buf_start, buf_end);
	hex_dump(hexdump_desc, hp.buf + buf_start, to_print_size);

	printf("\n");
	hpcap_unmap(&hp);
	hpcap_close(&hp);
	return 0;
}
