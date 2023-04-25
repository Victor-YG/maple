
//**************************************************************************
// Double-precision sparse matrix-vector multiplication benchmark
//--------------------------------------------------------------------------

#include <stdio.h>
#include "util.h"
#include "../maple/api/dcp_maple.h"

#if   SIZE == 5
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

// #define PRI 1
#define RES 1
#define FINE 1

#ifndef NUM_A
    #define NUM_A 1
#endif
#ifndef NUM_E
    #define NUM_E 1
#endif

// If we have same amount of A and E, FIFO count is NUM_A
#define NUM NUM_A

// global variables
static volatile uint32_t row_load = 0;
static volatile uint32_t row_exec = 0;
static volatile uint32_t idx_load = 0;
static volatile uint32_t idx_exec = 0;
static volatile uint32_t occupancy = 0;

void _kernel_(uint32_t id, uint32_t core_num) {
    // LK; printf("core id: %d\n", id); ULK;
    uint32_t fifo_load = 0;
    uint32_t fifo_exec = 0;

    // ACCESS
    row_load = id;
    row_exec = id;
    dec_open_producer(0);
    dec_set_base64(0, x); //Set the Base pointer, without it we would need to push &x[idx[k]]
    dec_open_consumer(0);

access:
    while (row_load < R) {
        // LK; printf("%d: loading row %d\n", id, row_load); ULK;

        uint32_t start = ptr[row_load];
        uint32_t end   = ptr[row_load + 1];

        idx_load = start;

access_continue:
        while(idx_load < end)
        {
            dec_load64_asynci_llc(fifo_load, idx[idx_load]);
            // LK; printf("core %d: loaded x(%d) into fifo %d\n", id, idx[idx_load], fpid[fifo_load]); ULK;
            idx_load += 1;
            occupancy += 1;

            // if (fifo_full(fifo_load)) {
            if (occupancy > 30) {
                // LK; printf("%d: fifo full %d A --> E\n", id, occupancy); ULK;
                goto execute_continue;
            }
        }
        row_load += 1;
    }
    goto execute_continue;

    //COMPUTE
execute:
    while (row_exec < R) {
        // LK; printf("%d: using row %d\n", id, row_exec); ULK;

        uint64_t yi0 = 0;
        uint32_t start = ptr[row_exec];
        uint32_t end   = ptr[row_exec + 1];
        uint64_t dat;

        idx_exec = start;

execute_continue:
        while (idx_exec < end) {
            dat = dec_consume64(fifo_exec); //dat = x[idx[k]];
            yi0 += val[idx_exec] * dat;
            // LK; printf("core %d: used x(%d) = %d from fifo %d\n", id, idx_exec, dat, fcid[fifo_exec]); ULK;
            idx_exec += 1;
            occupancy -= 1;

            // if (fifo_empty(fifo_exec)) {
            if (occupancy == 2) {
                // LK; printf("%d: fifo empty %d E --> A\n", id, occupancy); ULK;
                goto access_continue;
            }
        }
        row_exec += 1;
    }

exit:
    __sync_synchronize;
    dec_close_producer(0);
    dec_close_consumer(0);
    return;
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
    // LK; printf("ID: %d of %d\n", id, core_num); ULK
    ATOMIC_OP(amo_cnt, 1, add, w);
    while(core_num != amo_cnt);
    _kernel_(id, core_num);
    // barrier to make sure all tiles closed their fifos
    ATOMIC_OP(amo_cnt2, 1, add, w);
    while(core_num != amo_cnt2);
    if (id == 0) print_stats_fifos(NUM);
    return result;
#else
    uint32_t core_num = NUM_A + NUM_E;

    #include <omp.h>
    omp_set_num_threads(core_num);
    touch64(x, C);
    init_tile(NUM);
    #pragma omp parallel
    {
        uint32_t ide = omp_get_thread_num();
        // LK; printf("ID: %d\n", ide); ULK;
        #pragma omp barrier
        _kernel_(ide, core_num);
    }
    print_stats_fifos(NUM);
#endif

    return 0;
}