#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"

int _open_for_read(char* path){
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0){
		fprintf(stderr, "%s: Error opening %s\n", __func__, path);
		exit(0);
	}
	return fd;
}

int _open_for_write(char* path){
	int fd;

	fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0644);
	if (fd < 0){
		fprintf(stderr, "%s: Error opening %s\n", __func__, path);
		exit(0);
	}
	return fd;
}

void _write_file(char* filepath, void* buf, int size){
	char *ptr;
	int left_to_write, bytes_written, fd;

	fd = _open_for_write(filepath);

	ptr = (char*)buf;
	left_to_write = size;

	while (left_to_write > 0){
		bytes_written = write(fd, ptr, left_to_write);
		if (bytes_written <= 0){
			fprintf(stderr, "%s: Error writing buffer. "
					"fd=%d left_to_write=%d size=%d bytes_written=%d\n", 
					__func__, fd, left_to_write, size, bytes_written);
			exit(0);
		}
		left_to_write -= bytes_written;
		ptr += bytes_written;
	}

	close(fd);

}

void* _read_file(char* filepath, int* size_ptr){
	char *ptr;
	int left_to_read, bytes_read, size, fd;
	void* buf;

	fd = _open_for_read(filepath);

	size = lseek(fd, 0, SEEK_END);
	if (size <= 0) {
		fprintf(stderr, "%s: Error getting file size. filepath=%s\n",
				__func__, filepath);
		exit(0);
	}
	buf = malloc(size);
	if (!buf) {
		fprintf(stderr, "%s: Error allocating %d bytes\n", __func__,
				size);
		exit(0);   
	}
	lseek(fd, 0, SEEK_SET);

	ptr = (char*) buf;
	left_to_read = size;
	while (left_to_read > 0){
		bytes_read = read(fd, ptr, left_to_read);
		if (bytes_read <= 0){
			fprintf(stderr, "%s: Error reading buffer. "
					"fd=%d left_to_read=%d size=%d bytes_read=%d\n", 
					__func__, fd, left_to_read, size, bytes_read);
			exit(0);
		}
		left_to_read -= bytes_read;
		ptr += bytes_read;
	}

	close(fd);
	*size_ptr = size;

	return buf;
}
