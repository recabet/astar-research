# When does A* save work?

Project T01 for **Advanced Data Structures and Algorithms**, M1 Data Science and
Artificial Intelligence, UFAZ, Fall 2026.

Pair: **Elnur Haqqverdiyev – Rajab Iskandarli**.

This repository contains the C++20 implementation, the correctness tests, the
frozen experimental protocol, the raw measurements and the analysis for the
question:

> How do the structure of a graph and the heuristic change the number of
> expanded vertices and the runtime of A* compared with Dijkstra, on unit-cost
> grids with obstacles and on a real-life road graph?

The short answer is in [`RESULTS.md`](RESULTS.md).  The pre-registered
predictions are in [`HYPOTHESES.md`](HYPOTHESES.md), and the protocol frozen
before the main run is in [`PROTOCOL.md`](PROTOCOL.md).

## Quick start

Requirements: a C++20 compiler with OpenMP (tested with GCC 13.3), CMake 3.20+,
Python 3.10+ with `numpy`, `pandas`, `matplotlib` and `scipy`, and `curl`/`gzip`
for the SNAP data.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
./build/test_core
```

The core tests run without the real graph; the real-graph sample is skipped
until the graph is prepared.  To download, decompress and preprocess the real
graph (the script builds the C++ tools if needed):

```bash
./download_data.sh
```

To rerun the full frozen experiment and the analysis:

```bash
./scripts/run_experiments.sh
```

The exploratory extensions (heuristic informativeness, weighted A*, the
real-graph tie-break, and the equal-f plateau mechanism) are separate from the
frozen protocol:

```bash
./scripts/run_novelty.sh
```

The weighted-distance DIMACS road experiment is separate from the frozen
protocol and downloads its own 21 MB New York graph:

```bash
./scripts/run_weighted_road.sh
```

The OpenMP batch-throughput study is also separate from the frozen protocol:

```bash
./scripts/run_omp.sh
```
`run_experiments.sh` calls `download_data.sh` automatically if the graph is
missing.

The script pins the process to CPU 0 with `taskset`, applies the 4 GiB memory
cap from the protocol, runs a 30-query real-graph validation, then runs the grid
and real benchmarks.  On the development machine the timed part took about
2 min 45 s; the full script including compilation and analysis is under 5 min.

If the binaries and data already exist, the experiment can be run directly:

```bash
./build/astar_research benchmark-grid \
    --out results/grid.csv --sizes 128,256,512,1024 \
    --families open,random,clustered,rooms,maze \
    --reps 3 --per-band 5 --seed 20261030

./build/astar_research benchmark-real \
    --graph data/roadNet-CA.bin \
    --landmarks data/roadNet-CA.alt16.bin \
    --out results/real.csv \
    --reps 5 --per-band 10 --main-k 8 --seed 20261030

