#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

./download_data.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
./build/test_core

CPU="${CPU:-0}"
RUN="taskset -c ${CPU}"

${RUN} ./build/astar_research diagnose-grid \
    --out results/grid_diagnostics.csv \
    --sizes 128,256,512,1024 \
    --families open,random,clustered,rooms,maze \
    --per-band 5 \
    --seed 20261030

${RUN} ./build/astar_research diagnose-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out results/real_diagnostics.csv \
    --main-k 8 --per-band 10 --seed 20261030

${RUN} ./build/astar_research weighted-maze \
    --out results/weighted_maze.csv \
    --sizes 128,256,512,1024 \
    --per-band 5 --reps 3 --seed 20261030

${RUN} ./build/astar_research weighted-grid \
    --out results/weighted_families.csv \
    --sizes 128,256,512,1024 \
    --families open,random,clustered,rooms,maze \
    --per-band 5 --reps 3 --seed 20261030

${RUN} ./build/astar_research weighted-grid \
    --out results/weighted_families_closed.csv \
    --sizes 128,256,512,1024 \
    --families open,random,clustered,rooms,maze \
    --per-band 5 --reps 3 --seed 20261030 --reopen 0

${RUN} ./build/astar_research weighted-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out results/weighted_real.csv \
    --main-k 8 --per-band 10 --reps 5 --seed 20261030

${RUN} ./build/astar_research weighted-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out results/weighted_real_closed.csv \
    --main-k 8 --per-band 10 --reps 5 --seed 20261030 --reopen 0

${RUN} ./build/astar_research tie-plateau-grid \
    --out results/tie_plateau_grid.csv \
    --sizes 128,256,512,1024 \
    --families open,random,clustered,rooms,maze \
    --per-band 5 --seed 20261030

${RUN} ./build/astar_research tie-plateau-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out results/tie_plateau_real.csv \
    --main-k 8 --per-band 10 --seed 20261030

python3 scripts/analyze_novelty.py
python3 scripts/analyze_weighted_families.py
python3 scripts/analyze_real_tie_break.py
python3 scripts/analyze_tie_plateau.py
