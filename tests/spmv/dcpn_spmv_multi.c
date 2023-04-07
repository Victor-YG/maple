
//**************************************************************************
// Double-precision sparse matrix-vector multiplication benchmark
//--------------------------------------------------------------------------

// This executes dcpn_spmv twice but with symmetric cores. 
// One core is access core for one run and the exec core for anothe run, vice versa.

#include <stdio.h>
#include <stdatomic.h>
#include "util.h"
#include "../maple/api/dcp_maple.h"
#if SIZE == 5
    #include "../maple/tests/data/spmv_data_sq_big.h"
#elif SIZE == 4
    #include "../maple/tests/data/spmv_data_sq_small.h"
#elif SIZE == 3
    #include "../maple/tests/data/spmv_data_big.h"
#elif SIZE == 2 
    #include "../maple/tests/data/spmv_data_small.h"
#else
    #include "../maple/tests/data/spmv_data_tiny.h"
#endif
#define RES 1
#define FINE 1
//#define DOUBLEP 1

#ifndef NUM_A
    #define NUM_A 1
#endif
#ifndef NUM_E
    #define NUM_E 1
#endif

// If we have same amount of A and E, FIFO count is NUM_A 
#define NUM NUM_A

// <TODO> add one fifo per row

void _kernel_(uint32_t access_id, uint32_t core_num){
    uint32_t exec_id;
    uint32_t access_row;
    uint32_t execute_row;
    uint32_t exec_fifo = 0; // <fix> make compatible with multiple queues per tile
    uint32_t access_fifo = 0; // <fix> make compatible with multiple queues per tile
    uint32_t k_access;
    uint32_t k_execute;
    uint32_t access_ipr = 0;
    uint32_t execute_ipr = 0;

    if(access_id < NUM_A) {
        exec_id = access_id + NUM;
    } else {
        exec_id = access_id - NUM;
    }

    exec_fifo = exec_id;
    access_fifo = access_id;
    // Setup FIFOs
    dec_open_producer(access_id);
    dec_set_base64(access_id,x);
    dec_open_consumer(exec_id);

    // ACCESS
    access_row = 0;
access:
    for (; access_row < R; access_row++) {
        access_ipr = 1;
        #ifdef PRI
        printf("P\n");
        #endif

        LK;printf("access_id: %d, access_row: %d\n", access_id, access_row);ULK;

        int end = ptr[access_row+1];
        int endm1 = end-1;
        k_access = ptr[access_row];

continue_access:
        for (; k_access < end; k_access++){
            #ifdef DOUBLEP
            if (k!=endm1){
                dec_load64_asynci(fifo,((uint64_t)idx[k_access]) << 32 | ((uint64_t)idx[k_access+1]) ); 
                k++;
            } else {
            #endif
            #ifdef PRI
            printf("D\n");
            #endif
            if (fifo_full(access_fifo)) {
                // switch into execute
                LK;printf("access_id: %d -> exec_id: %d\n", access_id, exec_id);ULK;
                if (execute_ipr) {
                    goto continue_execute;
                } else {
                    goto execute;
                }
            }
            dec_load64_asynci_llc(access_fifo,idx[k_access]);
            //dec_produce64(fifo,x[idx[k]]);
            //dec_prefetchi(fifo, idx[k]);
            #ifdef DOUBLEP
            }
            #endif
        }
        access_ipr = 0;
    }

    if (execute_row >= R) {
        goto done;
    } else {
        // switch to execute
        if (execute_ipr) {
            goto continue_execute;
        } else {
            goto execute;
        }
    }

    //COMPUTE
    execute_row = 0;
execute:
    for(; execute_row < R; execute_row++) {
        execute_ipr = 1;

        LK;printf("exec_id: %d, exec_row: %d\n", exec_id, exec_row);ULK;

        #ifdef PRI
        printf("C\n");
        #endif
        uint64_t yi0 = 0;
        uint32_t start = ptr[execute_row];
        uint32_t end = ptr[execute_row+1];
        uint64_t dat;
        k_execute = start;

continue_execute:
        for (; k_execute < end; k_execute++){
            #ifdef PRI
            printf("S\n");
            #endif
            if (fifo_empty(exec_fifo)) {
                // switch into access
                LK;printf("exec_id: %d -> access_id: %d\n", exec_id, access_id);ULK;
                if (access_ipr) {
                    goto continue_access;
                } else {
                    goto access;
                }
            }
            dat = dec_consume64(exec_fifo);
            //dat = x[idx[k_execute]];
            yi0 += val[k_execute]*dat;
        }
        #ifdef RES
        if (yi0 != verify_data[execute_row]) {LK;printf("M%d-%d\n",execute_row,ptr[execute_row]); ULK; return;}
        #endif
        execute_ipr = 0;
    }

    if (access_row >= R) {
        goto done;
    } else {
        // switch to access
    }

done:
    __sync_synchronize;

    // DISCONNECT
    dec_close_consumer(access_id);
    dec_close_producer(exec_id);
}

int main(int argc, char ** argv) {

#ifdef BARE_METAL
    // synchronization variable
    volatile static uint32_t amo_cnt = 0;
    volatile static uint32_t amo_cnt2 = 0;
    uint32_t id, core_num;
    id = argv[0][0];
    core_num = argv[0][1];
    if (id == 0) init_tile(NUM);
    LK;printf("ID: %d of %d\n", id, core_num);ULK
    ATOMIC_OP(amo_cnt, 1, add, w);
    while(core_num != amo_cnt);
    _kernel_(id,core_num);
    // barrier to make sure all tiles closed their fifos
    ATOMIC_OP(amo_cnt2, 1, add, w);
    while(core_num != amo_cnt2);
    if (id == 0) print_stats_fifos(NUM);
    return result;
#else
    uint32_t core_num = NUM_A+NUM_E;
    assert(NUM_A == NUM_E);
    #include <omp.h>
    omp_set_num_threads(core_num);
    touch64(x,C);
    init_tile(NUM * 2);
    #pragma omp parallel
    {
        uint32_t ide = omp_get_thread_num();
        LK;printf("ID: %d\n", ide);ULK;
        #pragma omp barrier
        _kernel_(ide, core_num);
    }
    print_stats_fifos(NUM * 2);
#endif
return 0;
}


