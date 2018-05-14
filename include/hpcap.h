#ifndef _HPCAP_H_
#define _HPCAP_H_


#define MAX_DEVICES	16
#define MAX_RINGS	64 //
#define MAX_CONSUMERS_PER_Q 	32 // Number of consumer threads per queue.

#define minimo(a,b) ((a) < (b) ? (a) : (b))
#define maximo(a,b) ((a) > (b) ? (a) : (b))

#include <linux/types.h>

#ifdef __KERNEL__

#include <asm/ioctl.h>

#define HPCAP_MAJOR 345 //
#define HPCAP_NAME "hpcap"

#define MAX_BUFS 16

#define ns(a) ( (a*HZ) / 1000ul*1000ul*1000ul )

#else	/* __KERNEL__ */

#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <linux/ioctl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>

#define __user
#define IFNAMSIZ 16
#define ETH_ALEN 6

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#endif /* __KERNEL__ */

#define MAX_LISTENERS 4


static inline void prefetcht0(void *p)
{
	__asm__ volatile("prefetcht0 (%0)\n\t"
	                 :
	                 : "r"(p)
	                );
}

static inline void prefetchnta(void *p)
{
	__asm__ volatile("prefetchnta (%0)\n\t"
	                 :
	                 : "r"(p)
	                );
}

/* Branch prediction GCC optimization macros */
#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

/*********************************************************************************
 PARAMS
*********************************************************************************/
//#define HPCAP_MAX_IFS 1ul
//#define HPCAP_MAX_QUEUES 1ul
#define HPCAP_MAX_NIC 16ul

#define RAW_HLEN sizeof(struct raw_header)

/************************************************
* JUMBO
*  uncomment this define to enable support
*  for jumboframes (may affect performance)
************************************************/
#define MAX_DESCR_SIZE	2048
//#define JUMBO
#ifdef JUMBO
#define MAX_JUMBO_SIZE 15872 // 15.5 KB
#define MAX_DESCRIPTORS ( 1 + ((MAX_JUMBO_SIZE)/(MAX_DESCR_SIZE)) ) // max number of descriptors that a JUMBOFRAME can take
#define MAX_PACKET_SIZE	(MAX_JUMBO_SIZE)
#else
#define MAX_DESCRIPTORS (1)
#define MAX_PACKET_SIZE	(MAX_DESCR_SIZE)
#endif

/************************************************
* PRINT_DEBUG
*  uncomment this define in order to enable some
*  printk that may help debugging at the cost of
*  performance
************************************************/
//#define PRINT_DEBUG

/************************************************
* BUF_DEBUG
*  uncomment this define in order to ease buffer
*  debugging (will set to 0 the buffer at the
*  begining and in padding areas) at the cost
*  performance
************************************************/
//#define BUF_DEBUG

/************************************************
* DO_BUF_ALLOC
*  uncomment this define in order to make the
*  buffers to dynamically allocated
************************************************/
//#define DO_BUF_ALLOC
#ifdef DO_BUF_ALLOC
#define HPCAP_BUF_SIZE (2ul*1024ul*1024ul)
#else
#define HPCAP_BUF_SIZE ( 1024ul*1024ul )
#define HPCAP_BUF_DSIZE (4ul*1024ul*1024ul)
#endif

/**
 * HPCAP_PROFILING: enables the profiler (see hpcap_rx_profile.c/h) with some stats
 * about the driver polling threads.
 */
// #define HPCAP_PROFILING

/**
 * HPCAP_CONSUMERS_VIA_RINGS: Assign one different ring to each consumer, instead of
 * making all consumer threads read from the same ring.
 */
// #define HPCAP_CONSUMERS_VIA_RINGS

/**
 * HPCAP_MEASURE_LATENCY: Enables measurements of latency with iDPDK-LatencyMetter
 */
#define HPCAP_MEASURE_LATENCY

/************************************************
* REMOVE_DUPS
*  uncomment this define to enable the duplicate detection
************************************************/
// #define REMOVE_DUPS
#ifdef REMOVE_DUPS
#define DUP_CHECK_LEN 70
#define SCL "70"
#define DUP_WINDOW_SIZE (1024ul*32ul)
#define SWS "1K"
#define DUP_WINDOW_LEVELS 1
#define SWL "1"
#define DUP_TIME_WINDOW (2ul*1000ul*1000ul*1000ul) //this value is specified in ns
#define STW "2000000000"

