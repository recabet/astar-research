# Exploratory extensions: what makes A* win or lose?

These analyses were added **after** the frozen main experiment.  They do not
change the main protocol or its data; they are clearly labelled exploratory in
the report.  Full tables are in `results/novelty.md`, with the wider weighted
sweep in `results/weighted_families.md`, the real tie-break in
`results/real_tie_break.md`, and the plateau mechanism in
`results/tie_plateau.md`.  The C++/Python pipeline is reproducible with
`./scripts/run_novelty.sh`.

## 1. Heuristic informativeness

For every grid query we run reverse BFS from the target and compare the
heuristic with the true remaining distance on 5,000 sampled vertices.  We
measure:

- Spearman correlation between `h(v)` and `d(v,t)`;
- normalised mean absolute error `MAE / mean(d)`;
- detour ratio `BFS distance / Manhattan distance`;
- effective branching factor.

The result is a mechanism, not just a table: admissibility is not enough.  The
normalised heuristic error has a pooled Spearman correlation of **−0.81** with
the Dijkstra/A* time ratio; the correlation feature has **+0.78**.  A simple
one-feature model has **R² = 0.55**.  On the California road network with ALT
K=8, the heuristic–distance correlation is **0.98**, normalised MAE **0.155**,
and absolute error correlates **−0.77** with the time ratio.

The maze is the outlier: correlation 0.42, normalised MAE 0.97, and A* is
slower.  This explains the negative result instead of merely reporting it.

## 2. Price of optimality: weighted A* with alternatives

We add weighted A* with `f = g + εh` and sweep
`ε = 1, 1.1, 1.25, 1.5, 2, 3, 5, 10, 20, 50`.  The primary results use a
closed list (a settled vertex is not reopened); reopening is reported as a
sensitivity check.  The sweep covers all five grid families (four sizes,
15 queries each) and the 30 frozen road queries with ALT K=8.  The full tables
are in `results/weighted_families.md`.

The perfect maze remains the control: it has a **unique simple path**, so the
path-cost ratio is exactly **1.00** for every ε and the optimality price is
zero.  High ε can still reduce search time there (1.10x at ε=50), but only
because there is nothing to trade.

On graphs with alternative paths the trade is real, and it depends on the base
heuristic:

- **Random obstacles:** ε=1.1 raises the median speed-up from 12.8x to
  **20.1x** for a **1.8%** median cost increase; ε=1.25 reaches **38.2x** for
  **5.5%**.
- **Rooms:** ε=1.25 reaches **24.8x** for **3.1%**.
- **Clustered obstacles:** ε=1.5 reaches **26.5x** for **5.0%**.
- **Open grid:** Manhattan is exact, so the cost stays 1.00 and expansions do
  not fall; an already-perfect heuristic leaves no room to trade.
- **Road graph:** optimal ALT K=8 is **7.4x** faster than Dijkstra in this
  sweep.  With a closed list, ε=1.1 reaches **8.8x** at **1.4%** median cost.  Reopening the
  same ε collapses to **5.4x**: on a graph where the heuristic is already
  tight, reheapifying settled vertices costs more than the shorter search
  saves.

The practical rule is therefore not "weight more for speed" but "weight only
when the heuristic is weak, keep ε near 1.1-1.25 when path quality matters, and
use a closed list".  The figure is `figures/weighted_pareto.png`.

## 3. Tie-break tail risk

The existing `larger_g` vs `node_id` comparison is a tail-risk result.  On open
grids the median expansion ratio is 1.00, but **45%** of queries are worse with
`node_id`, **37%** are worse by more than 10x, and the worst is **285.7x**.
`larger_g` never expanded more vertices on the open grid.  This is why the
median alone is misleading for tie-breaking.

The frozen main run also lets us test the same question on the real road graph
without new timing data.  Under ALT K=8, `astar_alt8_node_id` expands a median
**1.14x** as many vertices as `astar_alt8` (+13.8%), is worse on **27/30**
queries, and reaches **2.39x** on the worst query (paired Wilcoxon
p = 4.4e-6).  Median wall-clock time is 1.09x (+8.8%), but that difference is
noisy (p = 0.124); the expansion count is the reliable signal.  The real graph
therefore confirms the grid finding: `larger_g` is a justified default, not a
cosmetic choice.  Details are in `results/real_tie_break.md`.

To explain both patterns we then counted **equal-f plateaus**: maximal runs of
settled vertices that share the same f value, which is exactly where the
tie-break rule can redirect the search.  The open grid is one long plateau
(median longest run = 1.00 of the search), so the rule decides the route and the
tail is huge.  The maze is the opposite (median 0.002), so the rule is neutral.
The road graph has intermediate, query-dependent plateaus (median 0.35):
longer plateaus predict a larger `node_id` penalty (expansion rho = 0.53,
p = 0.003; time rho = 0.41, p = 0.025).  The low-plateau third is neutral
(median time ratio 1.02), while the high-plateau third pays 1.25x (worst 2.18x).
The mechanism is reported in `results/tie_plateau.md`.


