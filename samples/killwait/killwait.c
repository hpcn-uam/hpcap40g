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

int main(int argc, char **argv)
{
	int fd = 0;
	struct hpcap_handle hp;
	int ret = 0;
	int ifindex = 0, qindex = 0;

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
		close(fd);
		return HPCAP_ERR;
	}

	hpcap_ioc_killwait(&hp);

	hpcap_close(&hp);

	return 0;
}