struct hpcap_dup_info {
	u64 tstamp;
	u16 len;
	u8 data[DUP_CHECK_LEN];
};
#endif

/************************************************
* HPCAP_SYSFS
*  comment/uncomment this define to enable/disable attribute hot-swapping
************************************************/
#define HPCAP_SYSFS
#ifdef HPCAP_SYSFS
#define HOT_DUPS 0		// position in device array for attributes
#define HOT_CAPLEN 1
#endif

#define HPCAP_DEFAULT_MODE 1
#define HPCAP_DEFAULT_PAGES_PER_ADAPTER 1000

#define HPCAP_MAX_RXQ 16

#define HPCAP_IBS 1048576ul
#define HPCAP_OBS 1048576ul
#define HPCAP_BS (4 * 1048576ul)
#define HPCAP_COUNT 512ul //256ul //3072ul //768ul //3072=384*8 para ficheros de 3GB
#define HPCAP_FILESIZE (HPCAP_BS*HPCAP_COUNT) //tiene que ser multiplo de oblock=8M
#define HPCAP_MAX_FILTERS 256
#define HPCAP_MAX_FILTER_STRLEN 50
/********************************************************************************/

#define HPCAP_OK 0
#define HPCAP_ERR -1

/***********************************************
 IOCTL commands
***********************************************/
#define HPCAP_IOC_MAGIC 70
#define HPCAP_IOC_LSTOP _IOWR(HPCAP_IOC_MAGIC, 1, struct hpcap_listener_op*)
#define HPCAP_IOC_KILLWAIT _IO(HPCAP_IOC_MAGIC, 4)
#define HPCAP_IOC_BUFINFO _IOR(HPCAP_IOC_MAGIC, 5, struct hpcap_buffer_info*)
#define HPCAP_IOC_OFFSETS _IOR(HPCAP_IOC_MAGIC, 6, struct hpcap_listener_op*)
#define HPCAP_IOC_DUP _IOR(HPCAP_IOC_MAGIC, 7, void *)
#define HPCAP_IOC_HUGE_MAP _IOW(HPCAP_IOC_MAGIC, 8, struct hpcap_buffer_info*)
#define HPCAP_IOC_HUGE_UNMAP _IO(HPCAP_IOC_MAGIC, 9)
#define HPCAP_IOC_STATUS_INFO _IOR(HPCAP_IOC_MAGIC, 10, struct hpcap_ioc_status_info*)
#define HPCAP_IOC_BUFCHECK _IO(HPCAP_IOC_MAGIC, 11)
#define HPCAP_IOC_KILL_LST _IOR(HPCAP_IOC_MAGIC, 12, int)
#define MAX_HUGETLB_FILE_LEN 256
#define MAX_PCI_BUS_NAME_LEN 20
#define MAX_NETDEV_NAME 10

/**
 * Structure with the layout of the RAW header.
 */
struct  __attribute__((__packed__)) raw_header {
	uint32_t sec;
	uint32_t nsec;
	uint16_t caplen;
	uint16_t len;
};

/**
 * @addtogroup HPCAP
 * @{
 */

/**
 * @internal
 * A structure holding the information that represents
 * the HPCAP buffer.
 */
struct hpcap_buffer_info {
	void* addr;			 /**< Starting address of the buffer. Read by driver. */
	size_t size;		 /**< Size of the buffer. Read/write. */
	size_t offset; 		 /**< Offset of the buffer in the page. Written by driver. */
	char file_name[MAX_HUGETLB_FILE_LEN]; /**< Name of the file that backs the hugepage buffer. R/W. */
	short has_hugepages; /**< 1 if the buffer is backed by hugepages, 0 if not. */
};

/**
 * Structures that contain the information for the status information request
 * via ioctl with command HPCAP_IOC_STATUS_INFO.
 */
struct hpcap_ioc_status_info_adapter {
	char netdev_name[MAX_NETDEV_NAME];	/**< Network interface name */
	char pdev_name[MAX_PCI_BUS_NAME_LEN];	/**< PCI bus name */
};

struct hpcap_ioc_status_info_listener {
	int id;			/**< Unique identification for this listener. */
	int kill;		/**< Used for signaling stop requests. */
	u64 bufferWrOffset; /**< Offset of the last write in the HPCAP buffer. */
	u64 bufferRdOffset; /**< Offset of the last read from the client in the buffer */
	size_t buffer_size; /**< Size of the buffer. */
};

