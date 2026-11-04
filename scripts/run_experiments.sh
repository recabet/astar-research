#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# The protocol's per-process memory cap.  The largest process in the main run
# peaks well below this (the pilot's peak RSS was about 0.5 GiB).
ulimit -v 4194304

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
./build/test_core

# Pin to one CPU to reduce migration noise.  The environment is WSL2; this is
# the same setting for every treatment.
CPU="${CPU:-0}"
RUN="taskset -c ${CPU}"

./download_data.sh

# Full validation sample before timing.
${RUN} ./build/astar_research validate-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --samples 30 \
    --main-k 8 \
    --seed 20261030

${RUN} ./build/astar_research benchmark-grid \
    --out results/grid.csv \
    --sizes 128,256,512,1024 \
    --families open,random,clustered,rooms,maze \
    --reps 3 \
    --per-band 5 \
    --seed 20261030 \
    --time-limit-s 30

${RUN} ./build/astar_research benchmark-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out results/real.csv \
    --reps 5 \
    --per-band 10 \
    --main-k 8 \
    --seed 20261030 \
    --time-limit-s 30

python3 scripts/analyze_results.py
