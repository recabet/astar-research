# Frozen experimental protocol

Project T01: **When does A* save work?**
M1 Data Science and Artificial Intelligence, UFAZ, Advanced Data Structures and
Algorithms, Fall 2026.

Status: **frozen before the main run**.  The pilot logs in `results/pilot_*.csv`
are design checks and are not used in the final analysis.  If a change becomes
unavoidable after this point, it will be recorded in `DEVIATIONS.md` and the
reason will be reported.

## 1. Research question

How do the structure of a graph and the strength of a heuristic change the
number of expanded vertices and the runtime of A* compared with Dijkstra, on
unit-cost grids with obstacles and on a real-life road graph?

The answer must separate **expansions** from **wall-clock time**, because A*
pays a per-expansion cost that Dijkstra does not.

## 2. Data

### 2.1 Real graph

- Source: Stanford Network Analysis Project (SNAP), `roadNet-CA`
  (California road network), <https://snap.stanford.edu/data/roadNet-CA.html>.
- Download: `download_data.sh` obtains the 18 MB gzip file.  The
  decompressed edge list is 84 MB.
- SNAP description: 1,965,206 nodes and 2,766,607 edges (largest weakly
  connected component 1,957,027 nodes, 99.6%).
- Preparation (`preprocess-real`):
  1. read all arcs;
  2. symmetrise each road segment (road query is undirected), remove self-loops
     and duplicate `(u,v)` pairs;
  3. take the largest connected component;
  4. compact vertex ids and store a CSR graph.
- Resulting experimental graph: **1,957,027 vertices** and **2,760,388
  undirected edges** (5,520,776 directed CSR arcs).  Average degree ≈ 2.82.
- Edge weight: **1 per road segment** (hop count).  SNAP does not provide road
  lengths or travel times, and we do not invent them.  This is a limitation for
  interpreting the result as a travel-time query, but it is a valid unit-cost
  graph and matches the unit-cost grid experiments.
- Preprocessing is outside all timed sections.

### 2.2 Grids

A grid is a `W x H` row-major array of cells.  A blocked cell is an isolated
vertex in the CSR graph; a free cell has unit arcs to its free 4-neighbours.
Thus graph vertex ids are cell ids and Manhattan distance is available without
extra lookup tables.  Only the largest 4-connected component is used for query
selection.

Five synthetic families isolate different structural effects:

| family | generator |
|---|---|
| `open` | no blocked cells |
| `random` | independent blocking with probability 0.20 |
| `clustered` | 30% blocked in random circular blobs (radius 3 .. min(W,H)/20), 85% fill inside a blob |
| `rooms` | walls every 16 cells, with one door per room-boundary segment; solid wall intersections |
| `maze` | perfect maze from iterative randomised depth-first search on odd cells |

Sizes: 128, 256, 512 and 1024 (square).  This gives 4096, 16384, 65536,
262144 and 1,048,576 cells.

The actual blocked fraction is reported in the CSV (`obstacle_density`), because
the clustered and room generators do not hit their nominal density exactly.

## 3. Algorithms and interfaces

All code is C++20, compiled with `-O3 -DNDEBUG` by CMake Release.  No external
graph, heap or search library is used.

### 3.1 Graph

`astar::Graph` is CSR:

- `offsets`: `n+1` 64-bit indices, `O(n)` memory;
- `targets` and `weights`: `O(m)` memory;
- `degree(u)`, iteration over outgoing arcs: `O(1)` / `O(degree(u))`.

`GraphBuilder` sorts and deduplicates an edge list in `O(m log m)`.

### 3.2 Binary heap

`astar::BinaryHeap` is an array-based binary min-heap with duplicate entries:

- `push`: `O(log n)` worst case;
- `pop`: `O(log n)` worst case;
- `top`, `size`, `empty`: `O(1)`;
- lazy deletion: no decrease-key; the search discards a popped entry whose
  `g` is no longer the current tentative distance.
- tie-break policies: `NodeId` = `(f, id)`; `LargerG` = `(f, -g, id)`.

