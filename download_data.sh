#!/usr/bin/env bash
# Download and prepare the SNAP roadNet-CA graph used by the experiments.
#
# The generated files are intentionally NOT part of the submitted archive:
#   data/roadNet-CA.txt.gz       original SNAP download
#   data/roadNet-CA.txt          decompressed edge list
#   data/roadNet-CA.bin          compact CSR graph
#   data/roadNet-CA.alt16.bin    16 ALT landmark distance tables
#
# The script is idempotent: existing files are reused, and the C++ tools are
# built automatically if build/astar_research is missing.
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p data

URL="https://snap.stanford.edu/data/roadNet-CA.txt.gz"
GZ="data/roadNet-CA.txt.gz"
TXT="data/roadNet-CA.txt"
GRAPH="data/roadNet-CA.bin"
LANDMARKS="data/roadNet-CA.alt16.bin"

if [[ ! -f "$GZ" ]]; then
    echo "downloading $URL"
    curl -L --fail --retry 3 -o "$GZ" "$URL"
fi

if [[ ! -f "$TXT" ]]; then
    echo "decompressing $GZ"
    gzip -dc "$GZ" > "$TXT"
fi

if [[ ! -x build/astar_research ]]; then
    echo "building the C++ tools"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j "$(nproc)"
fi

if [[ ! -f "$GRAPH" || ! -f "$LANDMARKS" ]]; then
    echo "preprocessing the road graph and building ALT landmarks"
    ./build/astar_research preprocess-real \
        --input "$TXT" \
        --out-graph "$GRAPH" \
        --out-landmarks "$LANDMARKS" \
        --landmarks 16 \
        --seed 20261030
fi

echo "ready:"
ls -lh "$GZ" "$TXT" "$GRAPH" "$LANDMARKS"
