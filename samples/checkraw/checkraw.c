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
static FILE* sizes_file = NULL;
static short fix_errors = 0;

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
	printf("  -x sizes_file : Output to this file the sizes and indexes of the packets\n");
	printf("  -f            : Try to fix the errors in the capture file. Currently, it\n");
	printf("                  only can fix the size of the last packet, useful for files\n");
	printf("                  where the capture stopped before filling the 2GB of the file.\n");
}

static short fix_capture(FILE* file, size_t frame_start_offset, size_t file_size)
{
	struct raw_header header;
	size_t read_bytes;
	size_t payload_start;
	size_t payload_size;

	fseek(file, frame_start_offset, SEEK_SET);
	read_bytes = fread(&header, 1, sizeof(struct raw_header), file);

	if (read_bytes < sizeof(struct raw_header)) {
		fprintf(stderr, "Cannot retrieve original header, so writing a padding header\n");

		header.sec = 0;
		header.nsec = 0;
		header.len = 0;
		header.caplen = 0;
	} else {
		payload_start = frame_start_offset + sizeof(struct raw_header);

		if (payload_start > file_size)
			payload_start = file_size;

		payload_size = file_size - payload_start;
		fprintf(stderr, "Setting last packet with caplen = %zu instead of %hu\n", payload_size, header.caplen);

		header.caplen = payload_size;

		if (payload_size < 60) {
			fprintf(stderr, "Last frame is to small to have proper data. Setting as padding.\n");
			header.sec = 0;
			header.nsec = 0;
			header.len = header.caplen;
		}
	}

	fseek(file, frame_start_offset, SEEK_SET);

	if (fwrite(&header, sizeof(struct raw_header), 1, file) < 1) {
		fprintf(stderr, "ERROR: Could not rewrite the header\n");
		perror("fwrite");
		return 0;
	}

	return 1;
}

static long read_raw(const char* fname, long file_tstamp, size_t* error_count)
{
	FILE* file;
	struct raw_header header;
	size_t frame_count = 0;
	size_t read_bytes = 0;
	short found_padding = 0;
	size_t file_size = 0;
	size_t frame_start_position = 0;

	*error_count = 0;

	if (fix_errors)
		file = fopen(fname, "r+");
	else
		file = fopen(fname, "r");

	if (file == NULL) {
		fprintf(stderr, "fopen %s: %s\n", fname, strerror(errno));
		return -1;
	}

	if (sizes_file != NULL) {
		fprintf(sizes_file, "# Reading RAW file %s\n", fname);
		fprintf(sizes_file, "# Columns: 1-Frame_index 2-Offset_in_file 3-caplen 4-len 5-timestamp\n");
	}

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);

	while (!feof(file)) {
		frame_start_position = ftell(file);

		read_bytes = fread(&header, 1, sizeof(struct raw_header), file);

		if (max_errors > 0 && *error_count > max_errors) {
			fprintf(stderr, "Max number of errors reached, stop reading file.\n");
			break;
		}

		if (read_bytes != sizeof(struct raw_header)) {
			if (read_bytes > 0) {
				fprintf(stderr, "ERROR %s - f%zu: Corrupted header in only %zu bytes left (not enought to read the header)\n", fname, frame_count,  read_bytes);

				if (fix_errors && fix_capture(file, frame_start_position, file_size))
					fprintf(stderr, "Fixed!\n");
				else
					(*error_count)++;
			}

			break;
		}

		if (sizes_file != NULL) {
			fprintf(sizes_file, "%zu %lu %hu %hu %u.%u\n",
					frame_count, ftell(file), header.caplen, header.len,
					header.sec, header.nsec);
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

			if (!ignore_caplen && header.caplen < 60) {
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

		if (fseek(file, header.caplen, SEEK_CUR) != 0) {
			fprintf(stderr, "ERROR %s - f%zu: Cannot advance %hu bytes in file (current position = %lu)\n", fname, frame_count, header.caplen, ftell(file));
			(*error_count)++;
		}

		if (ftell(file) > file_size) {
			fprintf(stderr, "ERROR %s - f%zu: Advanced past the end of the file (file size is %zu bytes, caplen %hu)\n", fname, frame_count, file_size, header.caplen);

			if (fix_errors) {
				fprintf(stderr, "Will try to fix this error...\n");

				if (!fix_capture(file, frame_start_position, file_size)) {
					fprintf(stderr, "Could not fix the error :(\n");
					(*error_count)++;
				} else {
					fprintf(stderr, "Fixed!\n");
					break;
				}
			} else
				(*error_count)++;
		}
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

	while ((opt = getopt(argc, argv, "hcft:e:x:")) != -1) {
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

			case 'x':
				sizes_file = fopen(optarg, "w");

				if (sizes_file == NULL)
					perror("fopen sizes_file");

				break;

			case 'f':
				fix_errors = 1;
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
