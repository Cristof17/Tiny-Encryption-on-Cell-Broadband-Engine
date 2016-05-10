#define _ISOC11_SOURCE

#include <stdio.h>
#include <cstdlib>
#include <errno.h>
#include <libspe2.h>
#include <pthread.h>
#include "utils.h"
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>

extern spe_program_handle_t dbuff_spu;

#define MAX_SPU_THREADS   16
#define ARR_SIZE	400000


#define DELTA    0x9e3779b9
#define GET_TIME_DELTA(t1, t2) ((t2).tv_sec - (t1).tv_sec + \
		((t2).tv_usec - (t1).tv_usec) / 1000000.0)
#ifdef DEBUG
#define NUM_ROUNDS 1
#define DPRINT printf
#define DEC_SUM DELTA
#else
#define DPRINT(...) 
#define NUM_ROUNDS 32
#define DEC_SUM 0xC6EF3720
#endif

#define LINE_SIZE 128
#define PATH_SIZE 40

int num_spe;
int mod_vector;
int mod_dma;

typedef struct {
	unsigned int* IN;	// pointer to section in first input array
	unsigned int* KEY;	// pointer to section in second input array
	unsigned int* OUT;	// pointer to section of output array
	unsigned int size;
	unsigned int double_bufferring;
	unsigned int vector;
	unsigned int offset;
	int num_elems;	// numarul de elemente procesate de 1 SPU
	char padding[16];
} pointers_t;

void *ppu_pthread_function(void *thread_arg) {

	spe_context_ptr_t ctx;
	pointers_t *arg = (pointers_t *) thread_arg;

	/* Create SPE context */
	if ((ctx = spe_context_create (0, NULL)) == NULL) {
		perror ("Failed creating context");
		exit (1);
	}

	/* Load SPE program into context */
	if (spe_program_load (ctx, &dbuff_spu)) {
		perror ("Failed loading program");
		exit (1);
	}

	/* Run SPE context */
	unsigned int entry = SPE_DEFAULT_ENTRY;
	if (spe_context_run(ctx, &entry, 0, arg, (void*)sizeof(pointers_t), NULL) < 0) {
		perror ("Failed running context");
		exit (1);
	}

	/* Destroy context */
	if (spe_context_destroy (ctx) != 0) {
		perror("Failed destroying context");
		exit (1);
	}

	return NULL;
}

void encrypt_block (unsigned int* v, unsigned int* k) {
	unsigned int i, sum = 0;
	for (i=0; i < NUM_ROUNDS; i++) { 
		sum += DELTA;
		// trebuie sa fie unsigned ca sa puna bitii de la stanga pe zero la shift right
		v[0] += ((v[1]<<4) + k[0]) ^ (v[1] + sum) ^ ((v[1]>>5) + k[1]);
		v[1] += ((v[0]<<4) + k[2]) ^ (v[0] + sum) ^ ((v[0]>>5) + k[3]);
		DPRINT("\tround = %d sum = %08x v0 = %08x v1 = %08x\n", i, sum, v[0], v[1]);
	}
}

void decrypt_block (unsigned int* v, unsigned int* k) {
	unsigned int sum = DEC_SUM, i;
	for (i=0; i<NUM_ROUNDS; i++) { 
		// trebuie sa fie unsigned ca sa puna bitii de la stanga pe zero la shift right
		v[1] -= ((v[0]<<4) + k[2]) ^ (v[0] + sum) ^ ((v[0]>>5) + k[3]);
		v[0] -= ((v[1]<<4) + k[0]) ^ (v[1] + sum) ^ ((v[1]>>5) + k[1]);
		sum -= DELTA;
		DPRINT("\tround = %d sum = %08x v0 = %08x v1 = %08x\n", i, sum, v[0], v[1]);	
	}                             
}

void encrypt (unsigned int* v, unsigned int* k, int num_ints) {
	DPRINT("encrypt: num_ints = %d\n", num_ints);
	for (int i = 0; i < num_ints; i += 2) { 
		DPRINT("encrypt: block = %d\n", i / 2);
		encrypt_block(&v[i], k);
	}
}

void decrypt (unsigned int* v, unsigned int* k, int num_ints)  {
	DPRINT("decrypt: num_ints = %d\n", num_ints);
	for (int i = 0; i < num_ints; i += 2) {
		DPRINT("decrypt: block = %d\n", i / 2);
		decrypt_block(&v[i], k);
	}
}

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
	buf = memalign(16, size);
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

