# PULP Matrix Simulation & Memory Optimization

This repository contains the source code, setup instructions, and environment details for running the PULP Matrix Simulation using the GVSoC simulator. 

This project investigates memory bottlenecks and software optimizations (Double-Buffering, Loop Unrolling) on a multicore RISC-V PULP-Open cluster.

---

## 1. Environment Download

* `muzair_pulp_env.tar`: The 6GB Docker container containing the PULP-SDK, RISC-V GNU Toolchain, and GVSoC simulator. 
  * **[Download the Docker Container Here](https://uoguelphca.sharepoint.com/:f:/s/ACAGroup/IgC8TpRCthvTSYwb5mayQYEnAVQklhLgBt6_WQvcb7ezYnU?e=PtO0Du)**

---

## 🛠️ 2. Docker Setup & Initialization

**Prerequisites:** You must have [Docker](https://www.docker.com/) installed and running on your system. 

### Step 1: Import the Environment
Place the downloaded `muzair_pulp_env.tar` file in your project folder. Open PowerShell or your terminal in that folder and run:

```powershell
docker import muzair_pulp_env.tar pulp_project
```
Your command line should look like thise:
```powershell
C:\Users\yourusername\Downloads>docker import muzair_pulp_env.tar pulp_project
```
Wait for a few minutes till it says: sha256:"Random Numbers and Letters"
```powershell
sha256:58904e41ac585a0d07d75f81352d4cfa25ddfc40a3c7dfd43eab04ac2d646e16
```
Then run:
```powershell
docker run -it --name pulp_sim pulp_project /bin/bash
```
### Step 2: Install Nano and Switch to the Correct User
Since Docker containers often lack a built-in text editor, we will install and use nano.

**Install nano:**

Stay in  root@...:/# and run:
```powershell
apt-get update
apt-get install nano -y
```
By default, Docker starts as root. Switch to the user profile where the tools are installed:


```powershell
su - muzair
```
(Your prompt should change from root@...:/# to muzair@...:~$)

### Step 3: Initialize the Tools (Required every session)
Activate the custom RISC-V compiler and the GVSoC simulator:

```powershell
export PATH=$PATH:/home/muzair/gvsoc/install/bin
source /home/muzair/pulp-sdk/configs/pulp-open.sh
```
Verify by typing 

```powershell 
which gvsoc
```
 If it returns a file path, you are ready to proceed.

### Step 4: Running the Tests (V1 - V5)
To test the different optimization strategies, you must update the baseline.c file before compiling.

**1. Navigate to the project directory:**
```powershell
cd ~/marsellus_test
```

**2. Open the file to edit:**
```powershell
nano baseline.c
```
**3. Update the code:**

Delete the current contents of the file.

Copy the complete code for the version you want to test (V1, V2, V3, V4, or V5) from the provided test files.

Paste the code into your terminal (usually done by right-clicking or pressing Ctrl + Shift + V).

**4. Save and exit:**

Press Ctrl + O and hit Enter to save.

Press Ctrl + X to exit nano.

Run the simulation. We intentionally restrict the L1 memory to 8 banks to force and measure memory contention:
```powershell
make clean all run
```
