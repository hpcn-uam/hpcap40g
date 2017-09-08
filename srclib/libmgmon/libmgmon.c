#include <sys/types.h>
#include <hpcap.h>

#include "libmgmon.h"

void * mgmon_signal_thread(void * arg)
{
	mgmon_signal *ms = arg;
	int ret = 0, sig = 0;

	ret = sigwait(&(ms->signal_mask), &sig);

	if ((ret == 0) && (sig == SIGINT))
		ms->stop = 1;
	else
		printf("Error...\n");

	return NULL;
}

int mgmon_packet_online_loop(int cpu, int ifindex, int qindex, packet_handler callback, void *arg)
{
	struct hpcap_handle hp;
	int ret = 0;
	u_char auxbuf[MAX_PACKET_SIZE];
	struct pcap_pkthdr head;
	uint8_t * bp;
	uint64_t timeout = 5000000000ULL;
	pthread_t tid, sigtid;
	cpu_set_t mask;
	mgmon_signal ms;

	tid = pthread_self();

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	ret = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &mask);

	if (ret < 0) {
		printf("[MgMON] Error when attaching thread to CPU %d\n", cpu);
		return -1;
	}

	ret = hpcap_open(&hp, ifindex, qindex);

	if (ret != HPCAP_OK) {
		printf("[MgMON] Error when opening interface hpcap%dq%d\n", ifindex, qindex);
		return -1;
	}

	ret = hpcap_map(&hp);

	if (ret != HPCAP_OK) {
		printf("[MgMON] Error when mapping interface hpcap%dq%d\n", ifindex, qindex);
		return -1;
	}

	ms.stop = 0;
	sigemptyset(&ms.signal_mask);
	sigaddset(&ms.signal_mask, SIGINT);
	pthread_sigmask(SIG_BLOCK, &ms.signal_mask, NULL);
	ret = pthread_create(&sigtid, NULL, mgmon_signal_thread, &ms);

	if (ret != 0) {
		printf("[MgMON] Error when creating signal watchdog thread\n");
		return -1;
	}

	pthread_setaffinity_np(sigtid, sizeof(cpu_set_t), &mask);


	while (!ms.stop) {
		hpcap_ack_wait_timeout(&hp, 1, timeout);

		while (hp.acks < hp.avail) {
			hpcap_read_packet(&hp, &bp, auxbuf, &head, hpcap_pcap_header);

			if (bp && callback)
				callback(bp, &head, arg);
		}
	}

	printf("[MgMON] stopping live packet capture for hpcap%dq%d\n", ifindex, qindex);
	pthread_join(sigtid, NULL);
	hpcap_ack(&hp);

	hpcap_unmap(&hp);
	hpcap_close(&hp);

	return 0;
}


static int open_multicast_rx_socket(int mode, int ifindex, int qindex)
{
	char mcastgroup[20];
	struct hostent *h;
	struct sockaddr_in servAddr;
	struct in_addr mcastAddr;
	int sd;
	struct ip_mreq mreq;
	struct timeval tv;

	sprintf(mcastgroup, "%d.%d.%d.%d", MCAST_BASE, mode, ifindex, qindex);
	h = gethostbyname(mcastgroup);

	if (!h) {
		printf("[MgMON] Error when setting multicast group\n");
		return 0;
	}

	memcpy(&mcastAddr, h->h_addr_list[0], h->h_length);
	sd = socket(AF_INET, SOCK_DGRAM, 0);

	if (sd == 0) {
		printf("[MgMON] Error when creating socket\n");
		return 0;
	}

	servAddr.sin_family = AF_INET;
	//servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_addr.s_addr = inet_addr(BIND_IFACE_IP);
	servAddr.sin_port = htons(MCAST_PORT);

	if (bind(sd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
		printf("[MgMON] Error when binding port to socket\n");
		return 0;
	}

	mreq.imr_multiaddr.s_addr = mcastAddr.s_addr;
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);

	//mreq.imr_interface.s_addr = inet_addr(BIND_IFACE_IP);
	if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *) &mreq, sizeof(mreq)) < 0) {
		printf("[MgMON] Error when joining multicast group %s\n", inet_ntoa(mcastAddr));
		return 0;
	}

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		printf("[MgMON] Error when setting socket timeout\n");
		return 0;
	}

	return sd;
}

