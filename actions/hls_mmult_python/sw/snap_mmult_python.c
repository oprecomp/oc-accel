/*
 * Copyright 2019 International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * SNAP Matrix Multiplication Example
 *
 * Demonstration how to get data into the FPGA, process it using a SNAP
 * action and move the data out of the FPGA back to host-DRAM.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <malloc.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <assert.h>

/* FIXME For bypassing "expected unqualified-id" error of osnap_tools.h with g++ compiler for "Fake 
         this for old RHEL verions e.g. RHEL5.6"
*/
#define CLOCK_MONOTONIC_RAW
#include <osnap_tools.h>
#include <libosnap.h>
#include <action_mmult_python.h>
#include <osnap_hls_if.h>

#include <math.h>

#ifdef FLOATX_ENABLE
#include "floatx.hpp"
#endif

int verbose_flag = 0;

static const char *version = GIT_VERSION;

static const char *mem_tab[] = { "HOST_DRAM", "CARD_DRAM", "TYPE_NVME" };

// Function that fills the MMIO registers / data structure
// these are all data exchanged between the application and the action
static void snap_prepare_mmult(struct snap_job *cjob,
				 struct mmult_job *mjob,
			         uint32_t a_row,
			         uint32_t a_col,
			         uint32_t b_col,
				 uint64_t offset_b,
				 uint64_t addr_in1,
				 uint64_t addr_in2,
				 uint32_t size_in,
				 uint8_t type_in,
				 uint64_t addr_out,
				 uint32_t size_out,
				 uint8_t type_out)
{
	fprintf(stderr, "  prepare mmult job of %ld bytes size\n", sizeof(*mjob));

	assert(sizeof(*mjob) <= SNAP_JOBSIZE);
	memset(mjob, 0, sizeof(*mjob));

	mjob->a_row = a_row;
	mjob->a_col = a_col;
	mjob->b_col = b_col;
	mjob->offset_to_point_b = offset_b;


	float source_in1_align_f = ceil((float)addr_in1/64.0) - (float)addr_in1 / 64.0;
	mjob->addr_in1_index = (uint32_t)(source_in1_align_f*16);

	float source_in2_align_f = ceil((float)addr_in2/64.0) - (float)addr_in2 / 64.0;
	mjob->addr_in2_index = (uint32_t)(source_in2_align_f*16);

	float source_out_align_f = ceil((float)addr_out/64.0) - (float)addr_out / 64.0;
	mjob->addr_out_index = (uint32_t)(source_out_align_f*16);

	printf("DEBUG snap_prepare_mmult: source_in1_align_f=%f, addr_in1_index=%u,  source_in2_align_f=%f, addr_in2_index=%u, source_out_align_f=%f, addr_out_index=%u\n", 
		source_in1_align_f, mjob->addr_in1_index, source_in2_align_f, mjob->addr_in2_index, source_out_align_f, mjob->addr_out_index);

	// Setting input params : where text is located in host memory
	snap_addr_set(&mjob->in, (void *)addr_in1, size_in, type_in,
		      SNAP_ADDRFLAG_ADDR | SNAP_ADDRFLAG_SRC);
	// Setting output params : where result will be written in host memory
	snap_addr_set(&mjob->out, (void *)addr_out, size_out, type_out,
		      SNAP_ADDRFLAG_ADDR | SNAP_ADDRFLAG_DST |
		      SNAP_ADDRFLAG_END);


	snap_job_set(cjob, mjob, sizeof(*mjob), NULL, 0);
}


// Software implementation of Matrix Multiplication
// The inputs are of the size (DATA_SIZE x DATA_SIZE)
void m_softwareGold(int*, int*, int*);
void m_softwareGold(
    int* in1, //Input Matrix 1
	int* in2, //Input Matrix 2
	int* out  //Output Matrix
) {
    int i, j, k;
    //Perform Matrix multiply Out = In1 x In2
    for (i = 0; i < DATA_SIZE; i++) {
        for (j = 0; j < DATA_SIZE; j++) {
            for (k = 0; k < DATA_SIZE; k++) {
                out[i * DATA_SIZE + j] +=
                    in1[i * DATA_SIZE + k] * in2[k * DATA_SIZE + j];
            }
        }
  }
}



