// Version 3: The Optimized Contribution (Double-Buffered)
//Copy and paste this code into baseline.c

#include "pmsis.h" // Standard PULP Microcontroller Software Interface Standard (PMSIS) API

#define MATRIX_SIZE 32
// We don't bring the whole matrix in at once. We bring it in "tiles" (chunks).
#define TILE_ROWS 8 
#define NUM_TILES (MATRIX_SIZE / TILE_ROWS) // 32 / 8 = 4 total tiles

// --- MAIN MEMORY ALLOCATION ---
// Matrix A is the massive dataset stuck out in slow L2 memory.
PI_L2 int32_t l2_matrix_a[MATRIX_SIZE * MATRIX_SIZE];
// For simplicity in this test, we assume B and C fit in fast L1.
PI_L1 int32_t matrix_b[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_c[MATRIX_SIZE * MATRIX_SIZE];

// --- THE "PING-PONG" BUFFERS ---
// These are two small, fast buffers in L1 memory. 
// While the processor calculates using Buffer 0, the DMA loads the next tile into Buffer 1.
PI_L1 int32_t l1_buffer_0[TILE_ROWS * MATRIX_SIZE];
PI_L1 int32_t l1_buffer_1[TILE_ROWS * MATRIX_SIZE];

void cluster_matrix_mult(void *arg) {
    uint32_t core_id = pi_core_id();
    uint32_t num_cores = pi_cl_team_nb_cores();
    
    // Struct to hold the parameters for the Direct Memory Access (DMA) engine
    pi_cl_dma_copy_t copy;

    // 1. Configure the 4 Performance Counters
    pi_perf_conf(
        (1 << PI_PERF_CYCLES) | 
        (1 << PI_PERF_INSTR) | 
        (1 << PI_PERF_LD_STALL) |
        (1 << PI_PERF_TCDM_CONT)
    );
    pi_perf_reset();
    pi_perf_start();

    // 2. PROLOGUE: Pre-fetch the very first tile
    // Before we can enter the math loop, we must load the first piece of data.
    // We restrict DMA commands to core 0 (the master core) to prevent collisions.
    if (core_id == 0) {
        copy.dir = PI_CL_DMA_DIR_EXT2LOC;    // Direction: External L2 -> Local L1
        copy.ext = (uint32_t)l2_matrix_a;    // Source: Start of Matrix A in L2
        copy.loc = (uint32_t)l1_buffer_0;    // Destination: Ping-pong Buffer 0 in L1
        copy.size = sizeof(l1_buffer_0);     // How much data to grab
        copy.id = 0;                         // Optional ID tag
        
        pi_cl_dma_memcpy(&copy);             // Tell the DMA hardware to start moving data
        pi_cl_dma_wait(&copy);               // WAIT here until the first tile is fully loaded
    }
    // Barrier: All 8 cores wait here until core 0 finishes the initial DMA load
    pi_cl_team_barrier(); 

    // 3. THE DOUBLE-BUFFERING LOOP
    // We loop through the matrix 4 times (since we have 4 tiles)
    for (int t = 0; t < NUM_TILES; t++) {
        
        // --- DATA MOVEMENT PHASE (The "Hide Latency" Trick) ---
        // While processing tile 't', ask the DMA to fetch tile 't+1' in the background.
        if (core_id == 0 && t < NUM_TILES - 1) {
            // Source: Calculate the memory address of the NEXT tile in L2
            copy.ext = (uint32_t)&l2_matrix_a[(t + 1) * TILE_ROWS * MATRIX_SIZE];
            
            // Destination: If 't' is even, we are currently doing math on Buffer 0, 
            // so we tell the DMA to load the next tile into Buffer 1. (And vice-versa).
            copy.loc = (t % 2 == 0) ? (uint32_t)l1_buffer_1 : (uint32_t)l1_buffer_0;
            
            // Start the DMA. Crucially, we DO NOT call 'wait' here! 
            // This is asynchronous, allowing the cores to move immediately to the math phase.
            pi_cl_dma_memcpy(&copy); 
        }

        // --- COMPUTE PHASE ---
        // Point our 'current_A' pointer to whichever buffer the DMA IS NOT currently writing to.
        int32_t *current_A = (t % 2 == 0) ? l1_buffer_0 : l1_buffer_1;
        
        // Partition the current TILE among the 8 cores
        uint32_t local_chunk = TILE_ROWS / num_cores;
        uint32_t start_row = core_id * local_chunk;
        uint32_t end_row = start_row + local_chunk;

        // Perform the standard matrix multiplication math on the fast L1 buffer
        for (int i = start_row; i < end_row; i++) {
            for (int j = 0; j < MATRIX_SIZE; j++) {
                int32_t sum = 0;
                for (int k = 0; k < MATRIX_SIZE; k++) {
                    sum += current_A[i * MATRIX_SIZE + k] * matrix_b[k * MATRIX_SIZE + j];
                }
                // Calculate the true row position in the final Matrix C and store the result
                int global_row = (t * TILE_ROWS) + i;
                matrix_c[global_row * MATRIX_SIZE + j] = sum;
            }
        }

        // --- SYNCHRONIZATION PHASE ---
        // 1. All cores wait here until everyone is finished with the math for this tile.
        pi_cl_team_barrier();
        
        // 2. Before we loop around to start math on the NEXT tile, core 0 must check 
        // if the DMA has actually finished loading it. (Usually, the math takes longer
        // than the DMA transfer, so this wait returns instantly).
        if (core_id == 0 && t < NUM_TILES - 1) {
            pi_cl_dma_wait(&copy); 
        }
        
        // 3. All cores wait here until core 0 confirms the new data is ready.
        pi_cl_team_barrier();
    }

    pi_perf_stop();

    // 4. Print Results from Manager Core
    if (core_id == 0) {
        uint32_t cycles = pi_perf_read(PI_PERF_CYCLES);
        uint32_t instr = pi_perf_read(PI_PERF_INSTR);
        float ipc = (float)instr / (float)cycles;

        printf("\n--- V3: DOUBLE BUFFERED ---\n");
        printf("Total Cycles:    %d\n", cycles);
        printf("Instructions:    %d\n", instr);
        printf("IPC (Efficiency): %.3f\n", ipc);
        printf("Load Stalls:     %d\n", pi_perf_read(PI_PERF_LD_STALL));
        printf("TCDM Contention: %d\n", pi_perf_read(PI_PERF_TCDM_CONT));
        printf("---------------------------\n");
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

//Results from the "Optimized" Contribution (Double-Buffered):
// --- V3: DOUBLE BUFFERED ---
// Total Cycles:    18736
// Instructions:    13867
// IPC (Efficiency): 0.740
// Load Stalls:     4101
// TCDM Contention: 0