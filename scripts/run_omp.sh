#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ulimit -v 4194304
./download_data.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
./build/test_core

NPROC="$(nproc)"
export OMP_NUM_THREADS="$NPROC"
THREADS="${THREADS:-1,2,4,8,${NPROC}}"
CPU_MAX=$((NPROC - 1))

OMP_PROC_BIND=close OMP_PLACES=cores taskset -c "0-${CPU_MAX}" \
    ./build/astar_research omp-scaling \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out-batch results/omp_scaling.csv \
    --out-tasks results/omp_tasks.csv \
    --sizes 1024 \
    --threads "$THREADS" \
    --per-band 5 \
    --reps 3 \
    --batch-reps 3 \
    --seed 20261030

python3 scripts/analyze_omp.py