### 3.3 Search

`shortest_path(graph, s, t, heuristic, options, scratch)` is the single
implementation used for both treatments.  A null heuristic is Dijkstra; a
non-null heuristic is A*.  This makes the heap, CSR traversal, relaxation and
counter code identical.  `scratch` is created per treatment and reused; its
`reset` is O(1) via a generation stamp, not an O(n) clear.

- Dijkstra: `f = g`, `h = 0`.
- A*: `f = g + h(v)`.
- Both stop when the target is popped and settled.
- Duplicate heap entries are stale when their `g` differs from the current
  distance, or when the vertex is already closed.
- With a consistent heuristic, a settled vertex never needs reopening; both
  heuristics used here are consistent.
- Path reconstruction is disabled in timed runs and enabled only for the
  correctness validation.

### 3.4 Heuristics

- Grids: Manhattan distance
  `h(v,t) = |row(v)-row(t)| + |col(v)-col(t)|`.
  For a 4-neighbour unit grid this is admissible and consistent, with or
  without obstacles.
- Real graph: ALT.  For landmarks `L` with exact distances `d(L,v)`:
  `h(v,t) = max_L |d(L,v) - d(L,t)|`.
  For each `L`, the triangle inequality gives
  `|d(L,v)-d(L,t)| <= d(v,t)`, so `h` is admissible; the same inequality on an
  edge gives `|h(u)-h(v)| <= w(u,v)`, so it is consistent.  Accessibility is
  restricted to the largest connected component.
- Landmark selection: deterministic first landmark, then farthest-point
  sampling: each next landmark maximises the minimum BFS distance to the
  already selected landmarks.  Sixteen landmarks are precomputed; the main
  treatment uses the first 8.  Landmark BFS is `O((k+1)(n+m))`, outside timing.

## 4. Query selection

Queries are selected only from the largest connected component, using BFS, and
independently of Dijkstra/A*.

For each selected source, BFS gives the eccentricity `dmax`.  Three bands of
BFS distance are used:

| band | fraction of `dmax` |
|---|---|
| short | [0.08, 0.25] |
| medium | [0.40, 0.60] |
| long | [0.80, 0.97] |

One target is sampled uniformly from the reachable vertices in each band.  If a
band is empty (very small graph), the closest reachable distance to the band
centre is used.  Source and target are always distinct and connected.  Pairs
with no path are excluded from the main performance table and are covered by a
separate correctness test; they are not silently counted as fast runs.

- Synthetic grids: 5 sources per graph, hence 5 pairs per band and 15 pairs per
  graph.  Seed: `20261030 ^ fnv1a64("query_" + graph_id)`.
- Real graph: 10 sources, hence 10 pairs per band and 30 pairs.

The same query list is used for every algorithm and every repeat.

## 5. Counters (exact definitions)

| counter | definition |
|---|---|
| `expanded` | popped entries that are not stale and whose vertex was not already closed; the target counts when it is popped |
| `pushed` | heap insertions, including the source and every successful distance decrease |
| `popped` | all heap removals, including stale entries |
| `stale_popped` | popped entries whose `g` differs from the current tentative distance, or whose vertex is already closed |
| `relaxed_edges` | outgoing arcs inspected from expanded vertices, including arcs to already-closed vertices |
| `improved` | successful strict decreases of a tentative distance after source initialisation |
| `heuristic_calls` | calls to `Heuristic::estimate` during the search; zero for Dijkstra |
| `heap_peak` | maximum number of entries in the heap at any time |
| `time_ns` | wall-clock time around the `shortest_path` call, nanoseconds, measured outside the search |
| `correct` | 1 iff the returned distance equals the BFS oracle distance (or both report no path) |

Identities checked by tests: `popped = expanded + stale_popped`;
`pushed = improved + 1` when the target is found and no timeout occurs;
for A*, `heuristic_calls = pushed`.

## 6. Correctness before timing

`./build/test_core` must pass before the main run.  It contains:

1. binary-heap order test against `std::priority_queue` on 5000 random entries;
2. hand-computed weighted six-vertex graph checked against Floyd–Warshall;
3. hand-checked 5x5 grid with a wall and a unique-length detour, checked by
   BFS; A* and Dijkstra must return distance 8;
4. no-path grid, source==target, blocked/isolated vertices;
5. all five grid families at 32x32, checked against BFS for every selected
   query, with path validation and counter identities;
6. ALT admissibility and consistency on a 9x9 obstacle grid for every landmark
   and every free vertex;
7. tie-break distance equivalence;
8. a sample of real-graph queries checked against BFS, with both paths
   validated edge by edge.

The benchmark itself aborts if a Dijkstra or A* distance differs from the BFS
oracle.  Timeouts are recorded as `status=timeout` and are never discarded.

## 7. Timing protocol

- One untimed warm-up run per treatment on the first query of each graph.
- Repeated timed runs: **3 repeats** per grid query, **5 repeats** per real
  query.
- The median of the repeats is the query-level summary; raw repeats are kept.
- Dijkstra and A* are paired on the same query.  Their order alternates with
  the repeat number; the real-graph variant order is rotated each repeat.
- Inputs (grid construction, BFS query selection, landmark tables, graph
  loading) are outside the timed region.
- The timed region contains only `shortest_path`, including scratch reset,
  target preparation, heap operations and (for A*) heuristic evaluations.
- Time limit: 30 s per search call.  The search checks the clock every 4096
  popped entries and returns `timeout` rather than being killed silently.
- Memory limit: the run script applies `ulimit -v 4194304` (4 GiB) per process.
- The process is pinned to one CPU with `taskset -c 0` to reduce migration
  noise.
- Machine: Intel Core Ultra 9 285H, 16 logical CPUs, 30 GiB RAM, Ubuntu 24.04
  on WSL2, kernel 6.6.87.1, GCC 13.3.0, CMake 4.4.2.
- Power mode: the main run was collected with the host in its lowest
  performance/power mode (operator-reported).  Treat absolute times as
  mode-specific; the paired ratios are the primary result.  A subset
  sensitivity check is provided in `scripts/check_performance_mode.sh`.

## 8. Experiment matrix and budget

| experiment | graphs | pairs/graph | repeats | treatments | timed runs |
|---|---:|---:|---:|---|---:|
| grid | 20 (5 families x 4 sizes) | 15 | 3 | Dijkstra, A* larger_g, A* node_id | 2,700 |
| real | 1 | 30 | 5 | Dijkstra, ALT K=1,4,8,16, ALT K=8 node_id | 900 |

Warm-ups add 60 + 6 runs.  Pilot runs (1 repeat, 2-3 pairs/band) were used to
check the budget; the main run is the full matrix above.

## 9. Statistical analysis

- Pair each A* run with the Dijkstra run on the same `(graph, pair, repeat)`;
  take the median over repeats for each treatment before forming ratios.
- Report **median** and **interquartile range** of per-query ratios, not a
  single aggregate number.
- Report the fraction of queries on which A* wins.
- 95% confidence intervals for the median ratio use 10,000 paired bootstrap
  resamples over query instances.
- Paired Wilcoxon signed-rank tests report whether the per-query time and
  expansion differences are distinguishable from zero; report the p-value and
  the number of pairs.  A difference smaller than the between-run spread is
  not treated as a result.
- Grid results are reported by family, size and band, and pooled by family;
  real results by band and by K.
- Raw CSV files, analysis script, figures and the generated tables are kept in
  `results/`.

## 10. Reproducibility

- All random seeds are fixed: `20261030` for the main run; graph/query seeds are
  derived with FNV-1a from stable graph ids.
- The SNAP edge list is identified by URL and size; the preprocessing binary
  format is documented in `README.md`.
- Commands to rebuild and rerun are in `README.md` and
  `scripts/run_experiments.sh`.
- The full raw CSV, not just the summaries, is a deliverable.