struct hpcap_ioc_status_info {
	int num_adapters; 	/**< Number of HPCAP adapters in the array */
	struct hpcap_ioc_status_info_adapter adapters[HPCAP_MAX_NIC]; /**< Adapters info */
	struct hpcap_buffer_info bufinfo; /**< Buffer information */

	struct hpcap_ioc_status_info_listener global_listener; /**< Buffer global listener */
	int num_listeners; /**< Number of listeners in the array */
	struct hpcap_ioc_status_info_listener listeners[MAX_LISTENERS];  /**< Buffer listeners */

	size_t buffer_filesize; 	/**< Offset in the current file (keeps track of the padding) */
	long thread_state;

	size_t consumer_write_off; 	/**< Internal write offset */
	size_t consumer_read_off; 	/**< Internal read offset */
};


/**
 * @internal
 * Structure to interchange listener information and operations
 * with the driver.
 */
struct hpcap_listener_op {
	size_t ack_bytes;
	size_t expect_bytes;
	size_t available_bytes;
	uint64_t read_offset;
	uint64_t write_offset;
	uint64_t timeout_ns;
};

/** @} */

/**********************************************/

#ifndef __KERNEL__

/**
 * @addtogroup userspace
 * @{
 */

/**
 * Userspace handle that contains all the necessary information of a
 * given HPCAP adapter.
 */
struct hpcap_handle {
	int fd;				/**< File descriptor of the /dev file */

	int adapter_idx;	/**< Adapter index (e.g, hpcapX is adapter X) */
	int queue_idx;		/**< Index of the queue */

	size_t file_offset; /**< Offset inside the file. Useful to keep track of the padding at its beginning */
	uint64_t rdoff;		/**< First position of the buffer from which to read new data */
	//uint64_t lastrdoff;
	uint64_t avail;		/**< Available bytes for reading in the buffer */

	uint64_t acks; 		/**< Number of bytes read but not acknowledged to the driver */

	u_char *buf;		/**< HPCAP buffer */
	u_char *page;		/**< Pointer to the page in which the buffer is placed **/
	uint64_t bufoff;//offset inside page
	uint64_t size;
	uint64_t bufSize;

	/**
	 * @name Hugepage interaction
	 *
	 * Internal variables for the control of hugepage allocation.
	 *
	 * @{
	 */
	int hugepage_fd;		/**< File descriptor of the hugepage-backed file */
	char hugepage_name[MAX_HUGETLB_FILE_LEN];	/**< Path of the hugepage-backed file */
	void* hugepage_addr;	/**< Address of the hugepage buffer */
	size_t hugepage_len;	/**< Length of the hugepage buffer */
	/** @} */
};

#ifdef DEBUG
#define printdbg(fmt, ...) fprintf(stderr, "(D) %s: " fmt , __func__ , ## __VA_ARGS__)
#else
#define printdbg(fmt, ...) do {} while (0)
#endif

#define printerr(fmt, ...) fprintf(stderr, "%s: " fmt , __func__ , ## __VA_ARGS__)

/**
 * Open a handle for a HPCAP adapter in the given queue.
 * @param  handle      Preallocated handle structure.
 * @param  adapter_idx Adapter ID (if the interface is hpcap0, then the ID is 0).
 * @param  queue_idx   Queue ID.
 * @return             HPCAP_OK if everything went OK, HPCAP_ERR in any other case.
 *
 * @notes This function automatically zeroes the whole handle structure before using it to
 *        avoid problems.
 */
int hpcap_open(struct hpcap_handle *handle, int adapter_idx, int queue_idx);

/**
 * Closes the handle and frees any associated resources.
 * @param handle HPCAP handle.
 */
void hpcap_close(struct hpcap_handle *handle);

/**
 * Maps the internal buffer from the driver back into userspace, so the current application
 * can access it and read it.
 * @param  handle HPCAP handle.
 * @return        HPCAP_OK/HPCAP_ERR.
 */
int hpcap_map(struct hpcap_handle *handle);

/**
 * Unmap the driver buffer.
 * @param  handle HPCAP handle.
 * @return        HPCAP_OK/HPCAP_ERR.
 */
int hpcap_unmap(struct hpcap_handle *handle);

/**
 * Blocks the current thread until enough bytes are available.
 * @param  handle HPCAP handle
 * @param  count  Number of bytes that should be available before the thread
 *                	continues.
 * @return        HPCAP_OK/HPCAP_ERR.
 */
