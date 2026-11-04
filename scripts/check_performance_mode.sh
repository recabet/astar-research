#!/usr/bin/env bash
# Sensitivity check for the host CPU performance/power mode.
#
# Usage:
#   ./scripts/check_performance_mode.sh high
#   python3 scripts/analyze_perf_mode.py high
#
# The frozen main run supplies the low-mode baseline, so a separate low-mode
# subset is optional.  To compare two subset runs collected in one session:
#   ./scripts/check_performance_mode.sh low
#   # switch Windows/Linux to the other performance mode, then
#   ./scripts/check_performance_mode.sh high
#   python3 scripts/compare_performance_modes.py low high
#
# This is a subset of the frozen experiment.  It does not overwrite the main
# results/grid.csv or results/real.csv.  It is a robustness check, not a
# replacement for the frozen protocol.
#
# The subset keeps the frozen per-band and repeat settings, so its queries are
# identical to the corresponding rows of the main run.  That makes the high-mode
# subset directly pairable with the frozen low-mode data (see
# scripts/analyze_perf_mode.py) as well as with a low-mode subset run.
set -euo pipefail

cd "$(dirname "$0")/.."

TAG="${1:-current}"
if [[ -z "$TAG" ]]; then
    echo "usage: $0 TAG" >&2
    exit 1
fi

./download_data.sh

CPU="${CPU:-0}"
RUN="taskset -c ${CPU}"

${RUN} ./build/astar_research benchmark-grid \
    --out "results/perf_mode_${TAG}_grid.csv" \
    --sizes 128,512 \
    --families open,random,clustered,rooms,maze \
    --reps 3 \
    --per-band 5 \
    --seed 20261030 \
    --time-limit-s 30

${RUN} ./build/astar_research benchmark-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out "results/perf_mode_${TAG}_real.csv" \
    --reps 5 \
    --per-band 10 \
    --main-k 8 \
    --seed 20261030 \
    --time-limit-s 30

echo
echo "subset written to:"
echo "  results/perf_mode_${TAG}_grid.csv"
echo "  results/perf_mode_${TAG}_real.csv"
