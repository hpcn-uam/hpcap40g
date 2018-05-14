#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "../include/hpcap.h"

#define HUGETLB_PATH "/mnt/hugetlb"

static inline void usage()
{
	printf("Usage: huge_map adapter queue (map|unmap) buffer-size [hugetlb path]\n");
}

static size_t parse_size(char* sz)
{
	char modifier = sz[strlen(sz) - 1];
	size_t multiplier = 1;
	size_t number;

	sz[strlen(sz) - 1] = 0;
	number = atoi(sz);
	sz[strlen(sz)] = modifier;

	switch (modifier) {
		case 'G':
		case 'g':
			multiplier = 1024UL * 1024UL * 1024UL;
			break;

		case 'M':
		case 'm':
			multiplier = 1024UL * 1024UL;
			break;

		case 'K':
		case 'k':
			multiplier = 1024UL;
			break;
	}

	return number * multiplier;
}

int main(int argc, char *argv[])
{
	char* action;
	int adapter, queue;
	size_t size;
	struct hpcap_handle handle;
	char* hugetlb_path;
	int retval = -1;

	if (argc != 5 && argc != 6) {
		usage();
		return -1;
	}

	if (argc == 6)
		hugetlb_path = argv[5];
	else
		hugetlb_path = HUGETLB_PATH;

	adapter = atoi(argv[1]);
	queue = atoi(argv[2]);
	action = argv[3];
	size = parse_size(argv[4]);

	if (hpcap_open(&handle, adapter, queue)) {
		fprintf(stderr, "Error opening HPCAP handle.\n");
		return -1;
	}

	if (!strcmp(action, "map")) {
		printf("Mapping on /dev/hpcap%d_%d buffer of size %zu\n", adapter, queue, size);

		retval = hpcap_map_huge(&handle, hugetlb_path, size);

		if (retval != HPCAP_OK)
			fprintf(stderr, "Error mapping hugefile %s of size %s (%zu bytes)\n", hugetlb_path, argv[4], size);
	} else if (!strcmp(action, "unmap")) {
		printf("Unmapping on /dev/hpcap%d_%d buffer of size %zu\n", adapter, queue, size);
		retval = hpcap_unmap_huge(&handle, 1);

		if (retval == -EIDRM)
			fprintf(stderr, "No hugetlb mapped into the given adapter.\n");
	} else
		usage();

	return retval;
}