int hpcap_wait(struct hpcap_handle *handle, uint64_t count);

/**
 * Communicate to the driver the bytes we have read from the beginning
 * of the buffer (that is, the value in handle->acks). Those bytes can
 * be discarded as they've been already processed.
 * @param  handle HPCAP handle
 * @return        HPCAP_OK/HPCAP_ERR.
 */
int hpcap_ack(struct hpcap_handle *handle);

/**
 * Communicate to the driver the bytes read from the beginning of the buffer,
 * and then wait until waitcount bytes are available.
 * @param  handle    HPCAP Handle.
 * @param  waitcount Number of bytes that should be available.
 * @return           HPCAP_OK/HPCAP_ERR.
 */
int hpcap_ack_wait(struct hpcap_handle *handle, uint64_t waitcount);

/**
 * Communicate to the driver the bytes read from the beginning of the buffer,
 * and then wait until waitcount bytes are available or the timeout expires.
 * @param  handle     HPCAP handle
 * @param  waitcount  Number of bytes that should be available.
 * @param  timeout_ns Timeout in nanoseconds.
 * @return            HPCAP_OK/HPCAP_ERR
 */
int hpcap_ack_wait_timeout(struct hpcap_handle *handle, uint64_t waitcount, uint64_t timeout_ns);

/**
 * Get the current write offset of the driver buffer.
 * @param  handle HPCAP handle
 * @return        Driver offset or negative number if an error ocurred.
 */
int hpcap_wroff(struct hpcap_handle *handle);

/**
 * Get the current read offset of the driver buffer.
 * @param  handle HPCAP handle
 * @return        Driver offset or negative number if an error ocurred.
 */
int hpcap_rdoff(struct hpcap_handle *handle);

/**
 * Abort any waiting listener that called hpcap_wait or hpcap_ack_wait
 * and allow it to continue.
 * @param  handle HPCAP handle
 * @return        HPCAP_OK/HPCAP_ERR.
 */
int hpcap_ioc_killwait(struct hpcap_handle *handle);

/**
 * Kill the listener with the given ID and close the corresponding file.
 * @param  handle      Handle to the file.
 * @param  listener_id ID of the listener.
 * @return             0 if OK, negative if error.
 */
int hpcap_ioc_kill(struct hpcap_handle* handle, int listener_id);

/**
 * Retrieve information of the HPCAP buffer
 * @param  handle HPCAP handle
 * @param  info   Information structure to be filled
 * @return        HPCAP_OK/HPCAP_ERR
 */
int hpcap_status_info(struct hpcap_handle* handle, struct hpcap_ioc_status_info* info);

#ifdef REMOVE_DUPS
/**
 * Print the duplicates table for the given handle.
 * @param  handle HPCAP handle
 * @return        HPCAP_OK if everything was OK, HPCAP_ERR if
 *                the interface doesn't check for duplicates.
 */
int hpcap_dup_table(struct hpcap_handle *handle);
#endif

/**
 * Fill the pcap_pkthdr pointed by header.
 * @param header Header (struct pcap_pkthdr*).
 * @param secs   Timestamp seconds.
 * @param nsecs  Timestamp nanoseconds.
 * @param len    Frame length.
 * @param caplen Frame captured length.
 */
void hpcap_pcap_header(void *header, u32 secs, u32 nsecs, u16 len, u16 caplen);

/**
 * Return the address at the given offset of the HPCAP internal buffer, supposing that
 * the first byte is at the position of the read offset.
 * @param  handle HPCAP handle
 * @param  offset Offset in the readable part.
 * @return        Address in the buffer.
 */
uint8_t* hpcap_get_memory_at_readable_offset(struct hpcap_handle* handle, size_t offset);

/**
 * Reads the next frame from the HPCAP buffer, discarding padding frames.
 *
 * @param  hp          HPCAP handle.
 * @param  pbuffer     When returning, its content will point to the buffer
 *                     	where the packet data is (NULL if there's only padding).
 * @param  auxbuf      Pointer to an auxiliar buffer in case the packet is broken into two (circular buffer).
 * @param  header      Pointer to a header structure
 * @param  read_header Function that initializes the header pointer. It has five arguments:
 *                     	header, timestamp seconds, timestamp seconds, frame length and frame captured length.
 *
 *                     If NULL, no packet header is processed and pbuffer does not point to the packet data but
 *                      to a buffer containing RAW header + packet data
 * @return             Timestamp in nanoseconds, 0 if there are no packets available or -1 if
 *                       an error occurred.
 */
