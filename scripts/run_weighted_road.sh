#!/usr/bin/env bash
# Reproduce the weighted DIMACS New York experiment.
set -euo pipefail

cd "$(dirname "$0")/.."

./scripts/download_dimacs_ny.sh

./build/astar_research benchmark-weighted-road \
    --graph data/roadNet-NY.dimacs.bin \
    --landmarks data/roadNet-NY.alt16.bin \
    --coords data/roadNet-NY.coords.bin \
    --out results/weighted_road.csv \
    --main-k 8 --per-band 10 --reps 5 --seed 20261030

python3 scripts/analyze_weighted_road.py