## 4. Weighted road graph: distance weights

The frozen road result is hop-count only.  To test whether the mechanism
survives weighted edges, we use the DIMACS `USA-road-d.NY` graph: 264,346
nodes, 730,100 undirected weighted arcs after symmetrisation, coordinates, and
integer distance weights.  Weighted Dijkstra supplies the oracle and the ALT
landmark tables; queries are selected in weighted-distance bands.

All 30 queries return the weighted Dijkstra optimum.  The median time ratios are:

| treatment | time ratio | expansion ratio |
|---|---:|---:|
| Euclidean A* | **1.6x** | **1.9x** |
| ALT K=1 | 1.6x | 2.3x |
| ALT K=4 | 10.1x | 14.6x |
| ALT K=8 | **18.4x** | **25.8x** |
| ALT K=16 | **21.6x** | **31.1x** |

Euclidean A* is free but weak: the straight-line bound is much looser than
landmark distances.  ALT K=16 is the fastest treatment, and K=8 still gives a
large gain.  Weighted edges also shrink the tie-break penalty: `node_id` expands
1.08x as many vertices as `larger_g`, versus 1.14x on the unit-hop California
graph, consistent with fewer equal-f plateaus.

This is a separate graph and query set; the absolute numbers are not directly
comparable with the frozen California results.  Details are in
`results/weighted_road.md`.

## 5. Batch throughput with OpenMP

The search itself is sequential, but independent queries are not.  We run the
five 1024x1024 grid families (15 queries each, 3 repeats) and the 30 road
queries (3 repeats) with 1, 2, 4, 8 and 16 OpenMP threads.  Each task still runs
Dijkstra and A* sequentially on the same query, and each worker gets its own
`SearchScratch` and heuristic object.  The frozen query-level results remain
serial.

| threads | batch time | speedup | efficiency | median Dijkstra | median A* |
|---:|---:|---:|---:|---:|---:|
| 1 | 44.39 s | 1.00x | 1.00 | 116.3 ms | 5.24 ms |
| 2 | 21.48 s | 2.07x | 1.03 | 108.4 ms | 5.51 ms |
| 4 | 10.11 s | 4.39x | 1.10 | 92.2 ms | 5.25 ms |
| 8 | 6.27 s | 7.08x | 0.89 | 112.1 ms | 5.50 ms |
| 16 | 4.56 s | **9.73x** | 0.61 | 140.8 ms | 6.17 ms |

The grid workload scales 9.93x and the road workload 9.36x at 16 threads.
Per-query latency inflates only 21% for Dijkstra and 18% for A* at 16 threads;
the median Dijkstra/A* ratio moves from 14.8x to 17.9x.  The maze remains the
exception (0.87x at 16 threads), so the structural conclusion is unchanged
under parallel load.

The sublinear efficiency is expected: the CPU has a mix of performance and
efficiency cores, and the road workload is memory-heavy.  The efficiencies
slightly above one at two and four threads are within frequency/run-to-run
variation, not super-linear scaling.  This is a throughput result, not a claim
that a single A* query becomes faster.

## Files

- `results/grid_diagnostics.csv` — 300 grid queries, heuristic errors and correlations
- `results/real_diagnostics.csv` — 30 road queries with ALT K=8
- `results/weighted_maze.csv` — original maze-only weighted A* sweep
- `results/weighted_families_closed.csv` — closed-list grid sweep (primary)
- `results/weighted_families.csv` — reopening grid sweep (sensitivity)
- `results/weighted_real_closed.csv` — closed-list road sweep (primary)
- `results/weighted_real.csv` — reopening road sweep (sensitivity)
- `results/real_tie_break.md` — real-graph `node_id` vs `larger_g` analysis
- `results/tie_plateau_grid.csv`, `results/tie_plateau_real.csv` — equal-f plateau runs
- `results/tie_plateau.md` — plateau mechanism report
- `results/weighted_road.csv` — weighted DIMACS New York benchmark
- `results/weighted_road.md` — weighted-road report
- `results/omp_scaling.csv` — OpenMP batch times
- `results/omp_tasks.csv` — OpenMP per-query latencies
- `results/novelty.md`, `results/omp.md` — generated tables
- `results/weighted_families.md` — generated weighted-A* report
- `figures/novelty_heuristic_speedup.png`
- `figures/novelty_weighted_maze.png`
- `figures/weighted_pareto.png`
- `figures/real_tie_break.png`
- `figures/tie_plateau.png`
- `figures/weighted_road.png`
- `figures/omp_scaling.png`
- `figures/omp_astar_advantage.png`
