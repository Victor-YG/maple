#include <stdio.h>
#include "util.h"
#include "../maple/api/dcp_maple.h"
#if SIZE == 5
  #include "../maple/tests/data/bfs_data_youtube.h"
#elif SIZE == 4
  #include "../maple/tests/data/bfs_data_amazon.h"
#elif SIZE == 3
  #include "../maple/tests/data/bfs_data_big.h"
#elif SIZE == 2
#include "../maple/tests/data/bfs_data_small.h"
#else
    #include "../maple/tests/data/bfs_data_tiny.h"
#endif

#define FINE 1
#define FIFO_SIZE 32

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
uint64_t yi0_prog0_e = 0;
uint32_t i_prog0_e = 0;
uint32_t k_prog0_e = 0;
volatile static uint8_t prog0_execute_done = 0;

/***prog1 access state variables***/
uint32_t i_prog1_a = 0;
uint32_t k_prog1_a = 0;
volatile static uint8_t prog1_access_done = 0;

/***prog1 execute state variables***/
uint64_t yi0_prog1_e = 0;
uint32_t i_prog1_e = 0;
uint32_t k_prog1_e = 0;
volatile static uint8_t prog1_execute_done = 0;

/***PROG0 FUNCTIONS***/

void prog0_access_init(uint32_t id) {
    dec_open_producer(id);
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
    int i = i_prog0_a;
    int k = k_prog0_a;
    uint8_t stop_switching = prog0_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    uint32_t fif = id;
    // LK;printf("prog0_access, i: %d\n",i);ULK;

    while (i < R) {
        // LK;printf("Producer ID: %d, row: %d, threshold: %d\n", id, i, threshold);ULK;
        if (ret_prop[i] == -1) {
            i += (2 * NUM);
            k = 0;
            continue;
        }

        int start = node_array[i];
        int end = node_array[i + 1];
        int endm1 = end - 1;

        if (k == 0) {
            k = start;
        }

        for (; k < end; k++){
            if ((produce_cnt >= produce_threshold) && (!stop_switching)) {
                stop_switching = prog0_execute_done;
                if (!stop_switching) {
                    prog0_produce_cnt += produce_cnt;
                    produce_cnt = 0;
                    i_prog0_a = i;
                    k_prog0_a = k;
                    produce_threshold = FIFO_SIZE - (prog0_produce_cnt - prog0_consume_cnt);
                    if (produce_threshold <= (FIFO_SIZE/2)) {
                        return;
                    }
                }
            }
            uint32_t edge_index = edge_array[k];
            dec_load32_async(id, &ret_prop[edge_index]);
            produce_cnt++;
        }
        i += (2 * NUM);
        k = 0;
    }

    prog0_access_done = 1;
    // LK;printf("prog0 access done\n");ULK;
    i_prog0_a = i;
    k_prog0_a = 0;
    prog0_produce_cnt += produce_cnt;
}

void prog0_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    int i = i_prog0_e;
    uint8_t stop_switching = prog0_access_done;
    uint64_t yi0 = yi0_prog0_e;
    uint32_t k = k_prog0_e;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    uint32_t fifo = exec_id;
    uint32_t hop  = 1;
    uint32_t hopm = 2;

    // LK;printf("prog0_access, i: %d\n",i);ULK;

    while (i < R) {
        // LK;printf("Producer ID: %d, row: %d, threshold: %d\n", id, i, threshold);ULK;
        if (ret_prop[i] == -1) {
            i += (2 * NUM);
            k = 0;
            continue;
        }

        int start = node_array[i];
        int end = node_array[i + 1];
        int endm1 = end - 1;

        if (k == 0) {
            k = start;
        }
        uint64_t dat;
        for (; k < end; k++){
            if ((consume_cnt >= consume_threshold) && (!stop_switching)) {
                stop_switching = prog0_access_done;
                if (!stop_switching) {
                    prog0_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    yi0_prog0_e = yi0;
                    i_prog0_e = i;
                    k_prog0_e = k;
                    consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
                    if (consume_threshold <= (FIFO_SIZE/2)) {
                        return;
                    }
                    // return;
                }
            }
            dat = dec_consume32(exec_id);
            if (dat == hopm) ret_tmp[i] = hop;
            consume_cnt++;
        }
        i += (2 * NUM);
        k = 0;
    }

    prog0_execute_done = 1;
    // LK;printf("prog0 execute done\n");ULK;
    i_prog0_e = i;
    k_prog0_e = 0;
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
    int i = i_prog1_a;
    int k = k_prog1_a;
    uint8_t stop_switching = prog1_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    uint32_t fif = id;
    // LK;printf("prog0_access, i: %d\n",i);ULK;

    while (i < R) {
        // LK;printf("Producer ID: %d, row: %d, threshold: %d\n", id, i, threshold);ULK;
        if (ret_prop[i] == -1) {
            i += (2 * NUM);
            k = 0;
            continue;
        }

        int start = node_array[i];
        int end = node_array[i + 1];
        int endm1 = end - 1;

        if (k == 0) {
            k = start;
        }
        for (; k < end; k++){
            if ((produce_cnt >= produce_threshold && (!stop_switching))) {
                stop_switching = prog1_execute_done;
                if (!stop_switching) {
                    prog1_produce_cnt += produce_cnt;
                    produce_cnt = 0;
                    i_prog1_a = i;
                    k_prog1_a = k;
                    produce_threshold = FIFO_SIZE - (prog1_produce_cnt - prog1_consume_cnt);
                    if (produce_threshold <= (FIFO_SIZE/2)) {
                        return;
                    }
                }
            }
            uint32_t edge_index = edge_array[k];
            dec_load32_async(id, &ret_prop[edge_index]);
            produce_cnt++;
        }
        i += (2 * NUM);
        k = 0;
    }

    prog1_access_done = 1;
    // LK;printf("prog1 access done\n");ULK;
    i_prog1_a = i;
    k_prog1_a = 0;
    prog1_produce_cnt += produce_cnt;
}