int open_multicast_tx_socket(int mode, int ifindex, int qindex, struct sockaddr_in *dstAddr)
{
	char mcastgroup[20];
	struct hostent *h;
	struct sockaddr_in cliAddr;
	int sd;
	unsigned char ttl = MCAST_TTL;
	int size = 2129920;

	sprintf(mcastgroup, "%d.%d.%d.%d", MCAST_BASE, mode, ifindex, qindex);
	h = gethostbyname(mcastgroup);

	if (!h) {
		printf("[MgMON] Error when setting multicast group\n");
		return 0;
	}

	dstAddr->sin_family = h->h_addrtype;
	memcpy(&(dstAddr->sin_addr.s_addr), h->h_addr_list[0], h->h_length);
	dstAddr->sin_port = htons(MCAST_PORT);

	sd = socket(AF_INET, SOCK_DGRAM, 0);

	if (sd == 0) {
		printf("[MgMON] Error when creating socket\n");
		return 0;
	}

	cliAddr.sin_family = AF_INET;
	//cliAddr.sin_addr.s_addr=htonl(INADDR_ANY);
	cliAddr.sin_addr.s_addr = inet_addr(BIND_IFACE_IP);
	cliAddr.sin_port = htons(0);

	if (bind(sd, (struct sockaddr *) &cliAddr, sizeof(cliAddr)) < 0) {
		printf("[MgMON] Error when bibding port to socket\n");
		return 0;
	}

	if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
		printf("[MgMON] Error when setting socket's TTL\n");
		return 0;
	}

	if (setsockopt(sd, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size)) != 0) {
		printf("[MgMON] Error when setting socket's TX buffer\n");
		return 0;
	}

	return sd;
}


int mgmon_flow_online_loop(int cpu, int ifindex, int qindex, flow_handler callback, void *arg)
{
	pthread_t tid, sigtid;
	cpu_set_t mask;
	mgmon_signal ms;
	int ret, sd;
	int n;
	IPFlow record;

	tid = pthread_self();

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	ret = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &mask);

	if (ret < 0) {
		printf("[MgMON] Error when attaching thread to CPU %d\n", cpu);
		return -1;
	}

	sd = open_multicast_rx_socket(MCAST_FLOW, ifindex, qindex);

	if (sd == 0)
		return -1;

	ms.stop = 0;
	sigemptyset(&ms.signal_mask);
	sigaddset(&ms.signal_mask, SIGINT);
	pthread_sigmask(SIG_BLOCK, &ms.signal_mask, NULL);
	ret = pthread_create(&sigtid, NULL, mgmon_signal_thread, &ms);

	if (ret != 0) {
		printf("[MgMON] Error when creating signal watchdog thread\n");
		return -1;
	}

	pthread_setaffinity_np(sigtid, sizeof(cpu_set_t), &mask);

	while (!ms.stop) {
		n = recvfrom(sd, &record, sizeof(IPFlow), 0, NULL, 0);
		printf("Flow loop: %d bytes recibidos\n", n);

		if (n == sizeof(IPFlow)) {
			if (callback)
				callback(&record, arg);
		}
	}

	printf("[MgMON] stopping live flow capture for hpcap%dq%d\n", ifindex, qindex);

	close(sd);

	return 0;
}


int mgmon_mrtg_online_loop(int cpu, int ifindex, int qindex, mrtg_handler callback, void *arg)
{
	pthread_t tid, sigtid;
	cpu_set_t mask;
	mgmon_signal ms;
	int ret, sd;
	int n;
	mrtg stat;

	tid = pthread_self();

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	ret = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &mask);

	if (ret < 0) {
		printf("[MgMON] Error when attaching thread to CPU %d\n", cpu);
		return -1;
	}

	sd = open_multicast_rx_socket(MCAST_MRTG, ifindex, qindex);

	if (sd == 0)
		return -1;

	ms.stop = 0;
	sigemptyset(&ms.signal_mask);
	sigaddset(&ms.signal_mask, SIGINT);
	pthread_sigmask(SIG_BLOCK, &ms.signal_mask, NULL);
	ret = pthread_create(&sigtid, NULL, mgmon_signal_thread, &ms);

	if (ret != 0) {
		printf("[MgMON] Error when creating signal watchdog thread\n");
		return -1;
	}

	pthread_setaffinity_np(sigtid, sizeof(cpu_set_t), &mask);

	while (!ms.stop) {
		n = recvfrom(sd, &stat, sizeof(mrtg), 0, NULL, 0);

		if (n == sizeof(mrtg)) {
			if (callback)
				callback(&stat, arg);
		}
	}

	printf("[MgMON] stopping live MRTG capture for hpcap%dq%d\n", ifindex, qindex);

	close(sd);

	return 0;
}
