#include <stdio.h>
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

#if NUM_A != NUM_E
    // NUM is the number of the opened FIFO Basically equals NUM_A * NUM_E
    #define NUM (NUM_A * NUM_E)
    #define MAP 1
#else
    // If we have same amount of A and E, FIFO count is NUM_A
    #define NUM NUM_A
#endif

#define FIFO_SIZE 32

#define ACCESS 0
#define EXECUTE 1

/***FIFO counters***/
volatile static uint32_t prog0_produce_cnt;
volatile static uint32_t prog0_consume_cnt;

volatile static uint32_t prog1_produce_cnt;
volatile static uint32_t prog1_consume_cnt;

/***prog0 access state variables***/
uint32_t i_prog0_a;
uint32_t k_prog0_a;
volatile static uint8_t prog0_access_done;

/***prog0 execute state variables***/
uint64_t yi0_prog0_e;
uint32_t i_prog0_e;
uint32_t k_prog0_e;
volatile static uint8_t prog0_execute_done;

/***prog1 access state variables***/
uint32_t i_prog1_a;
uint32_t k_prog1_a;
volatile static uint8_t prog1_access_done;

/***prog1 execute state variables***/
uint64_t yi0_prog1_e;
uint32_t i_prog1_e;
uint32_t k_prog1_e;
volatile static uint8_t prog1_execute_done;

/***PROG0 FUNCTIONS***/

void prog0_access_init(uint32_t id) {
    dec_open_producer(id);
    //Set the Base pointer, without it we would need to push &x[idx[k]]
    dec_set_base64(id,x);
    // LK;printf("Producer ID: %d\n", id);ULK;
    i_prog0_a = id;
    k_prog0_a = 0;
    prog0_produce_cnt = 0;
    prog0_access_done = 0;
}

void prog0_execute_init(uint32_t exec_id) {
    dec_open_consumer(exec_id);
    i_prog0_e = exec_id;
    k_prog0_e = 0;
    yi0_prog0_e = 0;
    prog0_consume_cnt = 0;
    prog0_execute_done = 0;
}

void prog0_access_kernel(uint32_t id, uint32_t threshold) {
    uint32_t i = i_prog0_a;
    uint32_t k = k_prog0_a;
    uint8_t stop_switching = prog0_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    // LK;printf("prog0_access, i: %d\n", i);ULK;
    for (; i < R; i += (2 * NUM)) {
        // LK;printf("Producer ID: %d, row: %d, threshold: %d\n", id, i, threshold);ULK;

        uint32_t end = ptr[i+1];

        if (k == 0) {
            k = ptr[i];
        }
        for (; k < end; k++){
            if (((produce_cnt + 1) > produce_threshold) && (!stop_switching)) {
                stop_switching = prog0_execute_done;
                if (!stop_switching) {
                    prog0_produce_cnt += produce_cnt;
                    produce_cnt = 0;
                    i_prog0_a = i;
                    k_prog0_a = k;
                    produce_threshold = FIFO_SIZE - (prog0_produce_cnt - prog0_consume_cnt);
                    if (produce_threshold < 1) {
                        return;
                    }
                }
            }
            dec_load64_asynci_llc(id,idx[k]);
            produce_cnt++;
        }
        k = 0;
    }

    prog0_access_done = 1;
    // LK;printf("prog0 access done\n");ULK;
    prog0_produce_cnt += produce_cnt;
}