/* main program of the application for the hls_mmult example        */
/* This application will always be run on CPU and will call either       */
/* a software action (CPU executed) or a hardware action (FPGA executed) */
#ifdef PY_WRAP
int mmult(int * input_arr_a, int size_in_a, int * input_arr_b, int size_in_b, int * output_arr_c, int size_out) {
	assert(size_in_a == DATA_SIZE*DATA_SIZE);
	assert(size_in_b == DATA_SIZE*DATA_SIZE);
	assert(size_out  == DATA_SIZE*DATA_SIZE);
#else
int main(int argc)
{
	assert(argc == 1);
#endif
	// Init of all the default values used
	int rc = 0;
	int card_no = 0;
	struct snap_card *card = NULL;
	struct snap_action *action = NULL;
	char device[128];
	struct snap_job cjob;
	struct mmult_job mjob;
	unsigned long timeout = 600;
	struct timeval etime, stime;

	//int *ibuff = NULL, 
	int *obuff = NULL;
	uint8_t type_in = SNAP_ADDRTYPE_HOST_DRAM;
	uint64_t addr_in1 = 0x0ull;
	uint64_t addr_in2 = 0x0ull;
	uint8_t type_out = SNAP_ADDRTYPE_HOST_DRAM;
	uint64_t addr_out = 0x0ull;
	int exit_code = EXIT_SUCCESS;

        uint32_t a_row;	// Matrix A Row Size
        uint32_t a_col;	// Matrix A Col Size
        uint32_t b_col;	// Matrix B Col Size
	a_row = a_col = b_col = DATA_SIZE;
	int64_t offset_b;
	size_t i;

	// default is interrupt mode enabled (vs polling)
	//snap_action_flag_t action_irq = (SNAP_ACTION_DONE_IRQ | SNAP_ATTACH_IRQ);
	snap_action_flag_t action_irq = SNAP_ACTION_DONE_IRQ;


	//Allocate Memory in Host Memory
	if (DATA_SIZE > MAX_SIZE) {
        	printf( "Size is bigger than internal buffer size, please use a size smaller than %u !\n", 
                  MAX_SIZE);
		return EXIT_FAILURE;
	}

	size_t matrix_size = DATA_SIZE * DATA_SIZE;
	size_t matrix_size_bytes = sizeof(int) * matrix_size;

	ssize_t isize = matrix_size_bytes * 2;
	ssize_t osize = matrix_size_bytes;


#ifdef PY_WRAP
	int *source_in1 = input_arr_a;
	int *source_in2 = input_arr_b;
#else	
	int *source_in1 = snap_malloc(matrix_size_bytes);
	int *source_in2 = snap_malloc(matrix_size_bytes);

	if ((source_in1 == NULL) || (source_in2 == NULL)) {
        	printf( "Failed to allocate memory. Aborting...\n");
		return EXIT_FAILURE;
	}

	// Create the test data and Software Result
	for (i = 0; i < matrix_size; i++) {
		source_in1[i] = i % 10;
	        source_in2[i] = i;
    	}
#endif

	int *source_sw_results = snap_malloc(matrix_size_bytes);
	int *source_hw_results = snap_malloc(matrix_size_bytes);
	if ((source_hw_results == NULL) || (source_sw_results == NULL)) {
        	printf( "Failed to allocate memory. Aborting...\n");
		return EXIT_FAILURE;
	}
	memset(source_sw_results, 0x0, matrix_size_bytes);
	memset(source_hw_results, 0x0, matrix_size_bytes);

	printf("DEBUG pointers: source_in1=%p, source_in1=%p, offset=%lu\n", source_in1, source_in2, source_in2 - source_in1);

	/* Allocate in host memory the place to put the text to process */
	//ibuff = snap_malloc(isize); //64Bytes aligned malloc
	//if (ibuff == NULL)
	//	goto out_error;
	//memset(ibuff, 0, isize);


	// copy array to host memory FIXME: we don't need copy
	// memcpy (ibuff, source_in1, isize/2);
	// memcpy (ibuff+matrix_size, source_in2, isize/2);


	// prepare params to be written in MMIO registers for action
	type_in = SNAP_ADDRTYPE_HOST_DRAM;
	//addr_in1 = (unsigned long)ibuff;
	addr_in1 = (unsigned long)source_in1;
	addr_in2 = (unsigned long)source_in2;

	float offset_b_f = 8.0;//(float)((source_in2-source_in1) * sizeof(int)) / 64;
	offset_b = (int64_t)ceil(offset_b_f);  //FIXME: can be 128 for 1k AXI I/F

	float source_in1_align_f = (float)addr_in1 / 64.0;
	float source_in2_align_f = (float)addr_in2 / 64.0;


#ifdef PY_WRAP
	obuff = output_arr_c;
#else
	/* Allocate in host memory the place to put the text processed */
	obuff = snap_malloc(osize); //64Bytes aligned malloc
	if (obuff == NULL)
		goto out_error;
	memset(obuff, 0x0, osize);
#endif

	// prepare params to be written in MMIO registers for action
	type_out = SNAP_ADDRTYPE_HOST_DRAM;
	addr_out = (unsigned long)obuff;


	printf("Git version: %s\n", version);
	/* Display the parameters that will be used for the example */
	printf("PARAMETERS:\n"
	       "  type_in:     %x %s\n"
	       "  addr_in1:     %016llx (%llu)\n"
	       "  addr_in2:     %016llx (%llu)\n"
	       "  source_in1:  %016llx\n"
	       "  source_in2:  %016llx\n"
	       "  source_sw_results:  %016llx\n"
	       "  type_out:    %x %s\n"
	       "  addr_out:    %016llx\n"
	       "  size_in :    %08lx\n"
	       "  size_out:    %08lx\n"
	       "  offset_b:    %016llx\n"
	       "  offset_b_f:  %f\n"
	       "  source_in1_align_f:  %f\n"
	       "  source_in2_align_f:  %f\n",
	       type_in,  mem_tab[type_in],  (long long)addr_in1, (long long)addr_in1, (long long)addr_in2, (long long)addr_in2,
	       source_in1, source_in2, source_sw_results,
	       type_out, mem_tab[type_out], (long long)addr_out,
	       isize, osize, (long long)offset_b, offset_b_f, source_in1_align_f, source_in2_align_f);


	// Allocate the card that will be used
        if(card_no == 0)
                snprintf(device, sizeof(device)-1, "IBM,oc-snap");
        else
                snprintf(device, sizeof(device)-1, "/dev/ocxl/IBM,oc-snap.000%d:00:00.1.0", card_no);

	card = snap_card_alloc_dev(device, SNAP_VENDOR_ID_IBM,
				   SNAP_DEVICE_ID_SNAP);
	if (card == NULL) {
		fprintf(stderr, "err: failed to open card %u: %s\n",
			card_no, strerror(errno));
                fprintf(stderr, "Default mode is FPGA mode.\n");
                fprintf(stderr, "Did you want to run CPU mode ? => add SNAP_CONFIG=CPU before your command.\n");
                fprintf(stderr, "Otherwise make sure you ran snap_find_card and snap_maint for your selected card.\n");
		goto out_error;
	}

	// Attach the action that will be used on the allocated card
	action = snap_attach_action(card, ACTION_TYPE, action_irq, 60);
	if(action_irq)
		snap_action_assign_irq(action, ACTION_IRQ_SRC_LO);
	if (action == NULL) {
		fprintf(stderr, "err: failed to attach action %u: %s\n",
			card_no, strerror(errno));
		goto out_error1;
	}

	// Fill the stucture of data exchanged with the action
	snap_prepare_mmult(&cjob, &mjob, 
			     a_row, a_col, b_col, offset_b,
			     addr_in1, addr_in2, isize, type_in,
			     addr_out, osize, type_out);

	// uncomment to dump the job structure
	//__hexdump(stderr, &mjob, sizeof(mjob));


	// Collect the timestamp BEFORE the call of the action
	gettimeofday(&stime, NULL);

	// Call the action will:
	//    write all the registers to the action (MMIO)
	//  + start the action
	//  + wait for completion
	//  + read all the registers from the action (MMIO)
	rc = snap_action_sync_execute_job(action, &cjob, timeout);

	// Collect the timestamp AFTER the call of the action
	gettimeofday(&etime, NULL);
	if (rc != 0) {
		fprintf(stderr, "err: job execution %d: %s!\n", rc,
			strerror(errno));
		goto out_error2;
	}


	memcpy (source_hw_results, obuff, osize);


	// test return code
	(cjob.retc == SNAP_RETC_SUCCESS) ? fprintf(stdout, "SUCCESS\n") : fprintf(stdout, "FAILED\n");
	if (cjob.retc != SNAP_RETC_SUCCESS) {
		fprintf(stderr, "err: Unexpected RETC=%x!\n", cjob.retc);
		goto out_error2;
	}

	// Compute Software Results
	m_softwareGold(source_in1, source_in2, source_sw_results);

	// Compare the results of the Device to the simulation
	int match = 0;
	for (i = 0; i < DATA_SIZE * DATA_SIZE; i++) {
		if (source_hw_results[i] != source_sw_results[i]) {
			printf("Error: Result mismatch\n");
			printf("i = %d, CPU result = %d, Device result = %d\n", i, source_sw_results[i], source_hw_results[i]);
			match = 1;
			//break;
		}
		//else
		//	printf("i = %d, CPU result = Device result = %d\n", i, source_hw_results[i]);
	}

	printf("TEST %s\n", (match ? "FAILED" : "PASSED"));

#ifndef PY_WRAP
	__free(source_in1);
	__free(source_in2);
#endif
	__free(source_hw_results);
	__free(source_sw_results);

	exit_code = (match ? EXIT_FAILURE : EXIT_SUCCESS);

	// Display the time of the action call (MMIO registers filled + execution)
	fprintf(stdout, "SNAP mmult_python took %lld usec\n",
		(long long)timediff_usec(&etime, &stime));

	// Detach action + disallocate the card
	snap_detach_action(action);
	snap_card_free(card);

#ifndef PY_WRAP
	__free(obuff);
	//__free(ibuff);
#endif

#ifdef PY_WRAP
	return(exit_code);
#else
	exit(exit_code);
#endif

 out_error2:
	snap_detach_action(action);
 out_error1:
	snap_card_free(card);
 out_error:
#ifndef PY_WRAP
	__free(obuff);
	//__free(ibuff);
#endif
	exit(EXIT_FAILURE);
}
