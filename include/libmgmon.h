#ifndef _MGMON_LIB_
#define _MGMON_LIB_

/**
 * @brief MgMon library
 * @addtogroup mgmon
 * @{
 */

#include <stdio.h>
#include <stdlib.h>
#include <pcap.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MCAST_BASE 224
#define MCAST_FLOW 128
#define MCAST_MRTG 129

#define MCAST_PORT 1500
//#define MCAST_PORT 65535
#define MCAST_TTL 1

#define BIND_IFACE_IP "127.0.0.1"

typedef struct {
	sigset_t signal_mask;
	int stop;
} mgmon_signal;

#define MAX_PACK 10
#define MAX_PAYLOAD 400

typedef struct IPFlow {
	uint32_t source_ip;
	uint32_t destination_ip;
	uint64_t source_mac;
	uint64_t destination_mac;
	uint16_t source_port;
	uint16_t destination_port;
	uint8_t transport_protocol;

	uint64_t nbytes;
	uint32_t npack;
	uint16_t max_pack_size;
	uint16_t min_pack_size;
	uint64_t nbytes_sqr;
	double avg_pack_size, std_pack_size; //new

	uint64_t previous_timestamp;

	uint64_t rtt_syn;
	uint8_t rtt_syn_done;

	double max_int_time;
	double min_int_time;
	double sum_int_time;
	double sum_int_time_sqr;
	double avg_int_time, std_int_time; //new

	uint64_t lastpacket_timestamp;//new
	uint64_t firstpacket_timestamp;//new
	uint64_t duration;//new

	uint32_t num_flags[8];
	// 0 FIN;
	// 1 SYN;
	// 2 RST;
	// 3 PSH;
	// 4 ACK;
	// 5 URG;
	// 6 CWR;
	// 7 ECE;

	uint32_t nwindow_zero;

	uint8_t *payload_ptr;
	uint32_t npack_payload;
	uint32_t current_seq_number;
	uint32_t previous_seq_number;

	uint16_t dataLen;
	uint16_t offset;

	uint8_t flags;
	uint8_t flag_FIN;
	uint8_t flag_ACK_nulo;

	// Frag packets flag
	uint8_t frag_flag;
	uint16_t ip_id;

	uint8_t expired_by_flags;

	uint64_t timestamp[MAX_PACK];
	uint16_t packet_offset[MAX_PACK];
	uint16_t size[MAX_PACK];
	uint8_t payload[MAX_PAYLOAD];
} IPFlow;

typedef struct {
	uint64_t bytes;
	uint64_t packets;
	uint64_t concurrent_flows;
	uint64_t timestamp;
} mrtg;

typedef void (*packet_handler)(uint8_t *payload, struct pcap_pkthdr *header, void *arg);
typedef void (*flow_handler)(IPFlow *record, void *arg);
typedef void (*mrtg_handler)(mrtg *stat, void *arg);

/**
 * Receives frames from the given interface/adapter in a loop.
 * @param  cpu      CPU core to bind the process to.
 * @param  ifindex  Interface index.
 * @param  qindex   Queue index.
 * @param  callback Function to call when a frame is received.
 * @param  arg      Argument to pass to the callback function.
 * @return          0 on OK, -1 on error.
 */
int mgmon_packet_online_loop(int cpu, int ifindex, int qindex, packet_handler callback, void *arg);

/**
 * Receives flows from the given interface/adapter in a loop.
 * @param  cpu      CPU core to bind the process to.
 * @param  ifindex  Interface index.
 * @param  qindex   Queue index.
 * @param  callback Function to call when a frame is received.
 * @param  arg      Argument to pass to the callback function.
 * @return          0 on OK, -1 on error.
 */
int mgmon_flow_online_loop(int cpu, int ifindex, int qindex, flow_handler callback, void *arg);

/**
 * Receives mrtgs from the given interface/adapter in a loop.
 * @param  cpu      CPU core to bind the process to.
 * @param  ifindex  Interface index.
 * @param  qindex   Queue index.
 * @param  callback Function to call when a frame is received.
 * @param  arg      Argument to pass to the callback function.
 * @return          0 on OK, -1 on error.
 */
int mgmon_mrtg_online_loop(int cpu, int ifindex, int qindex, mrtg_handler callback, void *arg);

int open_multicast_tx_socket(int mode, int ifindex, int qindex, struct sockaddr_in *dstAddr);

/** @} */

#endif /* _MGMON_LIB_ */
