/**
 * Just a program to send random data through raw sockets, useful for quick tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#define BASE_THREADS 5
#define MAX_THREADS 1000 // I don't want to start using dynamic memory, just preallocate.

volatile sig_atomic_t thread_count = 0;
volatile sig_atomic_t stop = 0;
volatile sig_atomic_t stop_thr[MAX_THREADS];
volatile int total_data = 0;

struct thread_data {
	struct sender_data* data;
	size_t thread_idx;
};

static struct thread_data thr_data[MAX_THREADS];

struct sender_data {
	struct sockaddr_in target;
	size_t frame_size;
};

void usage()
{
	printf("Usage: simple_sender IP framesize");
}

void* send_data(void* arg)
{
	struct thread_data* thr = (struct thread_data*) arg;
	int sock = -1;
	uint8_t* buffer = NULL;
	int retval;
	char errmsg[100];

	// Just to have messages with the thread index if we have to print them with perror.
	snprintf(errmsg, sizeof(errmsg) / sizeof(char), "send error thr %zu", thr->thread_idx);

	buffer = (uint8_t*) calloc(thr->data->frame_size, sizeof(uint8_t));

	if (buffer == NULL)
		goto out;

	for (size_t i = 0; i < thr->data->frame_size; i++)
		buffer[i] = 'a';

	sock = socket(PF_INET, SOCK_DGRAM, 17); // Send through UDP.

	if (sock == -1)
		goto out;

	while (!stop && !stop_thr[thr->thread_idx]) {
		retval = sendto(sock, buffer, thr->data->frame_size, 0,
						(struct sockaddr*) &thr->data->target, sizeof(struct sockaddr_in));

		if (retval <= 0) {
			perror(errmsg);
			goto out;
		}

		__sync_fetch_and_add(&total_data, retval);
	}

out:

	if (buffer)
		free(buffer);

	if (sock > 0)
		close(sock);

	printf("Thread %zu down!\n", thr->thread_idx);

	return NULL;
}

void cancel_work(int signal)
{
	__sync_fetch_and_add(&stop, 1);
	sleep(1);
	raise(SIGTERM);
}

void create_threads(pthread_t* threads, struct sender_data* data, size_t count)
{
	size_t initial_threads = thread_count;

	for (size_t i = 0; i < count; i++) {
		size_t thread_idx = initial_threads + i;
		thr_data[thread_idx].thread_idx = thread_idx;
		thr_data[thread_idx].data = data;
		stop_thr[thread_idx] = 0;

		if (pthread_create(threads + thread_idx, NULL, send_data, thr_data + thread_idx) == 0) {
			printf("Thread number %zu created\n", thread_idx);
			__sync_fetch_and_add(&thread_count, 1);
		} else
			perror("Error creating thread");
	}
}

void stop_threads(pthread_t* threads, size_t count)
{
	size_t initial_threads = thread_count;

	for (int i = initial_threads - 1; i >= (int)(initial_threads - count); i--) {
		stop_thr[i] = 1;
		__sync_fetch_and_sub(&thread_count, 1);
		printf("Thread number %d deleted (%d now)\n", i, thread_count);
	}
}

void* reporter(void* arg)
{
	int previous_data = 0;
	int current_data = 0;
	time_t start = *(time_t*)arg;
	time_t end;
	double rate, avg_rate;

	while (!stop) {
		sleep(1);

		end = time(NULL);
		current_data = total_data;

		rate = (double)(current_data - previous_data);
		rate /= 1000000; // Conver to MBps
		rate *= 8; // Convert to Mbps

		previous_data = current_data;

		avg_rate = 8 * (double)(current_data) / (1000000 * (end - start));

		printf("\rRate: %.3f Mbps. Average: %.3f Mbps", rate, avg_rate);
		fflush(stdout);
	}

	return NULL;
}

int main(int argc, char const *argv[])
{
	struct sender_data data;
	const char* addr_str = argv[1];
	pthread_t threads[MAX_THREADS];
	pthread_t report_thread;
	time_t start;

	if (argc != 3) {
		usage();
		exit(1);
	}

	srand(time(NULL));

	if (!inet_pton(AF_INET, addr_str, &data.target.sin_addr)) {
		fprintf(stderr, "Error parsing IP\n");
		exit(1);
	}

	data.target.sin_family = AF_INET;
	data.target.sin_port = 100; // Random, we don't care.

	data.frame_size = strtol(argv[2], NULL, 10);

	printf("Sending frames of size %zu to %s\n", data.frame_size, addr_str);

	if (signal(SIGABRT, cancel_work) == SIG_ERR)
		perror("Can't register for SIGABRT");

	if (signal(SIGINT, cancel_work) == SIG_ERR)
		perror("Can't register for SIGINT");

	start = time(NULL);
	pthread_create(&report_thread, NULL, reporter, &start);
	create_threads(threads, &data, BASE_THREADS);

	while (!stop) {
		int change;

		if (scanf("%d", &change) == 1) {
			if (change > 0) {
				printf("Creating %d threads\n", change);
				create_threads(threads, &data, change);
			} else if (change < 0) {
				change = thread_count < -change ? thread_count : -change;

				if (change > 0) {
					printf("Killing %d threads\n", change);
					stop_threads(threads, change);
				}
			}

		}
	}

	return 0;
}
