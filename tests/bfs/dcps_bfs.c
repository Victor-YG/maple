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
#define FIFO_ACC_THRES 30
#define FIFO_EXE_THRES 2

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

volatile static uint32_t queue_0_occ = 0;
volatile static uint32_t queue_1_occ = 0;
volatile static uint8_t prog0_execute_done = 0;
volatile static uint8_t prog1_execute_done = 0;

uint32_t prog0_acc_i = 0;
uint32_t prog0_acc_e = 0;
uint32_t prog0_exe_i = 0;
uint32_t prog0_exe_e = 0;

uint32_t prog1_acc_i = 0;
uint32_t prog1_acc_e = 0;
uint32_t prog1_exe_i = 0;
uint32_t prog1_exe_e = 0;

void prog0_access_kernel() {
    uint32_t id = 0;

    while(prog0_acc_i < R) {
        // LK;printf("Core %d accessing row: %d\n", id, prog0_acc_i);ULK;

        if (ret_prop[prog0_acc_i] == -1) {
            uint32_t start = node_array[prog0_acc_i];
            uint32_t end = node_array[prog0_acc_i + 1];
            while(prog0_acc_e < end) {
                uint32_t edge_index = edge_array[prog0_acc_e];
                dec_load32_async(id, &ret_prop[edge_index]);
                queue_0_occ++;
                prog0_acc_e++;

                if (queue_0_occ > FIFO_ACC_THRES) {
                    if (prog0_acc_e == end) prog0_acc_i += 2;
                    // LK;printf("Core %d A --> E\n", id);ULK;
                    return;
                }
            }
        }
        prog0_acc_i += 2;
    }
}

void prog1_access_kernel() {
    uint32_t id = 1;

    while(prog1_acc_i < R) {
        // LK;printf("Core %d accessing row: %d\n", id, prog1_acc_i);ULK;

        if (ret_prop[prog1_acc_i] == -1) {
            uint32_t start = node_array[prog1_acc_i];
            uint32_t end = node_array[prog1_acc_i + 1];
            while(prog1_acc_e < end) {
                uint32_t edge_index = edge_array[prog1_acc_e];
                dec_load32_async(id, &ret_prop[edge_index]);
                queue_1_occ++;
                prog1_acc_e++;

                if (queue_1_occ > FIFO_ACC_THRES) {
                    if (prog1_acc_e == end) prog1_acc_i += 2;
                    // LK;printf("Core %d A --> E\n", id);ULK;
                    return;
                }
            }
        }
        prog1_acc_i += 2;
    }
}

void prog0_execute_kernel() {
    uint32_t id = 0;
    uint32_t hop  = 1;
    uint32_t hopm = 2;
    uint32_t dat = 0;

    while(prog0_exe_i < R) {
        // LK;printf("Core %d executing row: %d\n", id, prog0_exe_i);ULK;

        if (ret_prop[prog0_exe_i] == -1) {
            uint32_t start = node_array[prog0_exe_i];
            uint32_t end = node_array[prog0_exe_i + 1];
            while(prog0_exe_e < end) {
                dat = dec_consume32(id);
                if (dat == hopm) ret_tmp[prog0_exe_i] = hop;
                queue_0_occ--;
                prog0_exe_e++;

                if (queue_0_occ < FIFO_EXE_THRES) {
                    if (prog0_exe_e == end) prog0_exe_i += 2;
                    // LK;printf("Core %d E --> A\n", id);ULK;
                    return;
                }
            }
        }
        prog0_exe_i += 2;
    }

    prog0_execute_done = 1;
    return;
}

void prog1_execute_kernel() {
    uint32_t id = 1;
    uint32_t hop  = 1;
    uint32_t hopm = 2;
    uint32_t dat = 0;

    while(prog1_exe_i < R) {
        // LK;printf("Core %d executing row: %d\n", id, prog1_exe_i);ULK;

        if (ret_prop[prog1_exe_i] == -1) {
            uint32_t start = node_array[prog1_exe_i];
            uint32_t end = node_array[prog1_exe_i + 1];
            while(prog1_exe_e < end) {
                dat = dec_consume32(id);
                if (dat == hopm) ret_tmp[prog1_exe_i] = hop;
                queue_1_occ--;
                prog1_exe_e++;

                if (queue_1_occ < FIFO_EXE_THRES) {
                    if (prog1_exe_e == end) prog1_exe_i += 2;
                    // LK;printf("Core %d E --> A\n", id);ULK;
                    return;
                }
            }
        }
        prog1_exe_i += 2;
    }

    prog1_execute_done = 1;
    return;
}

void prog0_kernel(uint32_t id) {
    dec_open_producer(id);
    dec_open_consumer(id);

    prog0_acc_i = id;
    prog0_exe_i = id;

    while(!prog0_execute_done) {
        prog0_access_kernel();
        prog0_execute_kernel();
    }

    dec_close_consumer(id);
    dec_close_producer(id);
}

void prog1_kernel(uint32_t id) {
    dec_open_producer(id);
    dec_open_consumer(id);

    prog1_acc_i = id;
    prog1_exe_i = id;

    while(!prog1_execute_done) {
        prog1_access_kernel();
        prog1_execute_kernel();
    }

    dec_close_consumer(id);
    dec_close_producer(id);
}

void _kernel_(uint32_t id, uint32_t core_num) {
    // LK;printf("Core %d\n", id);ULK;

    if (id < NUM_A) {
        prog0_kernel(id);
    }
    else {
        prog1_kernel(id);
    }
}

int main(int argc, char ** argv) {
    // synchronization variable
    volatile static uint32_t amo_cnt  = 0;
    volatile static uint32_t amo_cnt2 = 0;
    uint32_t id, core_num;
    id = argv[0][0];
    core_num = argv[0][1];
    // LK;printf("ID: %d, of %d\n", id, core_num);ULK;

    if (id == 0) init_tile(NUM);
    ATOMIC_OP(amo_cnt, 1, add, w);
    while(core_num != amo_cnt);

    _kernel_(id, core_num);
    // barrier to make sure all tiles closed their fifos
    ATOMIC_OP(amo_cnt2, 1, add, w);
    while(core_num != amo_cnt2);
    if (id == 0) print_stats_fifos(NUM * 2);
    return 0;
}