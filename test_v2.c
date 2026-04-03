// Version 2: The "Naive" Baseline (Direct L2 Access)
//Copy and paste this code into baseline.c
#include "pmsis.h" // Standard PULP Microcontroller Software Interface Standard (PMSIS) API

#define MATRIX_SIZE 32 // Defines the dimensions for our 32x32 matrices

// --- MEMORY ALLOCATION (THE BOTTLENECK) ---
// The PI_L2 macro explicitly forces the linker to place Matrix A into the slower, 
// external L2 memory rather than the tightly coupled L1 memory. 
// Matrix B and C remain in the fast L1 memory (TCDM). This simulates a "naive" 
// scenario where a dataset is too large for L1, but the engineer fails to use the DMA.
PI_L2 int32_t l2_matrix_a[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_b[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_c[MATRIX_SIZE * MATRIX_SIZE];

// --- CORE COMPUTE KERNEL ---
void cluster_matrix_mult(void *arg) {
    // Identify the specific core running this thread and total available cores
    uint32_t core_id = pi_core_id();
    uint32_t num_cores = pi_cl_team_nb_cores();

    // 1. Configure the 4 Performance Counters
    // We track the same hardware events as Version 1 to allow for a direct 1-to-1 comparison.
    pi_perf_conf(
        (1 << PI_PERF_CYCLES) |     // Total clock cycles elapsed
        (1 << PI_PERF_INSTR) |      // Total instructions executed
        (1 << PI_PERF_LD_STALL) |   // Cycles stalled waiting for memory loads
        (1 << PI_PERF_TCDM_CONT)    // Cycles delayed due to L1 memory bank access conflicts
    );
    pi_perf_reset(); // Clear previous counter values
    pi_perf_start(); // Begin tracking hardware events

    // 2. Work Partitioning
    // Divide the 32 matrix rows equally among the 8 cores (4 rows per core)
    uint32_t chunk = MATRIX_SIZE / num_cores;
    uint32_t start_row = core_id * chunk;
    uint32_t end_row = start_row + chunk;

    // 3. Math Loop (Fetching directly from L2)
    // Because we are fetching operands directly from L2 memory over the interconnect
    // without the DMA, this loop becomes severely memory-bound.
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            int32_t sum = 0;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                // THE STALL: Every time the core tries to read l2_matrix_a, it 
                // triggers an AXI bus transaction to the external L2 memory. 
                // The RISC-V pipeline must completely stall and wait for the data 
                // to arrive before it can perform the multiplication.
                sum += l2_matrix_a[i * MATRIX_SIZE + k] * matrix_b[k * MATRIX_SIZE + j];
            }
            matrix_c[i * MATRIX_SIZE + j] = sum; // Store result in fast L1
        }
    }

    // Stop tracking hardware events
    pi_perf_stop();

    // 4. Print Results from Manager Core
    // Confined to core 0 to prevent print collisions on the console.
    if (core_id == 0) {
        uint32_t cycles = pi_perf_read(PI_PERF_CYCLES);
        uint32_t instr = pi_perf_read(PI_PERF_INSTR);
        
        // Calculate Instructions Per Cycle (IPC)
        float ipc = (float)instr / (float)cycles;

        printf("\n--- V2: NAIVE L2 ACCESS ---\n");
        printf("Total Cycles:    %d\n", cycles);
        printf("Instructions:    %d\n", instr);
        printf("IPC (Efficiency): %.3f\n", ipc);
        printf("Load Stalls:     %d\n", pi_perf_read(PI_PERF_LD_STALL));
        printf("TCDM Contention: %d\n", pi_perf_read(PI_PERF_TCDM_CONT));
        printf("---------------------------\n");
    }
}

// --- CLUSTER DELEGATE WRAPPER ---
void cluster_delegate(void *arg) {
    // Wake up all available cluster cores and assign them the math function
    pi_cl_team_fork(pi_cl_team_nb_cores(), cluster_matrix_mult, NULL);
}

// --- MAIN FABRIC CONTROLLER ---
int main() {
    struct pi_device cluster_dev;
    struct pi_cluster_conf cl_conf;
    
    // Initialize and open the cluster
    pi_cluster_conf_init(&cl_conf);
    pi_open_from_conf(&cluster_dev, &cl_conf);
    if (pi_cluster_open(&cluster_dev)) return -1;

    // Dispatch the task
    struct pi_cluster_task task;
    pi_cluster_task(&task, cluster_delegate, NULL);
    pi_cluster_send_task_to_cl(&cluster_dev, &task);
    
    // Power down the cluster
    pi_cluster_close(&cluster_dev);
    
    return 0;
}

// Results from the "Naive" Baseline (Direct L2 Access):
// --- V2: NAIVE L2 ACCESS ---
// Total Cycles:    75315 (Massive increase due to L2 latency)
// Instructions:    13638 (Exact same instruction count as V1)
// IPC (Efficiency): 0.181 (Pipeline is effectively starved for data)
// Load Stalls:     4096
// TCDM Contention: 0
// ---------------------------