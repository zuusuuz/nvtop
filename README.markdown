# Nvtop (Intel Battlemage / Xe Fix Fork)

This is a fork of the excellent [Syllo/nvtop](https://github.com/Syllo/nvtop) project.

## The Purpose of This Fork
This fork addresses specific issues with **Intel Xe-based GPUs** (specifically the **Intel Arc B580 "Battlemage"**) when running in **Snapshot Mode** (`nvtop -s`).

On newer Intel drivers using the Xe kernel module, global hardware counters for GPU utilization are often unavailable or report as zero/null in headless environments. This results in standard monitoring tools outputting `null` for `gpu_util` and `power_draw` when used for scripting or dashboards.

## Changes Made

The `src/nvtop.c` logic has been modified to support a "Triple-Pass" measurement strategy specifically for the `-s` flag.

### 1. "Triple-Pass" Measurement Cycle
The standard snapshot mode often reads sensors instantly, failing to capture the "delta" required to calculate usage and power. This fork implements a robust cycle:
1.  **Warm-up Pass:** Initializes the `fdinfo` process list and wakes up the driver sensors.
2.  **Start Timestamp:** Records the initial state of all running processes.
3.  **Measurement Interval:** Sleeps for 1 second to allow work to accumulate.
4.  **Finish Timestamp:** Records the final state.

### 2. Process Summation Fallback
Since the Battlemage B580 does not reliably report a "Global GPU Load" percentage via the Xe driver, this fork calculates the true GPU usage by:
* Scanning all active processes.
* Summing their individual Render (RCS), Compute (CCS), and Video (VCS/VECS) usage.
* Forcing this summed value into the global `gpu_util` field.

### 3. Direct JSON Printing
To bypass internal validation flags that frequently mark Intel Xe data as "Invalid" (resulting in `null` output), the snapshot printer has been rewritten to:
* Directly output the calculated summation.
* Guarantee a valid integer output (0-100%) for `gpu_util`.
* Correctly format power draw (converting mW to W).

## Usage

Build as normal:

```bash
mkdir build && cd build
cmake ..
make