python3 scripts/analyze_results.py
./scripts/build_report.sh      # IEEE two-column PDF
```

## Real data

Source: SNAP, **roadNet-CA** (California road network),
<https://snap.stanford.edu/data/roadNet-CA.html>.

The SNAP page reports 1,965,206 nodes and 2,766,607 edges.  We read the
5,533,214 directed arcs, symmetrise each road segment, remove self-loops and
duplicates, take the largest connected component, and compact the vertex ids.
The experimental graph has:

- **1,957,027 vertices**
- **2,760,388 undirected edges** (5,520,776 CSR arcs)
- average degree ≈ 2.82
- unit edge weight (one per road segment / hop)

SNAP does not provide road length or travel time, so we do **not** invent a
weighted interpretation.  The real-graph result is a hop-count result, just as
the grid result is a unit-cost result.

For the weighted-distance extension we use the DIMACS `USA-road-d.NY` graph,
which provides both integer distance weights and coordinates.  After
symmetrisation and deduplication it has **264,346 vertices** and **730,100**
undirected weighted arcs.  `scripts/download_dimacs_ny.sh` downloads the
`.gr`/`.co` pair, builds weighted ALT landmarks with weighted Dijkstra, and
projects the coordinates for a conservative Euclidean lower bound.  This is a
separate data regime, not a replacement for the frozen California results.

The generated data files are intentionally **not** shipped in the archive, so
the project can be zipped without the 278 MB data directory.  `download_data.sh`
recreates them idempotently: `data/roadNet-CA.txt.gz` is the original download,
`data/roadNet-CA.txt` is the decompressed edge list, `data/roadNet-CA.bin` is the
compact CSR graph and `data/roadNet-CA.alt16.bin` contains the 16 landmark
distance tables.  The binaries are generated, not hand-edited.

## Grid families

All grids are square, row-major, 4-neighbour, unit cost.  A blocked cell is an
isolated vertex; only the largest connected component is used for query
selection.

| family | generation |
|---|---|
| `open` | no obstacles |
| `random` | independent blocked cells with probability 0.20 |
| `clustered` | 30% blocked in random circular blobs, radius 3 .. min(W,H)/20, 85% fill inside a blob |
| `rooms` | walls every 16 cells; one door per room-boundary segment; solid wall intersections |
| `maze` | perfect maze from iterative randomised DFS on odd cells |

Sizes 128, 256, 512, 1024.  The actual blocked fraction is reported in the CSV,
because the clustered and room generators do not hit their nominal values
exactly.

## What is implemented

### Graph (`include/graph.hpp`, `src/graph.cpp`)

`Graph` is a compact CSR graph:

- `offsets` of length `n+1`, `targets` and `weights` of length `m`;
- memory `O(n+m)`;
- `degree(u)` O(1), outgoing-edge iteration O(degree(u));
- `GraphBuilder` sorts and deduplicates an edge list in `O(m log m)`;
- a documented binary format (`ASTARGR1`) for reproducible graph loading.

### Binary heap (`include/binary_heap.hpp`)

Array-based binary min-heap with duplicate entries and lazy deletion:

- `push`, `pop`: `O(log n)` worst case;
- `top`, `size`, `empty`, `clear`: `O(1)`;
- no decrease-key: a popped entry is stale if its `g` is no longer the current
  distance, or if the vertex is already closed;
- two deterministic tie-breaks: `NodeId` = `(f,id)`, `LargerG` = `(f,-g,id)`.

### Searches (`include/search.hpp`, `src/search.cpp`)

A single `shortest_path` routine implements both algorithms.  A null heuristic
is Dijkstra; a non-null heuristic is A*.  This guarantees that the heap, edge
relaxation, counter updates and stopping rule are identical.

- Dijkstra: `f=g`, `h=0`.
- A*: `f=g+h(v)`.
- Both stop when the target is popped and settled.
- The heuristic is prepared once per query.
- With a consistent heuristic, settled vertices never need reopening; both
  heuristics used here are consistent.  The optional weighted mode
  `f=g+epsilon*h` can run with reopening or a closed list and is used only in
  the exploratory weighted-A* experiment.
- `SearchScratch` reuses distance/closed/parent arrays and resets them in O(1)
  with a generation stamp.  One scratch object is created per treatment so heap
  capacity cannot leak between treatments.
- Timed runs do not reconstruct paths; validation runs do.

### Heuristics (`include/heuristic.hpp`, `src/heuristic.cpp`)

- **Manhattan** on grids:
  `h(v,t)=|row(v)-row(t)|+|col(v)-col(t)|`.
  It is admissible and consistent for 4-neighbour unit grids, with or without
  obstacles.
- **ALT** on the real graph:
  `h(v,t)=max_L |d(L,v)-d(L,t)|`.
  For each landmark, the triangle inequality gives admissibility, and the same
  inequality on an edge gives consistency.
- **Euclidean** on the weighted road graph: a conservative projected
  straight-line lower bound, with no preprocessing beyond the coordinates.
- Sixteen landmarks are selected by deterministic farthest-point sampling;
  each landmark table is a BFS for unit graphs and a weighted Dijkstra for
  weighted graphs.  The main real treatment uses the first 8; the analysis also
  reports 1, 4 and 16.

### Oracles (`include/oracle.hpp`, `src/oracle.cpp`)

- BFS distances for unweighted graphs, independent of the heap searches;
- weighted Dijkstra distances for weighted graphs;
- Floyd–Warshall for hand-sized weighted graphs;
- edge-by-edge path validation;
- largest-connected-component extraction by union-find.

### Experiments (`include/experiments.hpp`, `src/experiments.cpp`)

- query selection from BFS distance bands for unit graphs, and from weighted
  distance bands for the weighted road graph, independent of Dijkstra/A*;
- paired benchmark runner and raw CSV writer;
- 30-query real validation sample;
- documented counters: expansions, pushes, pops, stale pops, relaxed arcs,
  improvements, heuristic calls and heap peak.

## Counter definitions

See `PROTOCOL.md` section 5.  In short:

- **expanded**: popped, not stale, not already closed, and therefore settled
  (the target counts when popped);
- **pushed**: every heap insertion, including the source and each successful
  decrease;
- **stale_popped**: popped entry whose `g` is not the current tentative distance,
  or whose vertex is already closed;
- **relaxed_edges**: outgoing arcs inspected from expanded vertices;
- **improved**: successful strict distance decreases after source initialisation;
- **heuristic_calls**: calls to `Heuristic::estimate` (zero for Dijkstra);
- **heap_peak**: maximum number of entries in the heap.

## Output layout

```
results/grid.csv                 raw grid runs (2700 rows)
results/real.csv                 raw real-graph runs (900 rows)
results/summary_*.csv            paired summaries and CIs
results/paired_*.csv             per-query paired ratios
results/analysis.md              generated tables and figures
results/RUN_LOG.md               environment, commands, completion status
results/grid_diagnostics.csv      heuristic-error diagnostics, 300 grid queries
results/real_diagnostics.csv      heuristic-error diagnostics, 30 road queries
results/weighted_maze.csv         original maze-only weighted A* sweep
results/weighted_families_closed.csv  weighted A* grid sweep, closed list
results/weighted_families.csv      weighted A* grid sweep, reopening
results/weighted_real_closed.csv   weighted A* road sweep, closed list
results/weighted_real.csv          weighted A* road sweep, reopening
results/weighted_families.md       generated weighted-A* report
results/real_tie_break.md         real-graph tie-break analysis
results/tie_plateau_grid.csv      equal-f plateau runs on grids
results/tie_plateau_real.csv      equal-f plateau runs on the road graph
results/tie_plateau.md            plateau mechanism report
results/weighted_road.csv         weighted DIMACS New York benchmark
results/weighted_road.md          weighted-road report
results/weighted_road_eps.csv     weighted A* epsilon sweep on weighted NY
results/weighted_road_eps.md      weighted-road epsilon report
results/unit_road.csv             same-topology unit-weight control
results/unit_road.md              weighted-versus-unit control report
results/summary_weighted_road*.csv  weighted-road summaries
results/paired_weighted_road*.csv  weighted-road per-query pairs
results/summary_unit_road.csv     weighted/unit comparison
results/omp_scaling.csv           OpenMP batch wall-clock times
results/omp_tasks.csv             OpenMP per-query latencies
results/novelty.md                generated novelty tables
results/omp.md                    generated OpenMP tables
results/perf_mode_high_grid.csv   high-performance-mode grid subset
results/perf_mode_high_real.csv   high-performance-mode road subset
results/perf_mode.md              generated performance-mode comparison
results/perf_mode_summary.csv     performance-mode summary table
figures/novelty_*.png             novelty figures
figures/weighted_pareto.png       weighted-A* cost/speed frontier
figures/real_tie_break.png        real-graph tie-break figure
figures/tie_plateau.png           equal-f plateau mechanism figure
figures/weighted_road.png         weighted-road algorithm comparison
figures/weighted_road_eps.png    weighted-road epsilon frontier
figures/unit_road.png            weighted-versus-unit control
figures/omp_*.png                 OpenMP figures
figures/perf_mode.png             performance-mode summary figure
NOVELTY.md                        summary of the exploratory extensions
download_data.sh                 downloads/prepares SNAP roadNet-CA
scripts/run_novelty.sh           heuristic + weighted-A* extensions
scripts/analyze_weighted_families.py  weighted-A* all-family/road analysis
scripts/analyze_real_tie_break.py     real-graph tie-break analysis
scripts/analyze_tie_plateau.py        equal-f plateau mechanism analysis
scripts/download_dimacs_ny.sh    downloads/prepares the weighted New York graph
scripts/run_weighted_road.sh     weighted DIMACS road experiment
scripts/analyze_weighted_road.py weighted-road analysis
scripts/analyze_weighted_road_eps.py  weighted-road epsilon analysis
scripts/analyze_unit_road.py     weighted-versus-unit control analysis
scripts/render_appendix_tables.py full report appendix tables
scripts/run_omp.sh               OpenMP batch-scaling extension
scripts/check_performance_mode.sh  matched subset run in another power mode
scripts/analyze_perf_mode.py     compares that subset with the frozen main data
figures/*.png                    figures used in the defence slides
HYPOTHESES.md                    predictions written before the main run
PROTOCOL.md                      frozen protocol
RESULTS.md                       answer to the research question
report/report.tex                IEEE two-column report source
report/report.pdf                IEEE two-column report (13 pages)
slides/checkpoint.tex            checkpoint deck source
slides/checkpoint.pdf            rendered checkpoint deck
slides/defence.tex               defence deck source
slides/defence.pdf               rendered defence deck
```

## Reproducibility details

- Main seed: `20261030`.
- Graph seed for a cell: `20261030 ^ fnv1a64("graph_" + graph_id)`.
- Query seed: `20261030 ^ fnv1a64("query_" + graph_id)`.
- Grid query selection: 5 sources x 3 bands = 15 pairs per graph.
- Real query selection: 10 sources x 3 bands = 30 pairs.
- Repeats: 3 for grids, 5 for real.
- Warm-up: one untimed run per treatment per graph.
- Timing: `std::chrono::steady_clock`, nanoseconds, only around
  `shortest_path`; input construction and query selection are outside.
- Time cap: 30 s per search, checked every 4096 pops; no timeouts occurred.
- Memory cap: 4 GiB per process (`ulimit -v 4194304` on Linux).
- Environment used for the reported data: Intel Core Ultra 9 285H, 16 logical
  CPUs, 30 GiB RAM, Ubuntu 24.04.5 on WSL2, kernel 6.6.87.1, GCC 13.3.0,
  CMake 4.4.2, Release build.

## Exploratory extensions

Five post-freeze analyses are included and clearly labelled exploratory:

1. **Heuristic informativeness.** Reverse BFS measures the true remaining
   distance on sampled vertices; the normalised heuristic error has Spearman
   correlation -0.81 with the paired time ratio across 300 grid queries.  This
   explains why the maze loses: Manhattan is admissible but uninformative.
2. **Price of optimality.** Weighted A* (`f = g + epsilon*h`) is swept across
   all five grid families and the road graph, with a closed list as the primary
   policy and reopening as a sensitivity check.  Random obstacles at
   `epsilon=1.1` reach 20.1x for 1.8% median path cost, and rooms at
   `epsilon=1.25` reach 24.8x for 3.1%.  On the road graph, optimal ALT K=8 is
   7.4x faster than Dijkstra in this sweep and the closed-list `epsilon=1.1`
   variant reaches 8.8x for 1.4% cost; reopening collapses to 5.4x.  The perfect maze remains a
   control: unique paths keep the cost ratio at exactly 1.00.
3. **Real-graph tie-break and plateau mechanism.** The frozen run already
   contains `node_id` and `larger_g` at ALT K=8.  `node_id` expands a median
   1.14x as many vertices, is worse on 27/30 queries, and reaches 2.39x on the
   worst query; median time is 1.09x but noisy.  A new pass counts maximal
   equal-f runs: the open grid is one saturated plateau, the maze has almost
   none, and on the road graph longer plateaus predict the penalty
   (expansion rho=0.53, p=0.003).  This explains why tie-breaking is a
   tail-risk effect.
4. **Weighted distance road.**  On DIMACS `USA-road-d.NY` with coordinates and
   integer road-distance weights, weighted ALT K=8 gives 18.4x median time and
   25.8x fewer expansions; K=16 gives 21.6x and 31.1x.  A free Euclidean
   heuristic gives only 1.6x and 1.9x, so ALT still earns its preprocessing
   cost.  Weighted A* with a closed list reaches 35.1x at 1.1% median cost and
   53.9x at 4.1%; reopening is slower.  A same-topology unit-weight control
   raises ALT K=8 from 10.4x to 18.4x when distance weights are used, and cuts
   the `node_id` time penalty from 1.17x to 1.08x.  All 30 queries return the
   exact oracle distance in every run.
5. **Batch throughput with OpenMP.** Independent queries scale to 9.73x at 16
   threads (61% efficiency); per-query latency inflates by 18--21%, and the A*
   advantage remains.  This is a throughput result, not single-query speedup.

See `NOVELTY.md`, `results/novelty.md`, `results/weighted_families.md`,
`results/real_tie_break.md`, `results/tie_plateau.md`,
`results/weighted_road.md`, `results/omp.md`, and the report/talk sections.

## Limitations

- Two real graphs are used: a sparse California hop-count network and a
  weighted New York distance network.  This is still not a sample of all road
  networks, and the weighted result should not be generalized to travel times
  without repeating the experiment.
- The grid families are synthetic controls.  They isolate structure, but they
  are not a sample of real maps.
- The real graph's edge weights are not physical lengths or travel times.
- The timing is single-machine wall-clock time; the analysis reports medians,
  IQRs, bootstrap intervals and within-query repeat variation rather than a
  single number.
- A* is compared with Dijkstra using the same binary heap and lazy deletion;
  the conclusion is about this implementation, not about every possible
  priority queue.
- The real graph is undirected after symmetrisation.  One-way roads are not
  modelled.

## Performance-mode sensitivity

The main run was collected with the host in its lowest performance/power mode.
Absolute times are therefore mode-specific; the paired ratios are the intended
result.  We reran an exactly matched subset with the host in its higher
performance mode on 2026-11-04: grid sizes 128 and 512 (all five families,
150 queries) plus all 30 road queries, with the frozen seeds, bands and repeat
counts.  Every high-mode row is therefore paired with its low-mode counterpart
in `results/grid.csv` and `results/real.csv`.

The higher mode made both algorithms about **2.24x faster** in wall-clock time
(Dijkstra 2.24x, A* 2.23x on grids; 2.26x / 2.23x on the road graph).  The
Dijkstra/A* ratio barely moved:

| workload | queries | Dijkstra speed-up | A* speed-up | ratio change high/low |
|---|---:|---:|---:|---:|
| grid, Manhattan | 150 | 2.24x | 2.23x | 1.011 (95% CI 0.994–1.028, p=0.90) |
| road, ALT K=8 | 30 | 2.26x | 2.23x | 0.993 (95% CI 0.967–1.020, p=0.21) |

Expansion counts and returned distances are **identical** on all 1,350 grid and
900 road paired raw rows; only wall-clock time changes.  The open-grid family is
the only visible deviation (ratio change 0.954, p=0.058), where A* takes only
17–133 µs per query and is most exposed to fixed overheads and cache state.  No
other family changes significantly.  The structural conclusions of the study
therefore hold in both modes.  The subset used the current post-freeze build;
the shared search loop gained weighted-A* branches after the main run, so a
small part of the timing movement may be code-version drift.  Expansion counts
and distances are unaffected, and the pooled ratio change is about 1%.

The check can be repeated without overwriting the frozen main CSVs:

```bash
./scripts/check_performance_mode.sh high
python3 scripts/analyze_perf_mode.py high
```

`check_performance_mode.sh` uses the frozen per-band and repeat settings, so a
later low-mode subset can also be compared directly with
`scripts/compare_performance_modes.py low high`.  The generated comparison is in
`results/perf_mode.md`; the main experiment remains the frozen
`results/grid.csv` and `results/real.csv`.