void process_single (char op, char* in, char* key, char* out) {
	struct timeval t1, t2, t3, t4;
	double total_time = 0, cpu_time = 0;
	int in_size, key_size, num_ints;

	int chunk_size;
	int i = 0;

	unsigned int *IN __attribute__ ((aligned(16)));
	unsigned int *KEY __attribute__ ((aligned(16)));
	unsigned int *OUT __attribute__ ((aligned(16)));

	gettimeofday(&t3, NULL);
	IN = (unsigned int*) _read_file(in, &in_size);
	KEY = (unsigned int*) _read_file(key, &key_size);
	OUT = (unsigned int *)memalign(16, in_size * sizeof(unsigned int)); 
	printf("In size = %d, key_size = %d\n", in_size,  key_size);

	chunk_size = in_size/num_spe;
	/*
	 * Send the data to spe's process it and then return the
	 * processed value
	 */

	pthread_t threads[num_spe];
	pointers_t thread_arg[num_spe] __attribute__ ((aligned(16)));
	/* 
	 * Create several SPE-threads to execute 'ex1_spu'.
	 */

	printf("Size of struct in ppu = %d\n", sizeof(pointers_t));
	if (chunk_size > (16 * 1024)) {
		/* 
 		 * Send chunks of 16KB
 		 */
		printf("Sending chunks of 16KB\n");
		int size_copy = in_size;
		int count = 0; //number of 16KB blocks sent 
		while( size_copy > 0){
			for(i = 0; i < num_spe; i++) {
				printf("Start address in PPU = %p\n", IN + (count * 16 * 1024));
				thread_arg[i].IN = IN + (count * 16 * 1024);
				thread_arg[i].KEY = KEY + (count * 16 * 1024);
				thread_arg[i].OUT = OUT + (count * 16 * 1024);
				thread_arg[i].num_elems = (16 * 1024);
				thread_arg[i].double_bufferring = 0;
				thread_arg[i].vector = 0;
				count++;
				printf("Count = %d\n", count);
				size_copy -= (16 * 1024);

				if (pthread_create (&threads[i], NULL, &ppu_pthread_function, &thread_arg[i]))  {
					perror ("Failed creating thread");
					exit (1);
				}
			}

			/* Wait for SPU-thread to complete execution.  */
			for (i = 0; i < num_spe; i++) {
				if (pthread_join (threads[i], NULL)) {
					perror("Failed pthread_join");
					exit (1);
				}
			}
		}
	} 
	else {
		for (i = 0; i < num_spe; ++i) {
			for(i = 0; i < num_spe; i++) {
				thread_arg[i].IN = IN + (i * chunk_size);
				thread_arg[i].KEY = KEY + (i * chunk_size);
				thread_arg[i].OUT = OUT + (i * chunk_size);
				thread_arg[i].num_elems = chunk_size;
				thread_arg[i].double_bufferring = 0;
				thread_arg[i].vector = 0;
				thread_arg[i].offset = i;

				if (pthread_create (&threads[i], NULL, &ppu_pthread_function, &thread_arg[i]))  {
					perror ("Failed creating thread");
					exit (1);
				}
			}

			/* Wait for SPU-thread to complete execution.  */
			for (i = 0; i < num_spe; i++) {
				if (pthread_join (threads[i], NULL)) {
					perror("Failed pthread_join");
					exit (1);
				}
			}
		}
	}
	if (key_size != 4 * sizeof(int)) {
		printf("Invalid key file %s\n", key);
		return;
	}

	gettimeofday(&t1, NULL);
	num_ints = in_size / sizeof(int);
	switch(op) {
		case 'e':
			/*
			 * Modifica blocul initial
			 */
			encrypt(IN, KEY, num_ints);
			break;
		case 'd':
			/*
			 * La fel, modifica blocul initial
			 */
			decrypt(IN, KEY, num_ints);
			break;
		default:
			printf("Usage: ./serial e/d in key out\n");
			return ;

	}
	gettimeofday(&t2, NULL);

	_write_file(out, IN, in_size);
	free(IN); 
	free(KEY);
	gettimeofday(&t4, NULL);

	total_time += GET_TIME_DELTA(t3, t4);
	cpu_time += GET_TIME_DELTA(t1, t2);

	if (op == 'e') 
		printf("Encrypted [%s,%d] in [CPU=%lf, Total=%lf]\n", in, 
				in_size, cpu_time, total_time);
	else
		printf("Decrypted [%s,%d] in [CPU=%lf, Total=%lf]\n", in, 
				in_size, cpu_time, total_time);

}

int main(int argc, char **argv)
{
	
	/*
	 * Check if arguments match
	 */
	if (argc == 8) {
		num_spe = atoi(argv[1]);
		mod_vector = atoi(argv[2]);
		mod_dma = atoi(argv[3]);
		process_single(argv[4][0], argv[5], argv[6], argv[7]);
		return 0;
	} else if (argc == 2) {
		//process_multi(argv[1]);
		return 0;
	}
	return 0;
}