void prog0_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    uint32_t i = i_prog0_e;
    uint8_t stop_switching = prog0_access_done;
    uint64_t yi0 = yi0_prog0_e;
    uint32_t k = k_prog0_e;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    // LK;printf("prog0_execute, i: %d\n", i);ULK;

    for (; i < R; i += (2 * NUM)) {
        // LK;printf("Consumer ID: %d, row: %d\n", exec_id, i);ULK;

        uint32_t start = ptr[i];
        uint32_t end = ptr[i+1];

        if (k == 0) {
            k = start;
        }
        for (; k < end; k++){
            if (((consume_cnt + 1) > consume_threshold) && (!stop_switching)) {
                stop_switching = prog0_access_done;
                if (!stop_switching) {
                    prog0_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    yi0_prog0_e = yi0;
                    i_prog0_e = i;
                    k_prog0_e = k;
                    consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
                    if (consume_threshold < 1) {
                        return;
                    }
                }
            }
            uint64_t dat = dec_consume64(exec_id);
            consume_cnt++;
            yi0 += val[k]*dat;
        }
        #ifdef RES
        if (yi0 != verify_data[i]) {
            LK;printf("M%d-%d\n",i,ptr[i]); ULK;
            return;
        }
        #endif
        k = 0;
        yi0 = 0;
    }

    prog0_execute_done = 1;
    // LK;printf("prog0 execute done\n");ULK;
    prog0_consume_cnt += consume_cnt;
}

void prog0_access_finish(uint32_t id) {
    dec_close_producer(id);
}

void prog0_execute_finish(uint32_t exec_id) {
    dec_close_consumer(exec_id);
}


/***PROG1 FUNCTIONS***/

void prog1_access_init(uint32_t id) {
    dec_open_producer(id);
    //Set the Base pointer, without it we would need to push &x[idx[k]]
    dec_set_base64(id,x);
    // LK;printf("Producer ID: %d\n", id);ULK;
    i_prog1_a = id;
    k_prog1_a = 0;
    prog1_produce_cnt = 0;
    prog1_access_done = 0;
}

void prog1_execute_init(uint32_t exec_id) {
    dec_open_consumer(exec_id);
    i_prog1_e = exec_id;
    k_prog1_e = 0;
    yi0_prog1_e = 0;
    prog1_consume_cnt = 0;
    prog1_execute_done = 0;
}

void prog1_access_kernel(uint32_t id, uint32_t threshold) {
    uint32_t i = i_prog1_a;
    uint32_t k = k_prog1_a;
    uint8_t stop_switching = prog1_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    // LK;printf("prog1_access, i: %d\n",i);ULK;
    for (; i < R; i += (2 * NUM)) {
        // LK;printf("Producer ID: %d, row: %d, threshold: %d\n", id, i, produce_threshold);ULK;

        uint32_t end = ptr[i+1];

        if (k == 0) {
            k = ptr[i];
        }
        for (; k < end; k++){
            if (((produce_cnt + 1) > produce_threshold && (!stop_switching))) {
                stop_switching = prog1_execute_done;
                if (!stop_switching) {
                    prog1_produce_cnt += produce_cnt;
                    produce_cnt = 0;
                    i_prog1_a = i;
                    k_prog1_a = k;
                    produce_threshold = FIFO_SIZE - (prog1_produce_cnt - prog1_consume_cnt);
                    if (produce_threshold < 1) {
                        return;
                    }
                    // return;
                }
            }
            dec_load64_asynci_llc(id,idx[k]);
            produce_cnt++;
        }
        k = 0;
    }

    prog1_access_done = 1;
    // LK;printf("prog1 access done\n");ULK;
    prog1_produce_cnt += produce_cnt;
}

void prog1_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    uint32_t i = i_prog1_e;
    uint8_t stop_switching = prog1_access_done;
    uint64_t yi0 = yi0_prog1_e;
    uint32_t k = k_prog1_e;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    // LK;printf("prog1_execute, i: %d\n",i);ULK;
    for (; i < R; i += (2 * NUM)) {
        // LK;printf("Consumer ID: %d, row: %d\n", exec_id, i);ULK;

        uint32_t start = ptr[i];
        uint32_t end = ptr[i+1];

        if (k == 0) {
            k = start;
        }
        for (; k < end; k++){
            if (((consume_cnt + 1) > consume_threshold) && (!stop_switching)) {
                stop_switching = prog1_access_done;
                if (!stop_switching) {
                    prog1_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    yi0_prog1_e = yi0;
                    i_prog1_e = i;
                    k_prog1_e = k;
                    consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
                    if (consume_threshold < 1) {
                        return;
                    }
                }
            }
            uint64_t dat = dec_consume64(exec_id);
            consume_cnt++;
            yi0 += val[k]*dat;
        }
        #ifdef RES
        if (yi0 != verify_data[i]) {
            LK;printf("M%d-%d\n",i,ptr[i]); ULK;
            return;
        }
        #endif
        k = 0;
        yi0 = 0;
    }

    prog1_execute_done = 1;
    // LK;printf("prog1 execute done\n");ULK;
    prog1_consume_cnt += consume_cnt;
}

