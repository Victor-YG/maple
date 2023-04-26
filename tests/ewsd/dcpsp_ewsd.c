#include <stdio.h>
#include "util.h"
#include "../maple/api/dcp_maple.h"
#if SIZE >= 3
    #include "../maple/tests/data/ewsd_data_big.h"
#elif SIZE == 2
    #include "../maple/tests/data/ewsd_data_small.h"
#else
    #include "../maple/tests/data/ewsd_data_tiny.h"
    #define RES 1
#endif

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

#define FIFO_SIZE 64

#define ACCESS 0
#define EXECUTE 1

//Type of parallelization
#define FINE 1



/***FIFO counters***/
volatile static uint32_t prog0_produce_cnt = 0;
volatile static uint32_t prog0_consume_cnt = 0;

volatile static uint32_t prog1_produce_cnt = 0;
volatile static uint32_t prog1_consume_cnt = 0;

/***prog0 access state variables***/
uint32_t i_prog0_a = 0;
uint32_t k_prog0_a = 0;
volatile static uint8_t prog0_access_done = 0;

/***prog0 execute state variables***/
uint32_t i_prog0_e = 0;
uint32_t k_prog0_e = 0;
volatile static uint8_t prog0_execute_done = 0;

/***prog1 access state variables***/
uint32_t i_prog1_a = 0;
uint32_t k_prog1_a = 0;
volatile static uint8_t prog1_access_done = 0;

/***prog1 execute state variables***/
uint32_t i_prog1_e = 0;
uint32_t k_prog1_e = 0;
volatile static uint8_t prog1_execute_done = 0;

/***PROG0 FUNCTIONS***/

void prog0_access_init(uint32_t id) {
    dec_open_producer(id);
    i_prog0_a = id;
    k_prog0_a = 0;
}

void prog0_execute_init(uint32_t exec_id) {
    dec_open_consumer(exec_id);
    i_prog0_e = exec_id;
    k_prog0_e = 0;
}

void prog0_access_kernel(uint32_t id, uint32_t threshold) {
    uint32_t i = i_prog0_a;
    uint32_t k = k_prog0_a;
    uint8_t stop_switching = prog0_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    // LK;printf("prog0_access, i: %d\n", i);ULK;
    uint32_t shape_0 = G_shape[0];
    uint32_t shape_1 = G_shape[1];

    for (; i < shape_0; i += (2 * NUM)) {
        uint32_t end = G_indptr[i+1];
        uint32_t start = G_indptr[i];

        if (k == 0) {
            k = start;
        }

        for (; k < end; k++) {
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
            uint32_t dense_idx = i * shape_1 + G_indices[k];
            dec_load32_async(id, &M[dense_idx]);
            produce_cnt++;
        }
        k = 0;
    }

    // LK;printf("prog0 access done\n");ULK;
    prog0_access_done = 1;
    prog0_produce_cnt += produce_cnt;
}

void prog0_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    uint32_t i = i_prog0_e;
    uint32_t k = k_prog0_e;
    uint8_t stop_switching = prog0_access_done;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    // LK;printf("prog0_execute, i: %d\n",i);ULK;
    uint32_t shape_0 = G_shape[0];

    for (; i < shape_0; i += (2 * NUM)) {
        uint32_t end = G_indptr[i+1];
        uint32_t start = G_indptr[i];

        if (k == 0) {
            k = start;
        }

        for (; k < end; k++) {
            if (((consume_cnt + 1) > consume_threshold) && (!stop_switching)) {
                stop_switching = prog0_access_done;
                if (!stop_switching) {
                    prog0_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    i_prog0_e = i;
                    k_prog0_e = k;
                    consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
                    if (consume_threshold < 1) {
                        return;
                    }
                }
            }
            uint32_t dat = G_data[k];
            uint32_t dense = dec_consume32(exec_id);
            consume_cnt++;
            result_data[k] = dat * dense;
            result_indices[k] = G_indices[k];

            #ifdef RES
            if (result_data2[k] != result_data[k]) {
                printf("M%d-%d\n", k, G_indptr[k]);
                printf("R%d-%d\n", result_data[k], result_data2[k]);
            }
            #endif
        }
        k = 0;
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
    i_prog1_a = id;
    k_prog1_a = 0;
}