void prog1_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    int i = i_prog1_e;
    uint8_t stop_switching = prog1_access_done;
    uint32_t k = k_prog1_e;
    uint64_t yi0 = yi0_prog1_e;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    uint32_t fifo = exec_id;
    uint32_t hop  = 1;
    uint32_t hopm = 2;

    // LK;printf("prog0_access, i: %d\n",i);ULK;

    while (i < R) {
        // LK;printf("Producer ID: %d, row: %d, threshold: %d\n", id, i, threshold);ULK;
        if (ret_prop[i] == -1) {
            i += (2 * NUM);
            k = 0;
            continue;
        }

        int start = node_array[i];
        int end = node_array[i + 1];
        int endm1 = end - 1;

        if (k == 0) {
            k = start;
        }
        uint64_t dat;
        for (; k < end; k++){
            if ((consume_cnt >= consume_threshold) && (!stop_switching)) {
                stop_switching = prog1_access_done;
                if (!stop_switching) {
                    prog1_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    yi0_prog1_e = yi0;
                    i_prog1_e = i;
                    k_prog1_e = k;
                    consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
                    if (consume_threshold <= (FIFO_SIZE/2)) {
                        return;
                    }
                }
            }
            dat = dec_consume32(exec_id);
            if (dat == hopm) ret_tmp[i] = hop;
            consume_cnt++;
        }
        i += (2 * NUM);
        k = 0;
        yi0 = 0;
    }

    prog1_execute_done = 1;
    // LK;printf("prog1 execute done\n");ULK;
    i_prog1_e = i;
    k_prog1_e = 0;
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
        uint32_t exec_id = id;
        prog0_access_init(id);
        prog0_execute_init(exec_id);
        // LK;printf("core: %d running\n", id);ULK;

        // Run
        while(!(prog0_access_done && prog0_execute_done)) {
            int consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
            int produce_threshold = FIFO_SIZE - (prog0_produce_cnt - prog0_consume_cnt);
            if ((((consume_threshold > ((FIFO_SIZE * 3)/4))) || prog0_access_done)) {
                // LK;printf("exec: %d\n", exec_id);ULK;
                prog0_execute_kernel(exec_id, consume_threshold);
            } else {
                // LK;printf("access: %d\n", id);ULK;
                prog0_access_kernel(id, produce_threshold);
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
        uint32_t exec_id = id;
        prog1_access_init(id);
        prog1_execute_init(exec_id);
        // LK;printf("core: %d running\n", id);ULK;

        // Run
        while(!(prog1_access_done && prog1_execute_done)) {
            int consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
            int produce_threshold = FIFO_SIZE - (prog1_produce_cnt - prog1_consume_cnt);
            if (((consume_threshold > ((FIFO_SIZE * 3)/4)) || prog1_access_done)) {
                // LK;printf("exec: %d\n", exec_id);ULK;
                prog1_execute_kernel(exec_id, consume_threshold);
            } else {
                // LK;printf("access: %d\n", id);ULK;
                prog1_access_kernel(id, produce_threshold);
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
    // synchronization variable
    volatile static uint32_t amo_cnt  = 0;
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
    return 0;
}