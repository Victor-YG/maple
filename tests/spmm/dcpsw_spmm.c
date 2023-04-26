#include <stdio.h>
#include "util.h"
#include "../maple/api/dcp_maple.h"
#if SIZE == 5
#include "../maple/tests/data/spmm_data_sq495.h"
#elif SIZE == 4
#include "../maple/tests/data/spmm_data_sq818.h"
#elif SIZE == 3
    #include "../maple/tests/data/spmm_data_big.h"
#elif SIZE == 2 
    #include "../maple/tests/data/spmm_data_small.h"
#else
    #include "../maple/tests/data/spmm_data_tiny.h"
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

/***FIFO counters***/
volatile static uint32_t prog0_produce_cnt;
volatile static uint32_t prog0_consume_cnt;

volatile static uint32_t prog1_produce_cnt;
volatile static uint32_t prog1_consume_cnt;

/***global variables***/
uint32_t A_nrows;
uint32_t B_ncols;

/***prog0 access state variables***/
uint32_t j_prog0_a;
uint32_t k_prog0_a;
int m_prog0_a;
uint32_t A_start_prog0_a;
uint32_t A_end_prog0_a;
volatile static uint8_t prog0_access_done;

/***prog0 execute state variables***/
uint32_t j_prog0_e;
uint32_t k_prog0_e;
int m_prog0_e;
uint32_t A_start_prog0_e;
uint32_t A_end_prog0_e;
volatile static uint8_t prog0_execute_done;

/***prog1 access state variables***/
uint32_t j_prog1_a;
uint32_t k_prog1_a;
int m_prog1_a;
uint32_t A_start_prog1_a;
uint32_t A_end_prog1_a;
volatile static uint8_t prog1_access_done;

/***prog1 execute state variables***/
uint32_t j_prog1_e;
uint32_t k_prog1_e;
int m_prog1_e;
uint32_t A_start_prog1_e;
uint32_t A_end_prog1_e;
volatile static uint8_t prog1_execute_done;

/***PROG0 FUNCTIONS***/

void prog0_access_init(uint32_t id) {
    dec_open_producer(id);
    // LK;printf("Producer ID: %d\n", id);ULK;
    j_prog0_a = id;
    k_prog0_a = 0;
    m_prog0_a = -1;
    A_start_prog0_a = 0;
    A_end_prog0_a = 0;
    C_indptr[0] = 0;
    prog0_produce_cnt = 0;
    prog0_access_done = 0;
}

void prog0_execute_init(uint32_t exec_id) {
    dec_open_consumer(exec_id);
    j_prog0_e = exec_id;
    k_prog0_e = 0;
    m_prog0_e = -1;
    A_start_prog0_e = 0;
    A_end_prog0_e = 0;
    prog0_consume_cnt = 0;
    prog0_execute_done = 0;
}

