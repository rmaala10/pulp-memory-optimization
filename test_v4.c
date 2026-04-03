// Version 4: The Core Scaling Run (Amdahl's Law)
//Copy and paste this code into baseline.c

#include "pmsis.h"

#define MATRIX_SIZE 32
#define TILE_ROWS 8 
#define NUM_TILES (MATRIX_SIZE / TILE_ROWS)

#define ACTIVE_CORES 1 

// Data locations
PI_L2 int32_t l2_matrix_a[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_b[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_c[MATRIX_SIZE * MATRIX_SIZE];

// The Ping-Pong Buffers in L1
PI_L1 int32_t l1_buffer_0[TILE_ROWS * MATRIX_SIZE];
PI_L1 int32_t l1_buffer_1[TILE_ROWS * MATRIX_SIZE];

void cluster_matrix_mult(void *arg) {
    uint32_t core_id = pi_core_id();
    
    // We force the math loop to partition the 
    // workload based strictly on our manual ACTIVE_CORES count.
    uint32_t num_cores = ACTIVE_CORES; 
    pi_cl_dma_copy_t copy;

    pi_perf_conf((1 << PI_PERF_CYCLES) | (1 << PI_PERF_INSTR) | 
                 (1 << PI_PERF_LD_STALL) | (1 << PI_PERF_TCDM_CONT));
    pi_perf_reset();
    pi_perf_start();

    // PROLOGUE (Same DMA double-buffering logic as V3)
    if (core_id == 0) {
        copy.dir = PI_CL_DMA_DIR_EXT2LOC;
        copy.ext = (uint32_t)l2_matrix_a;
        copy.loc = (uint32_t)l1_buffer_0;
        copy.size = sizeof(l1_buffer_0);
        copy.id = 0;
        pi_cl_dma_memcpy(&copy);
        pi_cl_dma_wait(&copy); 
    }
    pi_cl_team_barrier(); 

    // DOUBLE BUFFERING LOOP
    for (int t = 0; t < NUM_TILES; t++) {
        
        // 1. Fetch NEXT tile via DMA (Core 0 only)
        if (core_id == 0 && t < NUM_TILES - 1) {
            copy.ext = (uint32_t)&l2_matrix_a[(t + 1) * TILE_ROWS * MATRIX_SIZE];
            copy.loc = (t % 2 == 0) ? (uint32_t)l1_buffer_1 : (uint32_t)l1_buffer_0;
            pi_cl_dma_memcpy(&copy); 
        }

        // 2. COMPUTE PHASE
        int32_t *current_A = (t % 2 == 0) ? l1_buffer_0 : l1_buffer_1;
        
        // SCALED PARTITIONING: Because 'num_cores' is controlled by our macro, 
        // the 'local_chunk' size changes dynamically. 
        // If ACTIVE_CORES = 1, that single core computes all 8 rows of the tile. 
        // If ACTIVE_CORES = 4, each core computes 2 rows, etc.
        uint32_t local_chunk = TILE_ROWS / num_cores;
        uint32_t start_row = core_id * local_chunk;
        uint32_t end_row = start_row + local_chunk;

        for (int i = start_row; i < end_row; i++) {
            for (int j = 0; j < MATRIX_SIZE; j++) {
                int32_t sum = 0;
                for (int k = 0; k < MATRIX_SIZE; k++) {
                    sum += current_A[i * MATRIX_SIZE + k] * matrix_b[k * MATRIX_SIZE + j];
                }
                int global_row = (t * TILE_ROWS) + i;
                matrix_c[global_row * MATRIX_SIZE + j] = sum;
            }
        }

        // 3. SYNCHRONIZE
        // Even if ACTIVE_CORES is 1, the barrier is necessary to ensure 
        // the single core doesn't rush ahead before the DMA finishes.
        pi_cl_team_barrier();
        if (core_id == 0 && t < NUM_TILES - 1) {
            pi_cl_dma_wait(&copy);
        }
        pi_cl_team_barrier();
    }

    pi_perf_stop();

    if (core_id == 0) {
        uint32_t cycles = pi_perf_read(PI_PERF_CYCLES);
        uint32_t instr = pi_perf_read(PI_PERF_INSTR);
        float ipc = (float)instr / (float)cycles;

        printf("\n--- V4: CORE SCALING (%d CORES) ---\n", ACTIVE_CORES);
        printf("Total Cycles:    %d\n", cycles);
        printf("Instructions:    %d\n", instr);
        printf("IPC (Efficiency): %.3f\n", ipc);
        printf("Load Stalls:     %d\n", pi_perf_read(PI_PERF_LD_STALL));
        printf("TCDM Contention: %d\n", pi_perf_read(PI_PERF_TCDM_CONT));
        printf("----------------------------------\n");
    }
}

void cluster_delegate(void *arg) {
    // SCALED DELEGATION: Instead of waking up all cores in the cluster, 
    // we use the API to explicitly wake up only the number we defined at the top.
    pi_cl_team_fork(ACTIVE_CORES, cluster_matrix_mult, NULL);
}

int main() {
    struct pi_device cluster_dev;
    struct pi_cluster_conf cl_conf;
    pi_cluster_conf_init(&cl_conf);
    pi_open_from_conf(&cluster_dev, &cl_conf);
    if (pi_cluster_open(&cluster_dev)) return -1;

    struct pi_cluster_task task;
    pi_cluster_task(&task, cluster_delegate, NULL);
    pi_cluster_send_task_to_cl(&cluster_dev, &task);
    pi_cluster_close(&cluster_dev);
    return 0;
}

//Results from the Core Scaling Run (Amdahl's Law) with 1 Core:
// --- V4: CORE SCALING (1 CORES) ---
// Total Cycles:    142875 (Takes ~7.6x longer than the 8-core V3 version)
// Instructions:    109203
// IPC (Efficiency): 0.764
// Load Stalls:     32774
// TCDM Contention: 0
// ----------------------------------