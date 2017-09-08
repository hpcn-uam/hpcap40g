#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>

struct  __attribute__((__packed__)) raw_header {
	uint32_t sec;
	uint32_t nsec;
	uint16_t caplen;
	uint16_t len;
};

static long min_tstamp;
static short had_errors;
static size_t max_errors = 20;
static short ignore_caplen = 0;

#define NSECS_PER_SEC (1000000000)

static void usage()
{
	printf("checkraw: helper binary for checking the integrity of RAW files.\n");
	printf("usage: checkraw [OPTIONS] file/dir(s)\n\n");
	printf("file/dir(s) is one or more RAW files or directories where RAW files are stored.\n");
	printf("Options:\n");
	printf("  -t min_tstamp : Only check files with a timestamp greater than min_tstamp\n");
	printf("  -c            : Don't detect frames with caplen < 64 as errors.\n");
	printf("  -e max_errors : Limit of errors (default %zu) for each trace. If this number is reached, \n", max_errors);
	printf("                  the program will stop reading the file. If 0, there's no limit and checkraw\n");
	printf("                  will read everything.\n");
}

static long read_raw(const char* fname, long file_tstamp, size_t* error_count)
{
	FILE* file;
	struct raw_header header;
	size_t frame_count = 0;
	size_t read_bytes = 0;
	short found_padding = 0;

	*error_count = 0;

	file = fopen(fname, "r");

	if (file == NULL) {
		fprintf(stderr, "fopen %s: %s\n", fname, strerror(errno));
		return -1;
	}

	while (!feof(file)) {
		read_bytes = fread(&header, 1, sizeof(struct raw_header), file);

		if (max_errors > 0 && *error_count > max_errors) {
			fprintf(stderr, "Max number of errors reached, stop reading file.\n");
			break;
		}

		if (read_bytes != sizeof(struct raw_header)) {
			if (read_bytes > 0) {
				fprintf(stderr, "ERROR %s - f%zu: Corrupted header in only %zu bytes left (not enought to read the header)\n", fname, frame_count,  read_bytes);
				(*error_count)++;
			}

			break;
		}

		if (found_padding) {
			fprintf(stderr, "ERROR %s - f%zu: There's still data after padding\n", fname, frame_count);
			(*error_count)++;
			break;
		}

		frame_count++;

		if (header.sec == 0 && header.nsec == 0) {
			found_padding = frame_count > 1;

			if (header.len != header.caplen) {
				fprintf(stderr, "ERROR %s - f%zu: Wrong padding header (len = %hu, caplen = %hu)\n", fname, frame_count, header.len, header.caplen);
				(*error_count)++;
			}
		} else { // Not a padding format.
			if (header.nsec >= NSECS_PER_SEC) {
				fprintf(stderr, "ERROR %s - f%zu: Wrong NS value %u\n", fname, frame_count, header.nsec);
				(*error_count)++;
			}

			if (header.caplen > 1518) {
				fprintf(stderr, "WARN %s - f%zu: Caplen %hu > MTU\n", fname, frame_count, header.caplen);
				(*error_count)++;
			}

			if (!ignore_caplen && header.caplen < 64) {
				fprintf(stderr, "WARN %s - f%zu: Caplen %hu < min frame size\n", fname, frame_count, header.caplen);
				(*error_count)++;
			}

			if (header.len < header.caplen) {
				fprintf(stderr, "WARN %s - f%zu: Caplen %hu > len %hu\n", fname, frame_count, header.caplen, header.len);
				(*error_count)++;
			}

			if (header.sec + 1 < file_tstamp) { // Allow for a margin between timestamps
				fprintf(stderr, "WARN %s - f%zu: Timestamp %u < file tstamp %ld\n", fname, frame_count, header.sec, file_tstamp);
				(*error_count)++;
			}
		}

		fseek(file, header.caplen, SEEK_CUR);
	}

	fclose(file);

	return frame_count;
}

static void explore_file(const char* path)
{
	long file_tstamp;
	long frames;
	size_t error_count;
	const char* extension;
	char path_copy[1024]; // For basename, as it can modify the string.
	const char* filename;
	char time_str[500];
	struct tm *tmp;

	strncpy(path_copy, path, sizeof(path_copy));

	filename = basename(path_copy);

	extension = strrchr(filename, '.');

	if (extension != NULL && strcmp(extension, ".raw") == 0) {
		if (sscanf(filename, "%ld", &file_tstamp) != 1) {
			fprintf(stderr, "Cannot parse file name %s\n", filename);
			return;
		}

		if (file_tstamp < min_tstamp)
			return;

		printf("%s...\r", path);

		fflush(stdout);

		frames = read_raw(path, file_tstamp, &error_count);

		if (error_count > 0)
			had_errors = 1;

		tmp = localtime(&file_tstamp);
		strftime(time_str, sizeof(time_str), "%a %d/%m/%y %T", tmp);

		printf("%s (%s) - %zu frames | %zu errors\n", path, time_str, frames, error_count);
	}
}

static void explore_dir(const char* dirname)
{
	DIR* dir;
	struct dirent* file;
	char path[1024];

	dir = opendir(dirname);

	if (dir == NULL) {
		fprintf(stderr, "opendir(\"%s\"): %s\n", dirname, strerror(errno));
		return;
	}

	while ((file = readdir(dir)) != NULL) {
		snprintf(path, sizeof(path) * sizeof(char), "%s/%s", dirname, file->d_name);

		if (file->d_name[0] != '.' && file->d_type == DT_DIR)
			explore_dir(path);
		else if (file->d_type == DT_REG)
			explore_file(path);
	}

	closedir(dir);
}

int main(int argc, char *const *argv)
{
	const char* path;
	int opt;
	size_t i;
	struct stat s;

	min_tstamp = 0;
	had_errors = 0;

	while ((opt = getopt(argc, argv, "hct:e:")) != -1) {
		switch (opt) {
			case 't':
				min_tstamp = strtol(optarg, NULL, 10);
				break;

			case 'h':
				usage();
				return EXIT_SUCCESS;

			case 'c':
				ignore_caplen = 1;
				break;

			case 'e':
				max_errors = strtol(optarg, NULL, 10);
				break;

			default:
				usage();
				return EXIT_FAILURE;
		}
	}

	if (optind >= argc) {
		usage();
		return EXIT_FAILURE;
	}

	for (i = optind; i < argc; i++) {
		path = argv[i];

		if (stat(path, &s) == -1) {
			fprintf(stderr, "stat(\"%s\"): %s\n", path, strerror(errno));
			continue;
		}

		if (S_ISREG(s.st_mode))
			explore_file(path);
		else if (S_ISDIR(s.st_mode))
			explore_dir(path);
	}

	if (had_errors)
		return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}
