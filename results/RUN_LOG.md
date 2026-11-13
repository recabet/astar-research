# Run log

Date: 2026-10-30.
Protocol: `PROTOCOL.md` (frozen before the main run).

## Environment

- CPU: Intel Core Ultra 9 285H, 16 logical CPUs.
- RAM: 30 GiB.
- OS: Ubuntu 24.04.5 LTS on WSL2, kernel 6.6.87.1-microsoft-standard-WSL2.
- Compiler: GCC 13.3.0, C++20, CMake 4.4.2, Release (`-O3 -DNDEBUG`).
- Timing: one CPU via `taskset -c 0`; process memory cap
  `ulimit -v 4194304` (4 GiB).
- Timer: `std::chrono::steady_clock`, nanoseconds.
- Host power mode: **lowest performance/power mode** during the main run
  (reported by the operator).  Absolute times would change in another mode;
  the experiment is paired, so ratios are the primary result.  A subset
  sensitivity check is in `scripts/check_performance_mode.sh`.

## Build and correctness

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 16
./build/test_core
```

The test suite passed:

- binary heap order;
- hand-computed weighted graph vs Floyd–Warshall;
- hand-checked 5x5 grid (distance 8);
- no-path grid;
- all five 32x32 grid families vs BFS;
- ALT admissibility and consistency;
- tie-break distance equivalence;
- six real-graph queries vs BFS.

An additional AddressSanitizer/UndefinedBehaviorSanitizer build of the test
suite also passed.  The 30-query real validation separately passed:

```text
validation: 30 queries; BFS oracle, Dijkstra and A* distances agree and both paths are valid
```

## Main run

```text
./build/astar_research benchmark-grid \
  --out results/grid.csv --sizes 128,256,512,1024 \
  --families open,random,clustered,rooms,maze \
  --reps 3 --per-band 5 --seed 20261030 --time-limit-s 30

./build/astar_research benchmark-real \
  --graph data/roadNet-CA.bin \
  --landmarks data/roadNet-CA.alt16.bin \
  --out results/real.csv \
  --reps 5 --per-band 10 --main-k 8 --seed 20261030 --time-limit-s 30