void prog0_access_kernel(uint32_t id, uint32_t threshold) {
    uint32_t j = j_prog0_a;
    uint32_t k = k_prog0_a;
    int m = m_prog0_a;
    uint32_t A_start = A_start_prog0_a;
    uint32_t A_end = A_end_prog0_a;
    uint8_t stop_switching = prog1_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    // LK;printf("prog0_access, j: %d\n",j);ULK;
    for (; j < B_ncols; j+=(2 * NUM)) {
        // LK;printf("Producer ID: %d, j: %d, k: %d, m: %d, threshold: %d\n", id, j, k, m, threshold);ULK;
        #ifdef PRI
        printf("P\n");
        #endif

        uint32_t start = B_indptr[j];
        uint32_t end = B_indptr[j+1];
        
        if (k == 0) {
            k = start;
        }
        for (; k < end; k++){
            if (((produce_cnt + 2) > produce_threshold) && (!stop_switching)) {
                stop_switching = prog1_execute_done;
                if (!stop_switching) {
                    prog0_produce_cnt += produce_cnt;
                    produce_cnt = 0;
                    j_prog0_a = j;
                    k_prog0_a = k;
                    m_prog0_a = m;
                    A_start_prog0_a = A_start;
                    A_end_prog0_a = A_end;
                    produce_threshold = FIFO_SIZE - (prog0_produce_cnt - prog0_consume_cnt);
                    if (produce_threshold < 2) {
                        return;
                    }
                }
            }
            uint32_t l = B_indices[k];

            if (m == -1) {
                A_start = A_indptr[l];
                A_end = A_indptr[l+1];
                dec_produce32(id,A_start);
                dec_produce32(id,A_end);
                produce_cnt += 2;
                m = A_start;
            }
            for(; m < A_end; m++) {
                if (((produce_cnt + 2) > produce_threshold) && (!stop_switching)) {
                    stop_switching = prog1_execute_done;
                    if (!stop_switching) {
                        prog0_produce_cnt += produce_cnt;
                        produce_cnt = 0;
                        j_prog0_a = j;
                        k_prog0_a = k;
                        m_prog0_a = m;
                        A_start_prog0_a = A_start;
                        A_end_prog0_a = A_end;
                        produce_threshold = FIFO_SIZE - (prog0_produce_cnt - prog0_consume_cnt);
                        if (produce_threshold < 2) {
                            return;
                        }
                    }
                }
                uint32_t idx = A_indices[m];
                uint32_t A = A_data[m];
                dec_produce32(id,idx);
                dec_produce32(id,A);
                produce_cnt += 2;
                // LK;printf("prog0 produce, j: %d, k: %d, m: %d, idx: %d, A: %d\n", j, k, m, idx, A);ULK;
            }
            m = -1;
        }
        k = 0;
    }

    prog0_access_done = 1;
    LK;printf("prog0 access done\n");ULK;
    prog0_produce_cnt += produce_cnt;
}

void prog0_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    uint32_t j = j_prog0_e;
    uint32_t k = k_prog0_e;
    int m = m_prog0_e;
    uint32_t A_start = A_start_prog0_e;
    uint32_t A_end = A_end_prog0_e;
    uint8_t stop_switching = prog1_access_done;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    // LK;printf("prog0_execute, i: %d\n",i);ULK;
    for (; j < B_ncols; j+=(2 * NUM)) {
        // LK;printf("Consumer ID: %d, row: %d\n", exec_id, i);ULK;
        #ifdef PRI
        printf("C\n");
        #endif
        
        uint32_t start = B_indptr[j];
        uint32_t end = B_indptr[j+1];

        if (k == 0) {
            k = start;
        }
        for (; k < end; k++){
            if (((consume_cnt + 2) > consume_threshold) && (!stop_switching)) {
                stop_switching = prog1_access_done;
                if (!stop_switching) {
                    prog0_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    j_prog0_e = j;
                    k_prog0_e = k;
                    m_prog0_e = m;
                    A_start_prog0_e = A_start;
                    A_end_prog0_e = A_end;
                    consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
                    if (consume_threshold < 2) {
                        return;
                    }
                }
            }
            #ifdef PRI
            printf("K\n");
            #endif

            uint32_t B = B_data[k];

            if (m == -1) {
                A_start = dec_consume32(exec_id);
                A_end = dec_consume32(exec_id);
                consume_cnt += 2;
                m = A_start;
            }
            for(; m < A_end; m++) {
                if (((consume_cnt + 2) > consume_threshold) && (!stop_switching)) {
                    stop_switching = prog1_access_done;
                    if (!stop_switching) {
                        prog0_consume_cnt += consume_cnt;
                        consume_cnt = 0;
                        j_prog0_e = j;
                        k_prog0_e = k;
                        m_prog0_e = m;
                        A_start_prog0_e = A_start;
                        A_end_prog0_e = A_end;    
                        consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
                        if (consume_threshold < 2) {
                            return;
                        }
                    }
                }
                #ifdef PRI
                printf("M\n");
                #endif
                uint32_t idx = dec_consume32(exec_id);
                uint32_t A = dec_consume32(exec_id);
                consume_cnt += 2;
                // LK;printf("prog0 consume, j: %d, k: %d, m: %d,idx: %d, A: %d\n", j, k, m, idx, A);ULK;
                spa[j*A_nrows+idx] += A * B;
            }
            m = -1;
        }
        k = 0;
        uint32_t tmp_C_indptr = C_indptr[j];
        uint32_t tmp_bias = bias[j];
        for(uint32_t i = 0; i < A_nrows; i++) {
            uint32_t tmp_spa = spa[j*A_nrows + i];
            if(tmp_spa) {
                tmp_spa += tmp_bias;
                if(tmp_spa) {
                    tmp_C_indptr++;
                    tmp_C_indices[j*A_nrows + i] = i;
                    spa[j*A_nrows + i] = tmp_spa;
                }
            }
        }
        C_indptr[j+1] = tmp_C_indptr;
    }
    prog0_execute_done = 1;
    // LK;printf("prog0 execute done\n");ULK;
    prog0_consume_cnt += consume_cnt;
}

