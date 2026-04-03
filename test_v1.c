//Version 1: The "Ideal" Baseline (Direct L1 Access)
//Copy and paste this code into baseline.c
#include "pmsis.h" // Standard PULP Microcontroller Software Interface Standard (PMSIS) API

#define MATRIX_SIZE 32 // Defines the dimensions for our 32x32 matrices

// --- MEMORY ALLOCATION ---
// The PI_L1 macro explicitly instructs the linker to place these arrays into 
// the fast L1 Tightly Coupled Data Memory (TCDM). Because all data fits into L1, 
// we bypass the slow L2 memory entirely, establishing our theoretical maximum speed
PI_L1 int32_t matrix_a[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_b[MATRIX_SIZE * MATRIX_SIZE];
PI_L1 int32_t matrix_c[MATRIX_SIZE * MATRIX_SIZE];

// --- CORE COMPUTE KERNEL ---
// This function will be executed in parallel by every active core in the cluster.
void cluster_matrix_mult(void *arg) {
    // Identify which specific core is currently running this instance of the function
    uint32_t core_id = pi_core_id();
    // Get the total number of active cores in the cluster (typically 8 for PULP-Open)
    uint32_t num_cores = pi_cl_team_nb_cores();

    // 1. Configure the 4 Performance Counters
    // We configure the hardware to track specific metrics to analyze efficiency.
    pi_perf_conf(
        (1 << PI_PERF_CYCLES) |   // Total clock cycles elapsed
        (1 << PI_PERF_INSTR) |    // Total instructions executed
        (1 << PI_PERF_LD_STALL) | // Cycles stalled waiting for memory loads
        (1 << PI_PERF_TCDM_CONT)  // Cycles delayed due to L1 memory bank access conflicts
    );
    pi_perf_reset(); // Clear previous counter values
    pi_perf_start(); // Begin tracking hardware events

    // 2. Work Partitioning (Parallelization Strategy)
    // We divide the work by assigning a specific "chunk" of rows to each core.
    // E.g., for 32 rows and 8 cores, each core computes exactly 4 rows.
    uint32_t chunk = MATRIX_SIZE / num_cores;
    uint32_t start_row = core_id * chunk; // Core 0 starts at row 0, Core 1 at row 4, etc.
    uint32_t end_row = start_row + chunk; // Where this specific core should stop

    // 3. Math Loop
    // Standard matrix multiplication loop, but restricted to the core's assigned rows.
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            int32_t sum = 0;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                // Multiply-Accumulate (MAC) operation
                sum += matrix_a[i * MATRIX_SIZE + k] * matrix_b[k * MATRIX_SIZE + j];
            }
            matrix_c[i * MATRIX_SIZE + j] = sum; // Store the final dot product result
        }
    }
    

    // Stop tracking hardware events now that the math is done
    pi_perf_stop();

    // 4. Print Results from Manager Core
    // We only want to print the results once. By restricting the print statement
    // to core_id == 0 (the master core), we prevent 8 cores from trying to print
    // to the console simultaneously.
    if (core_id == 0) {
        uint32_t cycles = pi_perf_read(PI_PERF_CYCLES);
        uint32_t instr = pi_perf_read(PI_PERF_INSTR);

        // Instructions Per Cycle (IPC) is our primary metric for compute efficiency.
        // An IPC closer to 1.0 means the processor is doing useful work almost every cycle.
        float ipc = (float)instr / (float)cycles;

        printf("\n--- V1: IDEAL L1 BASELINE ---\n");
        printf("Total Cycles:    %d\n", cycles);
        printf("Instructions:    %d\n", instr);
        printf("IPC (Efficiency): %.3f\n", ipc);
        printf("Load Stalls:     %d\n", pi_perf_read(PI_PERF_LD_STALL));
        printf("TCDM Contention: %d\n", pi_perf_read(PI_PERF_TCDM_CONT));
        printf("-----------------------------\n");
    }
}

// --- CLUSTER DELEGATE WRAPPER ---
// This function acts as a bridge. The host processor calls this, which then
// commands the cluster to wake up its cores and run the matrix mult function.
void cluster_delegate(void *arg) {
    // Wake up all available cluster cores and assign them the math function
    pi_cl_team_fork(pi_cl_team_nb_cores(), cluster_matrix_mult, NULL);
}

// --- MAIN FABRIC CONTROLLER ---
// This code runs on the main SoC (Fabric Controller), not the compute cluster.
int main() {
    struct pi_device cluster_dev;
    struct pi_cluster_conf cl_conf;

    // Initialize cluster configuration with default parameters
    pi_cluster_conf_init(&cl_conf);
    pi_open_from_conf(&cluster_dev, &cl_conf);

    // Power on and open the cluster device. Exit if it fails.
    if (pi_cluster_open(&cluster_dev)) return -1;

    // Create a task descriptor pointing to our delegate wrapper function
    struct pi_cluster_task task;
    pi_cluster_task(&task, cluster_delegate, NULL);

    // Offload the task from the main SoC to the compute cluster. 
    // This blocks until the cluster finishes the computation.
    pi_cluster_send_task_to_cl(&cluster_dev, &task);

    // Power down the cluster to save energy now that the job is done
    pi_cluster_close(&cluster_dev);
    return 0;
}

// Results from the "Ideal" Baseline (Direct L1 Access):
// --- V1: IDEAL L1 BASELINE ---
// Total Cycles:    17971
// Instructions:    13638
// IPC (Efficiency): 0.759
// Load Stalls:     4096
// TCDM Contention: 0
// -----------------------------