u64 hpcap_read_packet(struct hpcap_handle *hp, u_char **pbuffer, u_char *auxbuf, void *header, void (* read_header)(void *, u32, u32, u16, u16));

/**
 * Writes a block from the HPCAP buffer to the given file.
 * @param  handle             handle pointer to read data from
 * @param  fd                 file descriptor of the output file (nothing will be written if fd==0)
 * @param  max_bytes_to_write maximum amount of bytes to be written to the output file
 * @return                    number of bytes read (-1 on error)
 */
uint64_t hpcap_write_block(struct hpcap_handle *hp, int fd, uint64_t max_bytes_to_write);

/**
 * Configures the HPCAP driver to use hugepage buffers.
 *
 * The virtual hugetlbfs filesystem must be mounted previously. The following
 * command can be used:
 *     mount -t hugetlbfs \
 *       o uid=<value>,gid=<value>,mode=<value>,size=<value>,nr_inodes=<value> \
 *       none <hugetlbfs_path>
 *
 * The function always exits in a coherent state.
 *
 * @see <a href="https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt">Kernel hugetlbpage docs</a>
 * @param handle         HPCAP descriptor.
 * @param hugetlbfs_path hugetlbfs filesystem path. Max 240 characters.
 * @param bufsize        Size of the buffer that will be allocated.
 * @return               HPCAP_OK or HPCAP_ERR depending on the operation result.
 *
 * @notes Actually, the limitation of the hugetlbfs_path is not enforced to 240 characters.
 *        The limitation is implemented to avoid exceding the maximum path length when concatenating
 *        the path and the buffer file name.
 */
int hpcap_map_huge(struct hpcap_handle* handle, const char* hugetlbfs_path, size_t bufsize);

/**
 * Releases the hugepage buffer from the adapter.
 * @param handle        HPCAP handle.
 * @param notify_driver 0 if the driver should not receive the communication to release the buffer.
 *                       This option is especially useful when the driver has already been closed
 *                       and only the userspace resources have to be released.
 * @return               HPCAP_OK or HPCAP_ERR. The latter is only returned if the ioctl call (only
 *  					 done when notify_driver != 0) fails.
 */
int hpcap_unmap_huge(struct hpcap_handle* handle, short notify_driver);

/**
 * Prints the status of the handle to the given file stream.
 *
 * Data printed includes adapter, queue, file descriptor for the HPCAP buffer,
 * acknowledged/available bytes and read offset.
 * @param out    File pointer where the information will be printed.
 * @param handle HPCAP handle.
 */
void hpcap_print_listener_status(FILE* out, struct hpcap_handle* handle);

/**
 * Return whether a given header is padding or not
 * @param  header Header pointer
 * @return        1 if padding, 0 if not.
 */
short hpcap_is_header_padding(struct raw_header* header);

/**
 * @internal
 * Read the next header and frame in the buffer
 *
 * @param  handle   HPCAP handle
 * @param  rawh     [Out] Store here the direction of the raw header.
 * @param  frame    [Out] Store here the direction of the frame. Ignored if NULL.
 * @param  for_copy If not NULL, use this buffer space to copy raw header/frame body in case they
 *                  fall in the border of the buffer. If NULL, rawh/frame will only point to the start
 *                  of the respective memory zones. The user should ensure that appropiate action is taken
 *                  if they fall on borders of the buffer.
 * @return          1 if a frame was read, 0 if there was not enough available bytes.
 */
short _hpcap_read_next(struct hpcap_handle* handle, struct raw_header** rawh, uint8_t** frame, uint8_t* for_copy);

/**
 * Return the available bytes to read for the given listener.
 * @param  l Listener structure pointer
 * @return   Available bytes to read.
 */
size_t hpcap_ioc_listener_info_available_bytes(struct hpcap_ioc_status_info_listener* l);


/**
 * Return the occupation rate for the given listener, defined as the percentage
 * of the buffer that the listener hasn't read.
 *
 * @param 	l 	Listener structure pointer.
 * @return 		Occupation rate
 */
double hpcap_ioc_listener_occupation(struct hpcap_ioc_status_info_listener* l);

/** @} */

#endif /* !__KERNEL__ */

#endif	/* _HPCAP_H_ */