void prog0_access_finish(uint32_t id) {
    dec_close_producer(id);
    // LK;printf("prog0_produce_cnt: %d\n", prog0_produce_cnt);ULK;
}

void prog0_execute_finish(uint32_t exec_id) {
    dec_close_consumer(exec_id);
    // LK;printf("prog0_consume_cnt: %d\n", prog0_consume_cnt);ULK;
}


/***PROG1 FUNCTIONS***/

void prog1_access_init(uint32_t id) {
    dec_open_producer(id);
    // LK;printf("Producer ID: %d\n", id);ULK;
    j_prog1_a = id;
    k_prog1_a = 0;
    m_prog1_a = -1;
    A_start_prog1_a = 0;
    A_end_prog1_a = 0;
    C_indptr[0] = 0;
    prog1_produce_cnt = 0;
    prog1_access_done = 0;
}

void prog1_execute_init(uint32_t exec_id) {
    dec_open_consumer(exec_id);
    j_prog1_e = exec_id;
    k_prog1_e = 0;
    m_prog1_e = -1;
    A_start_prog1_e = 0;
    A_end_prog1_e = 0;
    prog1_consume_cnt = 0;
    prog1_execute_done = 0;
}

void prog1_access_kernel(uint32_t id, uint32_t threshold) {
    uint32_t j = j_prog1_a;
    uint32_t k = k_prog1_a;
    int m = m_prog1_a;
    uint32_t A_start = A_start_prog1_a;
    uint32_t A_end = A_end_prog1_a;
    uint8_t stop_switching = prog0_execute_done;
    uint32_t produce_cnt = 0;
    uint32_t produce_threshold = threshold;
    // LK;printf("prog0_access, j: %d\n",j);ULK;
    for (; j < B_ncols; j+=(2 * NUM)) {
        // LK;printf("Producer ID: %d, row: %d, threshold: %d\n", id, j, threshold);ULK;
        #ifdef PRI
        printf("P\n");
        #endif

        uint32_t start = B_indptr[j];
        uint32_t end = B_indptr[j+1];
        
        if (k == 0) {
            k = start;
        }
        for (; k < end; k++){
            if (((produce_cnt + 2) > produce_threshold) && (!stop_switching)) {
                stop_switching = prog0_execute_done;
                if (!stop_switching) {
                    prog1_produce_cnt += produce_cnt;
                    produce_cnt = 0;
                    j_prog1_a = j;
                    k_prog1_a = k;
                    m_prog1_a = m;
                    A_start_prog1_a = A_start;
                    A_end_prog1_a = A_end;
                    produce_threshold = FIFO_SIZE - (prog1_produce_cnt - prog1_consume_cnt);
                    if (produce_threshold < 2) {
                        return;
                    }
                }
            }
            uint32_t l = B_indices[k];

            if (m == -1) {
                A_start = A_indptr[l];
                A_end = A_indptr[l+1];
                dec_produce32(id,A_start);
                dec_produce32(id,A_end);
                produce_cnt += 2;
                m = A_start;
            }
            for(; m < A_end; m++) {
                if (((produce_cnt + 2) > produce_threshold) && (!stop_switching)) {
                    stop_switching = prog0_execute_done;
                    if (!stop_switching) {
                        prog1_produce_cnt += produce_cnt;
                        produce_cnt = 0;
                        j_prog1_a = j;
                        k_prog1_a = k;
                        m_prog1_a = m;
                        A_start_prog1_a = A_start;
                        A_end_prog1_a = A_end;
                        produce_threshold = FIFO_SIZE - (prog1_produce_cnt - prog1_consume_cnt);
                        if (produce_threshold < 2) {
                            return;
                        }
                    }
                }
                uint32_t idx = A_indices[m];
                uint32_t A = A_data[m];
                dec_produce32(id,idx);
                dec_produce32(id,A);
                produce_cnt += 2;
                // LK;printf("prog1 produce, j: %d, k: %d, m: %d, idx: %d, A: %d\n", j, k, m, idx, A);ULK;
            }
            m = -1;
        }
        k = 0;
    }

    prog1_access_done = 1;
    // LK;printf("prog1 access done\n");ULK;
    prog1_produce_cnt += produce_cnt;
}

