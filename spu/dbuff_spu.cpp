#include <stdio.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>

#define waitag(t) mfc_write_tag_mask(1<<t); mfc_read_tag_status_all();
#define TRANSFER_SIZE (10000)                          // dimensiunea unui transfer DMA in octeti
#define NUM_ELEMS     (TRANSFER_SIZE / sizeof(float))  // numarul de elemente procesate de un SPU la o iteratie

typedef struct {
	float* A;	// pointer to section in first input array
	float* B;	// pointer to section in second input array
	float* C;	// pointer to section of output array
	int num_elems;	// numarul total de elemente procesate de 1 SPU
} pointers_t;

void print_vector(vector float *v, int length);

void add_float_arrays(float* a, float* b, float*c, int num_elems) {
	for (int i=0; i<num_elems; i++)
		c[i] = a[i] + b[i];
}

int main(unsigned long long speid, unsigned long long argp, unsigned long long envp)
{
	unsigned int i;			               // numarul iteratiei
	int buf, nxt_buf;                              // indexul bufferului, 0 sau 1
	pointers_t p __attribute__ ((aligned(16)));    // structura cu adrese si numarul total de elemente procesate de un SPU
	uint32_t tag_id[2];	                       // de data asta avem nevoie de doua tag_id

	/* rezervare de tag ID-uri */
	tag_id[0] = mfc_tag_reserve();
	if (tag_id[0]==MFC_TAG_INVALID){
		printf("SPU: ERROR can't allocate tag ID\n"); return -1;
	}
	tag_id[1] = mfc_tag_reserve();
	if (tag_id[1]==MFC_TAG_INVALID){
		printf("SPU: ERROR can't allocate tag ID\n"); return -1;
	}

	/* transferul initial, cu structura de pointeri */
	mfc_get((void*)&p, argp, (int) envp, tag_id[0], 0, 0);
	waitag(tag_id[0]);

	/* array-uri pentru a stoca datele - 2 seturi - intr-un set vom tranfera date in timp ce
	 * vom procesa datele din celalalt set */
	float A[2][NUM_ELEMS];
	float B[2][NUM_ELEMS];
	float C[2][NUM_ELEMS];

	// primul transfer de date, in afara buclei
	buf = 0;
	mfc_get((void*)A[buf], (uint32_t)(p.A), TRANSFER_SIZE, tag_id[buf], 0, 0);
	mfc_get((void*)B[buf], (uint32_t)(p.B), TRANSFER_SIZE, tag_id[buf], 0, 0);

	i=1; // am pornit un transfer mai sus
	while (i < p.num_elems / NUM_ELEMS) { // cat timp nu s-a terminat de luat tot

		// afisare numar iteratie 
		printf("[SPU %llx] iteratia %d\n", speid, i);
		// Cer bufferul urmator de date de la PPU
		nxt_buf = buf^1;

		mfc_get((void*)A[nxt_buf], ((uint32_t)(p.A)) + i * TRANSFER_SIZE, TRANSFER_SIZE, tag_id[nxt_buf], 0, 0);
		mfc_get((void*)B[nxt_buf], ((uint32_t)(p.B)) + i * TRANSFER_SIZE, TRANSFER_SIZE, tag_id[nxt_buf], 0, 0);

		// Astept bufferul precedent de date de la PPU 
		waitag(tag_id[buf]);

		// Procesez bufferul precedent
		add_float_arrays(A[buf], B[buf], C[buf], NUM_ELEMS);

		// Trimit bufferul precedent la PPU
		mfc_put((void*)C[buf], ((uint32_t)(p.C))+(i-1) * TRANSFER_SIZE, TRANSFER_SIZE, tag_id[buf], 0, 0);

		// Pregatim urmatoarea iteratie
		buf = nxt_buf;
		i++;

	}
	printf("[SPU %llx] iteratia %d\n", speid, i);

	// Astept ultimul buffer de date de la PPU
	waitag(tag_id[buf]);

	// Procesez ultimul buffer
	add_float_arrays(A[buf], B[buf], C[buf], NUM_ELEMS);

	// Trimit ultimul buffer la PPU
	mfc_put((void*)C[buf], (uint32_t)(p.C)+(i-1)*TRANSFER_SIZE, TRANSFER_SIZE, tag_id[buf], 0, 0);
	waitag(tag_id[buf]);	 

	// eliberez tag-uri
	mfc_tag_release(tag_id[0]);
	mfc_tag_release(tag_id[1]);

	return 0;
}

void print_vector(vector float *v, int length)
{
	int i;
	for (i = 0; i < length; i+=1)
		printf("%.2lf %.2lf %.2lf %.2lf ", v[i][0], v[i][1], v[i][2], v[i][3]);
	printf("\n");
}

