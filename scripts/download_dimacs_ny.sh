#!/usr/bin/env bash
# Download the DIMACS New York distance graph and its coordinates, then build the
# weighted CSR graph, weighted ALT landmarks, and projected coordinates.
#
# Files are intentionally kept in data/ (ignored by git):
#   data/USA-road-d.NY.gr   DIMACS distance graph
#   data/USA-road-d.NY.co   DIMACS coordinates
#   data/roadNet-NY.dimacs.bin
#   data/roadNet-NY.alt16.bin
#   data/roadNet-NY.coords.bin
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p data

GR="data/USA-road-d.NY.gr"
CO="data/USA-road-d.NY.co"
GRAPH="data/roadNet-NY.dimacs.bin"
LANDMARKS="data/roadNet-NY.alt16.bin"
COORDS="data/roadNet-NY.coords.bin"

BASE="https://raw.githubusercontent.com/Jiboxiake/EDP/master"

if [[ ! -f "$GR" ]]; then
    echo "downloading $GR"
    curl -L --fail --retry 3 -o "$GR" "$BASE/USA-road-d.NY.gr"
fi
if [[ ! -f "$CO" ]]; then
    echo "downloading $CO"
    curl -L --fail --retry 3 -o "$CO" "$BASE/USA-road-d.NY.co"
fi

if [[ ! -x build/astar_research ]]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j "$(nproc)"
fi

if [[ ! -f "$GRAPH" || ! -f "$LANDMARKS" || ! -f "$COORDS" ]]; then
    ./build/astar_research preprocess-dimacs \
        --gr "$GR" --co "$CO" \
        --out-graph "$GRAPH" \
        --out-landmarks "$LANDMARKS" \
        --out-coords "$COORDS" \
        --landmarks 16 --seed 20261030
fi

echo "ready:"
ls -lh "$GR" "$CO" "$GRAPH" "$LANDMARKS" "$COORDS"