```

- Grid: 20 graphs, 300 queries, 3 repeats, 3 treatments -> 2,700 rows.
- Real: 1 graph, 30 queries, 5 repeats, 6 treatments -> 900 rows.
- Total: 3,600 rows; all `status=ok` and `correct=1`; **no timeouts**.
- Wall-clock time for tests + validation + both benchmarks: about **2 min 45 s**
  (`user 160.98 s`).
- Median within-query time CV: grid Dijkstra 7.2%, grid A* 10.1%, real
  Dijkstra 7.4%, real A* K=8 9.6%.
- Expansions were deterministic across repeats (0 of 1,080 query/treatment
  groups had more than one distinct expansion count).

## Analysis

```text
python3 scripts/analyze_results.py
```

Outputs: `results/analysis.md`, `results/summary_*.csv`,
`results/paired_*.csv`, `figures/*.png`.

## Data availability

The large SNAP/filtered graph files are intentionally not shipped so the project
can be archived without 278 MB of generated binaries.  Run `./download_data.sh`
to recreate them.  These are the checksums of the exact files used for the
reported run:

```text
8a0d9cd0c6d71dbe63a1e368acbb891d2cfb76dcb58534627ec6a10f30d814e5  results/grid.csv
a9b2f8bf4ccd8745cf7b3455d3d923cb109632451577abd6c5e043a1483b406e  results/real.csv
383f7b14424530a9e25c392969a9c19dace32ebcfd141f0967a1433498c2acdd  data/roadNet-CA.txt.gz
6446779dc9a374004c9af78b921ef47136e9e19e80c16a5c961f18f67894fddf  data/roadNet-CA.bin
cfba9dbf41b37e3c2e0a4d264f138243d51334fc6d0e10748b9a62f4af5edaf9  data/roadNet-CA.alt16.bin
```

## Post-freeze exploratory runs

These runs were made after the main protocol and are reported separately in
`NOVELTY.md`; they do not replace `results/grid.csv` or `results/real.csv`.

```text
./build/astar_research diagnose-grid \
  --out results/grid_diagnostics.csv --sizes 128,256,512,1024 \
  --families open,random,clustered,rooms,maze --per-band 5 --seed 20261030

./build/astar_research diagnose-real \
  --graph data/roadNet-CA.bin --landmarks data/roadNet-CA.alt16.bin \
  --out results/real_diagnostics.csv --main-k 8 --per-band 10 --seed 20261030

./build/astar_research weighted-maze \
  --out results/weighted_maze.csv --sizes 128,256,512,1024 \
  --per-band 5 --reps 3 --seed 20261030
```

- Grid diagnostics: 300 queries; wall clock 21.7 s; peak RSS 116 MB.
- Real diagnostics: 30 queries; wall clock 17.0 s; peak RSS 317 MB.
- Weighted A*: 1,260 rows (180 Dijkstra + 1,080 weighted), all `status=ok`;
  wall clock 15.6 s; peak RSS 81 MB.
- `epsilon=1` returns the optimal distance on every query; all weighted runs
  return at least the optimal distance.  On the perfect mazes, the returned
  path-cost ratio is exactly 1.00.
- Analysis: `python3 scripts/analyze_novelty.py`.

The host was still in its lowest performance/power mode for these exploratory
runs; all comparisons are paired and within-mode.

## Weighted-A* sweeps and real tie-break (2026-11-04)

A second exploratory pass widened weighted A* from mazes to all grid families
and the road graph, and tested both reopen policies.  The closed-list policy is
the primary result; reopening is a sensitivity check.

```text
./build/astar_research weighted-grid \
  --out results/weighted_families.csv --sizes 128,256,512,1024 \
  --families open,random,clustered,rooms,maze --per-band 5 --reps 3 \
  --seed 20261030

./build/astar_research weighted-grid \
  --out results/weighted_families_closed.csv --sizes 128,256,512,1024 \
  --families open,random,clustered,rooms,maze --per-band 5 --reps 3 \
  --seed 20261030 --reopen 0

./build/astar_research weighted-real \
  --graph data/roadNet-CA.bin --landmarks data/roadNet-CA.alt16.bin \
  --out results/weighted_real.csv --main-k 8 --per-band 10 --reps 5 \
  --seed 20261030

./build/astar_research weighted-real \
  --graph data/roadNet-CA.bin --landmarks data/roadNet-CA.alt16.bin \
  --out results/weighted_real_closed.csv --main-k 8 --per-band 10 --reps 5 \
  --seed 20261030 --reopen 0
```

- Epsilon grid: 1.0, 1.1, 1.25, 1.5, 2, 3, 5, 10, 20, 50.
- Grid sweeps: 20 graphs x 15 queries x 3 repeats x 11 treatments = 9,900 rows
  each; wall clock 26.3 s (reopen) and 24.3 s (closed).
- Road sweeps: 30 queries x 5 repeats x 11 treatments = 1,650 rows each; wall
  clock 118 s (reopen) and about 2 min (closed).
- All rows `status=ok`; `epsilon=1` returns the optimal distance under both
  policies, and no weighted run beats the optimum.
- Closed-list results: random `epsilon=1.1` 20.1x at 1.8% median cost;
  rooms `epsilon=1.25` 24.8x at 3.1%; clustered `epsilon=1.5` 26.5x at 5.0%;
  road `epsilon=1.1` 8.8x at 1.4%.  Reopening on the road collapses to 5.4x.
- Analysis: `python3 scripts/analyze_weighted_families.py` writes
  `results/weighted_families.md`, the `summary_weighted_*` CSV files,
  `results/paired_weighted_families.csv` and `figures/weighted_pareto.png`.
- Real tie-break: `python3 scripts/analyze_real_tie_break.py` reuses the frozen
  `results/real.csv` (no new timings) and writes `results/real_tie_break.md`,
  `summary_real_tie_break.csv`, `paired_real_tie_break.csv` and
  `figures/real_tie_break.png`.  `node_id` expands 1.14x as many vertices and
  is worse on 27/30 queries; the expansion difference is p=4.4e-6, while the
  time difference (1.09x) is noisy (p=0.124).
- Raw-file checksums:

```text
0e6b9e8d53efb7a4e45d93beec30d657c8fc38fc2f5b674662df973e780a94f8  results/weighted_families.csv
6239b754dfc64f897355e579da48403b28df2c03c76c4cc2621bebfa8438ed04  results/weighted_families_closed.csv
8fca7ae5f8158a26033666147c8de21adcdf20353582c17ed8af0182a239cb0e  results/weighted_real.csv
c57a082d64a56f926d670ec35ef2e564d474573c03303cc8c6fe6b2bd4239826  results/weighted_real_closed.csv
```

## Equal-f plateau mechanism run (2026-11-13)

The search now also counts maximal runs of settled vertices that share the same
`f` value.  This is a post-freeze instrumentation change; the frozen `grid.csv`
and `real.csv` schemas and values are untouched.

```text
./build/astar_research tie-plateau-grid \
  --out results/tie_plateau_grid.csv --sizes 128,256,512,1024 \
  --families open,random,clustered,rooms,maze --per-band 5 --seed 20261030

./build/astar_research tie-plateau-real \
  --graph data/roadNet-CA.bin --landmarks data/roadNet-CA.alt16.bin \
  --out results/tie_plateau_real.csv --main-k 8 --per-band 10 \
  --seed 20261030
```

- Grid: 20 graphs x 15 queries x 2 tie-breaks = 600 rows; wall clock 9.5 s;
  peak RSS 110 MB.
- Road: 30 queries x 2 tie-breaks = 60 rows; wall clock 7.7 s; peak RSS 305 MB.
- All 660 rows `status=ok`; every returned distance matches the frozen oracle.
- Road result: longer equal-f plateaus predict the `node_id` penalty
  (expansion rho=0.53, p=0.003; time rho=0.41, p=0.025).  Low/mid/high plateau
  tertiles have median time ratios 1.02/1.07/1.25.  Open grids are one
  saturated plateau; mazes have almost none.
- Analysis: `python3 scripts/analyze_tie_plateau.py` writes
  `results/tie_plateau.md`, `summary_tie_plateau*.csv`,
  `paired_tie_plateau.csv` and `figures/tie_plateau.png`.
- Checksums:

```text
61eed48d05c23cd0d69bfb97a4eea32535900cc9aaddaf6ab55945a804f721a5  results/tie_plateau_grid.csv
495bb06b0664bee408a79738f532ec2fbe1cb74231669c9b6b781ab40fa37a97  results/tie_plateau_real.csv
```

## Weighted road network run (2026-11-13)

Separate data regime: DIMACS `USA-road-d.NY` with coordinates and distance
weights.  The frozen SNAP California data and results are untouched.

```text
./scripts/download_dimacs_ny.sh

./build/astar_research benchmark-weighted-road \
  --graph data/roadNet-NY.dimacs.bin \
  --landmarks data/roadNet-NY.alt16.bin \
  --coords data/roadNet-NY.coords.bin \
  --out results/weighted_road.csv \
  --main-k 8 --per-band 10 --reps 5 --seed 20261030

python3 scripts/analyze_weighted_road.py
```

- Source: DIMACS `USA-road-d.NY` `.gr`/`.co`, downloaded from the public EDP
  mirror; `.gr` weights are integer distance units and `.co` holds coordinates.
- Preprocessing: 264,346 nodes, 733,846 input arcs, 730,100 undirected weighted
  arcs after symmetrisation/deduplication; wall clock 3.2 s, peak RSS 86 MB.
- Benchmark: 30 weighted-band queries x 5 repeats x 7 treatments = 1,050 rows;
  wall clock 8.1 s, peak RSS 86 MB; all `status=ok`, `correct=1`.
- Every treatment returned the weighted Dijkstra optimum.
- Median time ratios: Euclidean 1.6x, ALT K=1 1.6x, K=4 10.0x, K=8 18.4x,
  K=16 21.6x.  `node_id` / `larger_g` expansion ratio 1.08x.
- Outputs: `results/weighted_road.md`, `summary_weighted_road*.csv`,
  `paired_weighted_road.csv`, `figures/weighted_road.png`.
- Input checksums:

```text
37f5b3eff82330850466956b6c036a8ab8551512bdc7509edca1d4c6cf943fa3  data/USA-road-d.NY.gr
c39ec9b87c66d6e254f80cd3cfe20c7eb3c5ce4aca4063dc2efc228ab12fa95f  data/USA-road-d.NY.co
```

- Output checksum:

```text
39e93de23f68f52612c07d98ec767ea12be0224275beb43fb255be1b7a05ab9c  results/weighted_road.csv
```

### Weighted A* and same-topology control

```text
./build/astar_research weighted-road-eps \
  --graph data/roadNet-NY.dimacs.bin \
  --landmarks data/roadNet-NY.alt16.bin \
  --coords data/roadNet-NY.coords.bin \
  --out results/weighted_road_eps.csv \
  --main-k 8 --per-band 10 --reps 5 --seed 20261030

./build/astar_research benchmark-unit-road \
  --graph data/roadNet-NY.dimacs.bin \
  --queries results/weighted_road.csv \
  --out results/unit_road.csv \
  --main-k 8 --reps 5 --seed 20261030
```

- Weighted epsilon sweep: 30 queries x 5 repeats x (1 Dijkstra + 2 policies x
  10 epsilons) = 3,150 rows; wall clock 3.1 s, peak RSS 56 MB.
- Unit control: 30 identical query pairs x 5 repeats x 6 treatments = 900 rows;
  wall clock 7.8 s, peak RSS 81 MB.
- Both runs: all `status=ok`; every distance matches its oracle; `epsilon=1` is
  optimal under both policies.
- Closed-list weighted A*: `epsilon=1.1` 35.1x at 1.1% median cost;
  `epsilon=1.25` 53.9x at 4.1%; reopening is slower at every `epsilon>1`.
- Same-topology control: ALT K=8 10.4x on unit weights vs 18.4x on distance
  weights; `node_id` / `larger_g` time penalty 1.17x vs 1.08x.
- Outputs: `results/weighted_road_eps.md`, `results/unit_road.md`, summary and
  paired CSVs, `figures/weighted_road_eps.png`, `figures/unit_road.png`.
- Checksums:

```text
fa98304f5e269afea561a52ee7c0ec03f3924aa58dc83e5ba4e704b0330a54d5  results/weighted_road_eps.csv
3084bb987958cd31d34cf7dcc08f79bd3dbc0ec2653e90b75b845d5587321f00  results/unit_road.csv
```

## OpenMP batch-throughput run

Separate from the frozen protocol and the query-level study.  The process was
pinned to CPUs 0--15 with `taskset`; OpenMP used `OMP_PROC_BIND=close` and
`OMP_PLACES=cores`; the 4 GiB process cap was applied.

```text
ulimit -v 4194304
OMP_PROC_BIND=close OMP_PLACES=cores taskset -c 0-15 \
  ./build/astar_research omp-scaling \
  --graph data/roadNet-CA.bin --landmarks data/roadNet-CA.alt16.bin \
  --out-batch results/omp_scaling.csv --out-tasks results/omp_tasks.csv \
  --sizes 1024 --threads 1,2,4,8,16 --per-band 5 --reps 3 --batch-reps 3 \
  --seed 20261030
```

- Workload: five `1024x1024` grids (15 queries each, 3 repeats) + 30 road
  queries (3 repeats) = 270 tasks per batch.
- Thread counts: 1, 2, 4, 8, 16; 3 timed batch repetitions plus one warm-up
  per thread count.
- Rows: 90 batch rows, 4,050 task rows; all `status=ok`.
- Wall clock: 5 min 43 s; maximum RSS 1.08 GB.
- Batch time at 1 thread: 44.39 s; at 16 threads: 4.56 s; speedup **9.73x**,
  efficiency 0.61.  Grid-only 9.93x, road-only 9.36x.
- Median per-query latency at 16 threads: Dijkstra 140.8 ms (21% above 1
  thread), A* 6.17 ms (18% above 1 thread).  Median Dijkstra/A* ratio moves
  from 14.8x to 17.9x; the maze remains below parity.
- Analysis: `python3 scripts/analyze_omp.py`.

The host was still in its lowest performance/power mode; all OpenMP timings are
from that mode.  This is a throughput result, not a replacement for the serial
query-level protocol.

## Performance-mode sensitivity run (2026-11-04)

After the frozen and exploratory runs, the host was switched to its higher
performance/power mode (reported by the operator).  We reran an exactly matched
subset of the frozen design so that every row pairs with a low-mode row:

```text
./scripts/check_performance_mode.sh high
python3 scripts/analyze_perf_mode.py high
```

- Grid: sizes 128 and 512, all five families, 15 queries per graph, 3 repeats,
  3 treatments -> 1,350 rows.
- Real: all 30 frozen queries, 5 repeats, 6 treatments -> 900 rows.
- All 2,250 rows have `status=ok` and `correct=1`; no timeouts.
- Expansion counts and returned distances are identical to the frozen low-mode
  rows on every paired query.
- Wall clock for the whole subset: 50 s (`user 49.73 s`), maximum RSS 527 MB.
- Absolute speed-up from the higher mode: grid Dijkstra 2.24x, grid A* 2.23x;
  road Dijkstra 2.26x, road A* 2.23x (medians).
- Paired Dijkstra/A* ratio change (high/low): grid **1.011** (95% CI
  0.994--1.028, p=0.90); road **0.993** (95% CI 0.967--1.020, p=0.21).
- Family check: no significant change except a 4.6% relative reduction on the
  open grid (0.954, p=0.058), where A* takes only 17--133 microseconds.
- Outputs: `results/perf_mode.md`, `results/perf_mode_summary.csv`,
  `figures/perf_mode.png`.
- Caveat: the subset used the current post-freeze build.  The shared search
  loop gained weighted-A* branches after the frozen main run, so a small part of
  the timing movement may be code-version drift rather than the power mode.
  Expansions and distances are unaffected, and the pooled ratio change is about
  1%.

The SNAP input and prepared graph checksums above still match; the subset used
the same `data/roadNet-CA.bin` and `data/roadNet-CA.alt16.bin`.  The frozen
`results/grid.csv` and `results/real.csv` were not modified.  Checksums of the
new raw subset files:

```text
2918b30a56c0fa27c8cae757495625ac92a945b7e118cf53c3fb9aa560e20287  results/perf_mode_high_grid.csv
295aebcd9648f8a428e17b128f42de6366de93657bb0d778058a8a2d8a869939  results/perf_mode_high_real.csv
```
