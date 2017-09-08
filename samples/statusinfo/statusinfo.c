#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "hpcap.h"

int main(int argc, char *argv[])
{
	int i;
	struct hpcap_ioc_status_info info;
	struct hpcap_buffer_info bufinfo;
	char devname[100] = "";
	short buffer_check = 0;
	int fd;

	if (argc < 3) {
		printf("Usage: %s <adapter index> <queue index> [--buffer-check]\n", argv[0]);
		printf("--buffer-check: Ensure that the buffer is writable.\n");
		return HPCAP_ERR;
	} else
		sprintf(devname, "/dev/hpcap_%d_%d", atoi(argv[1]), atoi(argv[2]));

	if (argc > 3 && strcmp("--buffer-check", argv[3]) == 0)
		buffer_check = 1;

	fd = open(devname, 0);

	if (fd == -1) {
		fprintf(stderr, "Error opening %s: %s\n", devname, strerror(errno));
		return -1;
	}

	ioctl(fd, HPCAP_IOC_STATUS_INFO, &info);

	//Adapters
	for (i = 0; i < info.num_adapters; i++)
		printf("%s connected to PCI bus %s\n", info.adapters[i].netdev_name, info.adapters[i].pdev_name);

	//Buffer
	bufinfo = info.bufinfo;
	printf("bufinfo, %zu bytes at %p + %zu, hugepages = %hd\n",
		   bufinfo.size, bufinfo.addr, bufinfo.offset, bufinfo.has_hugepages);

	printf("Consumer atomic offsets: Read %zu, write %zu\n", info.consumer_read_off, info.consumer_write_off);

	//Global listener
	printf("Global Listener %d: Kill %d. Read Offset: %ld. Write Offset: %ld\n",
		   info.global_listener.id, info.global_listener.kill, info.global_listener.bufferRdOffset, info.global_listener.bufferWrOffset);

	//Other listeners
	printf("%d active listeners\n", info.num_listeners);

	for (i = 0; i < info.num_listeners; i++) {
		printf("Listener %d: Kill %d. Read Offset: %ld. Write Offset: %ld\n",
			   info.listeners[i].id, info.listeners[i].kill, info.listeners[i].bufferRdOffset, info.listeners[i].bufferWrOffset);
	}

	if (info.thread_state == -1)
		printf("Reception thread state: -1 Not created\n");
	else if (info.thread_state == 0)
		printf("Reception thread state: 0 Running\n");
	else if (info.thread_state == 1)
		printf("Reception thread state: Sleeping - Interruptible\n");
	else if (info.thread_state == 2)
		printf("Reception thread state: %ld unknown\n", info.thread_state);

	if (buffer_check) {
		if (ioctl(fd, HPCAP_IOC_BUFCHECK) < 0)
			printf("Buffer check failed.\n");
		else
			printf("Buffer check OK.\n");
	}

	close(fd);

	return 0;
}