void prog1_execute_kernel(uint32_t exec_id, uint32_t threshold) {
    uint32_t j = j_prog1_e;
    uint32_t k = k_prog1_e;
    int m = m_prog1_e;
    uint32_t A_start = A_start_prog1_e;
    uint32_t A_end = A_end_prog1_e;
    uint8_t stop_switching = prog0_access_done;
    uint32_t consume_cnt = 0;
    uint32_t consume_threshold = threshold;
    // LK;printf("prog1_execute, i: %d\n",i);ULK;
    for (; j < B_ncols; j+=(2 * NUM)) {
        // LK;printf("Consumer ID: %d, j: %d, k: %d, m: %d, threshold: %d\n", exec_id, j, k, m, threshold);ULK;
        #ifdef PRI
        printf("C\n");
        #endif
        
        uint32_t start = B_indptr[j];
        uint32_t end = B_indptr[j+1];
        
        if (k == 0) {
            k = start;
        }
        for (; k < end; k++){
            if (((consume_cnt + 2) > consume_threshold) && (!stop_switching)) {
                stop_switching = prog0_access_done;
                if (!stop_switching) {
                    prog1_consume_cnt += consume_cnt;
                    consume_cnt = 0;
                    j_prog1_e = j;
                    k_prog1_e = k;
                    m_prog1_e = m;
                    A_start_prog1_e = A_start;
                    A_end_prog1_e = A_end;
                    consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
                    if (consume_threshold < 2) {
                        return;
                    }
                }
            }
            #ifdef PRI
            printf("K\n");
            #endif

            uint32_t B = B_data[k];

            if (m == -1) {
                A_start = dec_consume32(exec_id);
                A_end = dec_consume32(exec_id);
                consume_cnt += 2;
                m = A_start;
            }
            for(; m < A_end; m++) {
                if (((consume_cnt + 2) > consume_threshold) && (!stop_switching)) {
                    stop_switching = prog0_access_done;
                    if (!stop_switching) {
                        prog1_consume_cnt += consume_cnt;
                        consume_cnt = 0;
                        j_prog1_e = j;
                        k_prog1_e = k;
                        m_prog1_e = m;
                        A_start_prog1_e = A_start;
                        A_end_prog1_e = A_end;  
                        consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
                        if (consume_threshold < 2) {
                            return;
                        }
                    }
                }
                #ifdef PRI
                printf("M\n");
                #endif
                uint32_t idx = dec_consume32(exec_id);
                uint32_t A = dec_consume32(exec_id);
                consume_cnt += 2;
                // LK;printf("prog1 consume, j: %d, k: %d, m: %d,idx: %d, A: %d\n", j, k, m, idx, A);ULK;
                spa[j*A_nrows+idx] += A * B;
            }
            m = -1;
        }
        k = 0;
        uint32_t tmp_C_indptr = C_indptr[j];
        uint32_t tmp_bias = bias[j];
        for(uint32_t i = 0; i < A_nrows; i++) {
            uint32_t tmp_spa = spa[j*A_nrows + i];
            if(tmp_spa) {
                tmp_spa += tmp_bias;
                if(tmp_spa) {
                    tmp_C_indptr++;
                    tmp_C_indices[j*A_nrows + i] = i;
                    spa[j*A_nrows + i] = tmp_spa;
                }
            }
        }
        C_indptr[j+1] = tmp_C_indptr;
    }
    prog1_execute_done = 1;
    // LK;printf("prog1 execute done\n");ULK;
    prog1_consume_cnt += consume_cnt;
}

