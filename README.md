# PULP Matrix Simulation & Memory Optimization

This repository contains the source code, setup instructions, and environment details for running the PULP Matrix Simulation using the GVSoC simulator. 

This project investigates memory bottlenecks and software optimizations (Double-Buffering, Loop Unrolling) on a multicore RISC-V PULP-Open cluster.

---

## 📥 1. Environment Download

* `muzair_pulp_env.tar`: The 6GB Docker container containing the PULP-SDK, RISC-V GNU Toolchain, and GVSoC simulator. 
  * **[Download the Environment from Google Drive Here](INSERT_YOUR_GOOGLE_DRIVE_LINK_HERE)**

---

## 🛠️ 2. Docker Setup & Initialization

**Prerequisites:** You must have [Docker](https://www.docker.com/) installed and running on your system. 

### Step 1: Import the Environment
Place the downloaded `muzair_pulp_env.tar` file in your project folder. Open PowerShell or your terminal in that folder and run:

```powershell
docker import muzair_pulp_env.tar pulp_project
docker run -it --name pulp_sim pulp_project /bin/bash
Step 2: Switch to the Correct User
By default, Docker starts as root. Switch to the user profile where the tools are installed:

Bash
su - muzair
(Your prompt should change from root@...:/# to muzair@...:~$)

Step 3: Initialize the Tools (Required every session)
Activate the custom RISC-V compiler and the GVSoC simulator:

Bash
export PATH=$PATH:/home/muzair/gvsoc/install/bin
source /home/muzair/pulp-sdk/configs/pulp-open.sh
(Verify by typing which gvsoc. If it returns a file path, you are ready to proceed).

🧪 3. Running the Tests (V1 - V5)
To test the different optimization strategies, you must update the main C file before compiling.

Navigate to the project directory: cd ~/marsellus_test

Open the baseline.c file and delete its current contents.

Copy the complete code for the version you want to test (V1, V2, V3, V4, or V5) from the provided test files and paste it into baseline.c.

Save the file.

Run the simulation. We intentionally restrict the L1 memory to 8 banks to force and measure memory contention:

Bash
make clean all run GV_OPT="--config-opt=cluster/l1/banks=8"