void prog1_access_finish(uint32_t id) {
    dec_close_producer(id);
}

void prog1_execute_finish(uint32_t exec_id) {
    dec_close_consumer(exec_id);
}

void _kernel_(uint32_t id, uint32_t core_num){
    // Allocate producer/consumer roles based on id
    if (id < NUM_A) {
        /***PROG0 ACCESS and PROG1 EXECUTE*/
        // Init
        uint32_t exec_id = id + NUM;
        prog0_access_init(id);
        prog0_execute_init(exec_id);
        // LK;printf("core: %d running\n", id);ULK;

        // Run
        uint8_t role = ACCESS;
        while(!(prog0_access_done && prog1_execute_done)) {
            int consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
            int produce_threshold = FIFO_SIZE - (prog0_produce_cnt - prog0_consume_cnt);
            if (((role == EXECUTE) || prog0_access_done) && (!prog0_execute_done)) {
                // LK;printf("exec: %d\n", exec_id);ULK;
                prog0_execute_kernel(exec_id, consume_threshold);
                role = ACCESS;
            } else {
                // LK;printf("access: %d\n", id);ULK;
                prog0_access_kernel(id, produce_threshold);
                role = EXECUTE;
            }
        }

        // LK;printf("core: %d waiting\n", id);ULK;
        // Finish
        __sync_synchronize;
        prog0_access_finish(id);
        prog0_execute_finish(exec_id);
    } else {
        /***PROG1 ACCESS and PROG0 EXECUTE*/
        // Init
        uint32_t exec_id = id - NUM;
        prog1_access_init(id);
        prog1_execute_init(exec_id);
        // LK;printf("core: %d running\n", id);ULK;

        // Run
        uint8_t role = ACCESS;
        while(!(prog1_access_done && prog1_execute_done)) {
            int consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
            int produce_threshold = FIFO_SIZE - (prog1_produce_cnt - prog1_consume_cnt);
            if (((role == EXECUTE) || prog1_access_done) && (!prog1_execute_done)) {
                // LK;printf("exec: %d\n", exec_id);ULK;
                prog1_execute_kernel(exec_id, consume_threshold);
                role = ACCESS;
            } else {
                // LK;printf("access: %d\n", id);ULK;
                prog1_access_kernel(id, produce_threshold);
                role = EXECUTE;
            }
        }
        // LK;printf("core: %d waiting\n", id);ULK;
        // Finish
        __sync_synchronize;
        prog1_access_finish(id);
        prog1_execute_finish(exec_id);
    }
}

int main(int argc, char ** argv) {

#ifdef BARE_METAL
    // synchronization variable
    volatile static uint32_t amo_cnt = 0;
    volatile static uint32_t amo_cnt2 = 0;
    uint32_t id, core_num;
    id = argv[0][0];
    core_num = argv[0][1];
    if (id == 0) init_tile(NUM * 2);
    LK;printf("ID: %d of %d\n", id, core_num);ULK
    ATOMIC_OP(amo_cnt, 1, add, w);
    while(core_num != amo_cnt);
    _kernel_(id,core_num);
    // barrier to make sure all tiles closed their fifos
    ATOMIC_OP(amo_cnt2, 1, add, w);
    while(core_num != amo_cnt2);
    if (id == 0) print_stats_fifos(NUM * 2);
#else
    uint32_t core_num = NUM_A+NUM_E;
    #include <omp.h>
    omp_set_num_threads(core_num);
    touch64(x,C);
    init_tile(NUM * 2);
    #pragma omp parallel
    {
        uint32_t ide = omp_get_thread_num();
        // LK;printf("ID: %d\n", ide);ULK;
        #pragma omp barrier
        _kernel_(ide, core_num);
    }
    print_stats_fifos(NUM * 2);
#endif
return 0;
}