void prog1_access_finish(uint32_t id) {
    dec_close_producer(id);
    // LK;printf("prog1_produce_cnt: %d\n", prog1_produce_cnt);ULK;
}

void prog1_execute_finish(uint32_t exec_id) {
    dec_close_consumer(exec_id);
    // LK;printf("prog1_consume_cnt: %d\n", prog1_consume_cnt);ULK;
}

void _kernel_(uint32_t id, uint32_t core_num){
    A_nrows = A_shape[0];
    B_ncols = B_shape[1];
    // Allocate producer/consumer roles based on id
    if (id < NUM_A) {
        /***PROG0 ACCESS and PROG1 EXECUTE*/
        // Init
        uint32_t exec_id = id + NUM;
        prog0_access_init(id);
        prog1_execute_init(exec_id);
        // LK;printf("core: %d running\n", id);ULK;
        
        // Run
        uint8_t role = ACCESS;
        while(!(prog0_access_done && prog1_execute_done)) {
            // LK;printf("core 0 switch\n");ULK;
            int consume_threshold = prog1_produce_cnt - prog1_consume_cnt;
            int produce_threshold = FIFO_SIZE - (prog0_produce_cnt - prog0_consume_cnt);
            // LK;printf("prog0_produce_cnt: %d\n", prog0_produce_cnt);ULK;
            // LK;printf("prog0_consume_cnt: %d\n", prog0_consume_cnt);ULK;
            // LK;printf("prog1_produce_cnt: %d\n", prog1_produce_cnt);ULK;
            // LK;printf("prog1_consume_cnt: %d\n", prog1_consume_cnt);ULK;
            if (((role == EXECUTE) || prog0_access_done) && (!prog1_execute_done)) {
                // LK;printf("exec: %d\n", exec_id);ULK;
                prog1_execute_kernel(exec_id, consume_threshold);
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
        prog1_execute_finish(exec_id);
    } else {
        /***PROG1 ACCESS and PROG0 EXECUTE*/
        // Init
        uint32_t exec_id = id - NUM;
        prog1_access_init(id);
        prog0_execute_init(exec_id);
        // LK;printf("core: %d running\n", id);ULK;
        
        // Run
        uint8_t role = ACCESS;
        while(!(prog1_access_done && prog0_execute_done)) {
            // LK;printf("core 1 switch\n");ULK;
            int consume_threshold = prog0_produce_cnt - prog0_consume_cnt;
            int produce_threshold = FIFO_SIZE - (prog1_produce_cnt - prog1_consume_cnt);
            if (((role == EXECUTE) || prog1_access_done) && (!prog0_execute_done)) {
                // LK;printf("exec: %d\n", exec_id);ULK;
                prog0_execute_kernel(exec_id, consume_threshold);
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
        prog0_execute_finish(exec_id);
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
    touch(A_data,NNZ);
    touch(A_indices,NNZ);
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