// Version 5: The Compute Optimization Run (Loop Unrolling)
//Copy and paste this code into baseline.c

#include "pmsis.h" // Ensure the PMSIS API is included

#define MATRIX_SIZE 32
#define TILE_ROWS 8 
#define NUM_TILES (MATRIX_SIZE / TILE_ROWS)

// Data locations
// Matrix A is in the slow L2, B and C are in the fast L1.
PI_L2 int32_t l2_matrix_a[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_b[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_c[MATRIX_SIZE * MATRIX_SIZE];

// The Ping-Pong Buffers in L1
// Used by the DMA to hide L2 latency, as established in Version 3.
PI_L1 int32_t l1_buffer_0[TILE_ROWS * MATRIX_SIZE];
PI_L1 int32_t l1_buffer_1[TILE_ROWS * MATRIX_SIZE];

void cluster_matrix_mult(void *arg) {
    uint32_t core_id = pi_core_id();
    uint32_t num_cores = pi_cl_team_nb_cores();
    pi_cl_dma_copy_t copy;

    // Configure and start the hardware performance counters
    pi_perf_conf((1 << PI_PERF_CYCLES) | (1 << PI_PERF_INSTR) | 
                 (1 << PI_PERF_LD_STALL) | (1 << PI_PERF_TCDM_CONT));
    pi_perf_reset();
    pi_perf_start();

    // Fetch the first tile via DMA (Core 0 only)
    if (core_id == 0) {
        copy.dir = PI_CL_DMA_DIR_EXT2LOC;
        copy.ext = (uint32_t)l2_matrix_a;
        copy.loc = (uint32_t)l1_buffer_0;
        copy.size = sizeof(l1_buffer_0);
        copy.id = 0;
        pi_cl_dma_memcpy(&copy);
        pi_cl_dma_wait(&copy); 
    }
    pi_cl_team_barrier(); // Synchronize all cores before starting the main loop

    // DOUBLE BUFFERING LOOP
    for (int t = 0; t < NUM_TILES; t++) {
        
        // 1. Fetch NEXT tile (Asynchronous data movement)
        if (core_id == 0 && t < NUM_TILES - 1) {
            copy.ext = (uint32_t)&l2_matrix_a[(t + 1) * TILE_ROWS * MATRIX_SIZE];
            copy.loc = (t % 2 == 0) ? (uint32_t)l1_buffer_1 : (uint32_t)l1_buffer_0;
            pi_cl_dma_memcpy(&copy); 
        }

        // 2. COMPUTE PHASE (OPTIMIZED)
        // Set the active buffer for computation
        int32_t *current_A = (t % 2 == 0) ? l1_buffer_0 : l1_buffer_1;
        
        // Partition the workload across the 8 active cores
        uint32_t local_chunk = TILE_ROWS / num_cores;
        uint32_t start_row = core_id * local_chunk;
        uint32_t end_row = start_row + local_chunk;

        for (int i = start_row; i < end_row; i++) {
            for (int j = 0; j < MATRIX_SIZE; j++) {
                int32_t sum = 0;
                
                // --- LOOP UNROLLING (Factor of 4) ---
                // In standard loops, the processor wastes cycles evaluating the condition 
                // (k < MATRIX_SIZE) and incrementing the counter (k++).
                // Here, we increment by 4 (k+=4) and execute four sequential MAC operations.
                for (int k = 0; k < MATRIX_SIZE; k+=4) {
                    
                    // By grouping these instructions, we force the compiler to keep 
                    // current_A and matrix_b values in the fast CPU registers. 
                    // This maximizes data reuse, drastically reducing Load Stalls 
                    // and increasing the overall Instructions Per Cycle (IPC).
                    sum += current_A[i * MATRIX_SIZE + k]     * matrix_b[k * MATRIX_SIZE + j];
                    sum += current_A[i * MATRIX_SIZE + (k+1)] * matrix_b[(k+1) * MATRIX_SIZE + j];
                    sum += current_A[i * MATRIX_SIZE + (k+2)] * matrix_b[(k+2) * MATRIX_SIZE + j];
                    sum += current_A[i * MATRIX_SIZE + (k+3)] * matrix_b[(k+3) * MATRIX_SIZE + j];
                }
                
                // Calculate the final row index and store the sum
                int global_row = (t * TILE_ROWS) + i;
                matrix_c[global_row * MATRIX_SIZE + j] = sum;
            }
        }

        // 3. SYNCHRONIZE
        // Wait for all cores to finish computing the current tile
        pi_cl_team_barrier();
        
        // Wait for the DMA to finish loading the next tile
        if (core_id == 0 && t < NUM_TILES - 1) {
            pi_cl_dma_wait(&copy);
        }
        
        // Final sync before looping back to process the newly loaded tile
        pi_cl_team_barrier();
    }

    pi_perf_stop();

    // 4. Print Results from Manager Core
    if (core_id == 0) {
        uint32_t cycles = pi_perf_read(PI_PERF_CYCLES);
        uint32_t instr = pi_perf_read(PI_PERF_INSTR);
        float ipc = (float)instr / (float)cycles;

        printf("\n--- V5: LOOP UNROLLING OPTIMIZATION ---\n");
        printf("Total Cycles:    %d\n", cycles);
        printf("Instructions:    %d\n", instr);
        printf("IPC (Efficiency): %.3f\n", ipc);
        printf("Load Stalls:     %d\n", pi_perf_read(PI_PERF_LD_STALL));
        printf("TCDM Contention: %d\n", pi_perf_read(PI_PERF_TCDM_CONT));
        printf("---------------------------------------\n");
    }
}

void cluster_delegate(void *arg) {
    pi_cl_team_fork(pi_cl_team_nb_cores(), cluster_matrix_mult, NULL);
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

//Results from the Compute Optimization Run (Loop Unrolling):
// --- V5: LOOP UNROLLING OPTIMIZATION ---
// Total Cycles:    22323
// Instructions:    19011
// IPC (Efficiency): 0.852 (Project high!)
// Load Stalls:     1293 (Project low!)
// TCDM Contention: 0
// ---------------------------------------