void prog1_execute_init(uint32_t exec_id) {
    dec_open_consumer(exec_id);
    i_prog1_e = exec_id;
    k_prog1_e = 0;
}

void prog1_access_kernel(uint32_t id, uint32_t threshold) {
    uint32_t i = i_prog1_a;
    uint32_t k = k_prog1_a;
    uint8_t stop_switching = prog1_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    // LK;printf("prog1_access, i: %d\n", i);ULK;
    uint32_t shape_0 = G_shape[0];
    uint32_t shape_1 = G_shape[1];

    for (; i < shape_0; i += (2 * NUM)) {
        uint32_t end = G_indptr[i+1];
        uint32_t start = G_indptr[i];

        if (k == 0) {
            k = start;
        }

        for (; k < end; k++) {
            if (((produce_cnt + 1) > produce_threshold) && (!stop_switching)) {
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
                }
            }
            uint32_t dense_idx = i * shape_1 + G_indices[k];
            dec_load32_async(id, &M[dense_idx]);
            produce_cnt++;
        }
        k = 0;
    }

    // LK;printf("prog1 access done\n");ULK;
    prog1_access_done = 1;
    prog1_produce_cnt += produce_cnt;
}

void prog1_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    uint32_t i = i_prog1_e;
    uint32_t k = k_prog1_e;
    uint8_t stop_switching = prog1_access_done;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    // LK;printf("prog1_execute, i: %d\n",i);ULK;
    uint32_t shape_0 = G_shape[0];

    for (; i < shape_0; i += (2 * NUM)) {
        uint32_t end = G_indptr[i+1];
        uint32_t start = G_indptr[i];

        if (k == 0) {
            k = start;
        }

        for (; k < end; k++) {
            if (((consume_cnt + 1) > consume_threshold) && (!stop_switching)) {
                stop_switching = prog1_access_done;
                if (!stop_switching) {
                    prog1_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    i_prog1_e = i;
                    k_prog1_e = k;
                    consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
                    if (consume_threshold < 1) {
                        return;
                    }
                }
            }
            uint32_t dat = G_data[k];
            uint32_t dense = dec_consume32(exec_id);
            consume_cnt++;
            result_data[k] = dat * dense;
            result_indices[k] = G_indices[k];

            #ifdef RES
            if (result_data2[k] != result_data[k]) {
                printf("M%d-%d\n", k, G_indptr[k]);
                printf("R%d-%d\n", result_data[k], result_data2[k]);
            }
            #endif
        }
        k = 0;
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

void _kernel_(uint32_t id, uint32_t core_num) {
    if (id < NUM_A) {
        /***PROG0 ACCESS and PROG1 EXECUTE*/
        // Init
        uint32_t exec_id = id + NUM;
        prog0_access_init(id);
        prog0_execute_init(exec_id);
        // LK;printf("core: %d running\n", id);ULK;

        // Run
        uint8_t role = ACCESS;
        while(!(prog0_access_done && prog0_execute_done)) {
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
    }
    else {
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
    // LK;printf("ID: %d of %d\n", id, core_num);ULK
    ATOMIC_OP(amo_cnt, 1, add, w);
    while(core_num != amo_cnt);
    _kernel_(id,core_num);
    // barrier to make sure all tiles closed their fifos
    ATOMIC_OP(amo_cnt2, 1, add, w);
    while(core_num != amo_cnt2);
    if (id == 0) print_stats_fifos(NUM * 2);
    return result;
#else
    uint32_t core_num = NUM_A+NUM_E;
    #include <omp.h>
    omp_set_num_threads(core_num);
    touch(M,G_shape[0]*G_shape[1]);
    init_tile(NUM * 2);
    #pragma omp parallel
    {
        uint32_t ide = omp_get_thread_num();
        // printf("ID: %d\n", ide);
        #pragma omp barrier
        _kernel_(ide, core_num);
    }
    print_stats_fifos(NUM * 2);
#endif
return 0;
}