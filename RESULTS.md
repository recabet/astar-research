# When does A* save work? — results

This is the answer to the T01 research question, using the frozen protocol in
`PROTOCOL.md` and the raw data in `results/grid.csv` and `results/real.csv`.
Every number below is a **paired** comparison: Dijkstra and A* ran on exactly
the same graph, source, target and repeat.  Ratios are
`Dijkstra / A*`, so a value above 1 means A* is better.

## Bottom line

A* is worth using when the heuristic is informative about the **actual**
remaining path, not merely about the geometry:

- On an open grid, Manhattan is exact and A* is dramatically better: median
  **105.7x faster** and **222x fewer expansions** (up to 390x / 696x at
  1024x1024).
- With random obstacles, clustered obstacles or rooms, A* still wins clearly,
  but the advantage falls to **8.7x–12.8x time** and **14.3x–22.9x
  expansions**.
- In a perfect maze, Manhattan sees the straight-line distance but not the
  forced turns.  A* expands essentially the same number of vertices (Dijkstra/
  A* expansion ratio 1.02, i.e. about **2% fewer** for A*) and is **0.81x as
  fast** (about **24% slower**) than Dijkstra.  This is a genuine negative
  result.
- On the California road network, ALT with 8 landmarks is **7.6x faster** and
  expands **11.5x fewer** vertices.  With 16 landmarks it is **13.5x faster**
  and expands **22.4x fewer**.  With one landmark the expansion saving is real
  (**2.0x**) but the time saving is not statistically significant
  (**1.29x**, Wilcoxon p = 0.13).

The time saving is always smaller than the expansion saving because A* pays
more per expansion: its measured cost is about **1.6x–2.0x** Dijkstra's per
settled vertex, due to heuristic evaluation and larger heap entries.  In the
maze there is no expansion saving to pay for that extra work, so A* loses.

## Data and correctness

- Real graph: SNAP `roadNet-CA`, prepared to **1,957,027 vertices** and
  **2,760,388 undirected edges** (5,520,776 CSR arcs), unit hop cost.
- Grids: 4 sizes (128, 256, 512, 1024) x 5 families (open, random 20%,
  clustered 30%, rooms, maze).
- Grid experiment: 20 graphs x 15 queries x 3 repeats x 3 treatments =
  **2,700 timed runs**.
- Real experiment: 30 queries x 5 repeats x 6 treatments = **900 timed runs**.
- **3,600/3,600 runs** returned `status=ok` and matched the independent BFS
  distance oracle.  No timeouts, no discarded runs.
- Before timing, 30 real queries were also checked with edge-by-edge path
  validation, and the core test suite checked a hand-computed 5x5 grid, a
  hand-computed weighted graph against Floyd–Warshall, all five grid families
  against BFS, and ALT admissibility/consistency.
- Expansions were identical across repeated runs (as expected for a
  deterministic search).  Median within-query time variation was **7.2%**
  for grid Dijkstra, **10.1%** for grid A*, **7.4%** for real Dijkstra and
  **9.6%** for real A* (K=8).  The reported treatment differences are much
  larger than this variation, except for real ALT K=1, which is explicitly
  reported as not significant.

## Grids

### Main treatment: Manhattan, `larger_g` tie-break

| family | time ratio (median) | time IQR | expansion ratio (median) | expansion IQR | time saving | expansion saving | queries where A* faster |
|---|---:|---:|---:|---:|---:|---:|---:|
| open | **105.7x** | 54.9–249.3 | **222.4x** | 105.1–490.3 | 99.1% | 99.6% | 100% |
| random 20% | **12.8x** | 7.2–21.9 | **21.4x** | 11.3–31.7 | 92.2% | 95.3% | 98% |
| clustered 30% | **12.8x** | 7.0–34.6 | **22.9x** | 11.7–67.8 | 92.2% | 95.6% | 98% |
| rooms | **8.7x** | 5.6–17.0 | **14.3x** | 8.4–25.6 | 88.5% | 93.0% | 100% |
| maze | **0.81x** | 0.75–0.94 | **1.02x** | 1.00–1.05 | **−24.0%** | 1.9% | 17% |

Wilcoxon signed-rank p-values are below 1e-4 for every family, including the
maze (the maze difference is a real slowdown, not noise).

### Size scaling

The open-grid advantage grows with size because the A* corridor is a vanishing
fraction of Dijkstra's Manhattan ball:

| size | open: time / expansions | random: time / expansions | rooms: time / expansions | maze: time / expansions |
|---:|---:|---:|---:|---:|
| 128 | 45.5x / 94.4x | 12.0x / 19.7x | 7.6x / 10.8x | 0.75x / 1.05x |
| 256 | 76.0x / 172.5x | 11.3x / 21.9x | 8.7x / 16.7x | 0.77x / 1.02x |
| 512 | 177.4x / 363.9x | 12.7x / 14.6x | 11.4x / 20.5x | 0.86x / 1.01x |
| 1024 | 389.9x / 696.3x | 21.0x / 27.5x | 8.7x / 10.9x | 0.97x / 1.01x |

The maze slowdown shrinks at 1024 because both algorithms are doing a large
amount of work and fixed overheads matter less, but A* never becomes faster in
the median.

### Distance bands

| family | short: time / expansions | medium: time / expansions | long: time / expansions |
|---|---:|---:|---:|
| open | 83.5x / 150.0x | 149.9x / 297.9x | 156.0x / 294.9x |
| random | 14.4x / 22.2x | 12.8x / 20.1x | 11.5x / 21.6x |
| clustered | 11.9x / 20.9x | 14.3x / 24.3x | 12.0x / 22.8x |
| rooms | 10.3x / 11.9x | 6.9x / 10.6x | 9.5x / 20.8x |
| maze | 0.82x / 1.05x | 0.75x / 1.01x | 0.84x / 1.00x |

The open-grid prediction (long queries benefit more) is confirmed.  The random
grid does **not** follow it: its long-query ratio is slightly lower than its
short-query ratio.  Structure, not distance alone, controls the benefit.

## Real road network with ALT

| treatment | time ratio | time IQR | expansion ratio | expansion IQR | time saving | expansion saving | queries faster |
|---|---:|---:|---:|---:|---:|---:|---:|
| ALT K=1 | 1.29x | 0.93–1.75 | 1.96x | 1.44–2.44 | 22.5% | 48.9% | 70% |
| ALT K=4 | 4.75x | 2.38–7.71 | 7.26x | 3.76–10.86 | 78.9% | 86.2% | 100% |
| ALT K=8 (main) | **7.62x** | 4.38–16.65 | **11.54x** | 7.06–25.66 | 86.8% | 91.3% | 100% |
| ALT K=16 | **13.52x** | 7.64–25.25 | **22.41x** | 12.94–38.70 | 92.6% | 95.5% | 100% |

The K=1 time difference is not significant (Wilcoxon p = 0.13); its expansion
difference is significant (p < 1e-8).  K=4, K=8 and K=16 are significant for
both time and expansions (p < 1e-8).  K=16 is not a diminishing-return plateau:
paired with K=8 on the same 30 queries, K=16 uses about **21% less time** and
**25% fewer expansions** (paired medians; p < 1e-4).  On this road graph, more
landmarks keep tightening the bound faster than their per-expansion cost grows.

By distance band (K=8):

| band | time ratio | expansion ratio |
|---|---:|---:|
| short | 14.9x | 19.1x |
| medium | 9.4x | 13.5x |
| long | 4.3x | 7.5x |

Absolute work still grows with distance: median long-query A* expands 247,065
vertices and takes 99 ms, versus 448 ms for Dijkstra.  The ratio is smaller for
long queries because a long road route has more opportunities for the landmark
bound to be loose.

## Why time saving is smaller than expansion saving

A* does more work per settled vertex.  The medians across all grid families:

| | Dijkstra ns/expansion | A* ns/expansion | ratio |
|---|---:|---:|---:|
| open | 142.2 | 288.3 | 2.03x |
| random | 156.5 | 254.1 | 1.62x |
| clustered | 126.7 | 245.0 | 1.93x |
| rooms | 146.0 | 270.4 | 1.85x |
| maze | 64.9 | 78.8 | 1.21x |

On the real graph, Dijkstra costs 234.5 ns per expansion, ALT K=8 costs 372.9 ns
(1.59x) and ALT K=16 costs 393.7 ns (1.68x).  The extra cost comes from
evaluating the heuristic at every push, maintaining a larger heap key, and
extra memory traffic.  Therefore:

- open grid: expansion ratio 222x, time ratio 106x;
- random: 21.4x -> 12.8x;
- maze: expansion ratio 1.02x, time ratio 0.81x (the extra cost has no
  compensating reduction in expansions).

This is the central answer: **A* saves time only when the reduction in expanded
vertices is large enough to pay for the more expensive expansion.**

## Tie-breaking

`larger_g` (prefer the larger `g` among equal `f`) versus `node_id`:

| family | median expansion ratio node_id / larger_g | queries where node_id is worse | worst node_id expansion ratio |
|---|---:|---:|---:|
| open | 1.00 | 45% | 285.7x |
| random | 1.10 | 63% | 51.5x |
| clustered | 1.17 | 68% | 92.6x |
| rooms | 1.02 | 58% | 17.8x |
| maze | 1.00 | 2% | 1.01x |
| road (ALT K=8) | 1.14 | 90% | 2.39x |

The median on the open grid is 1.00 because some targets happen to be popped
before the tie-break can matter.  But on the queries where it does matter,
`node_id` can expand hundreds of times more vertices.  `larger_g` never expanded
more vertices than `node_id` on the open and clustered grids; on the random
grids the worst regression was 0.8%.  Tie-breaking is therefore a **tail-risk**
decision, not a universal median effect.  It does not change any returned
distance.

On the real road graph with ALT K=8, the frozen main run contains both
treatments on the same 30 queries.  `node_id` expands a median **1.14x** as many
vertices (+13.8%), is worse on **27/30** queries, and reaches **2.39x** on the
worst query (paired Wilcoxon p=4.4e-6).  Median wall-clock time is 1.09x
(+8.8%), but that difference is noisy (p=0.124).  The expansion count is the
reliable signal, and it confirms that `larger_g` is the right default.  Details:
`results/real_tie_break.md` and `figures/real_tie_break.png`.

### Mechanism: equal-f plateaus
A tie-break can only matter where several settled vertices share the same f
value.  We reran both rules and counted maximal equal-f runs.  On the open grid
the whole search is effectively one plateau (median longest run = **1.00** of
all expansions), so `node_id` can redirect the route and create the 285.7x tail.
On the maze there is almost no plateau (median **0.002**), and the ratio is
neutral.  The road graph is intermediate and variable: its median longest run
is **0.35**, and longer plateaus predict a larger `node_id` penalty
(expansion rho=0.53, p=0.003; time rho=0.41, p=0.025).  The low-plateau third is
neutral (median time ratio 1.02), while the high-plateau third pays 1.25x
(worst 2.18x).  This explains why tie-breaking is a tail-risk effect rather than
a median effect.  Details: `results/tie_plateau.md` and
`figures/tie_plateau.png`.

## Hypothesis verdicts

| hypothesis | verdict | evidence |
|---|---|---|
| H1 open grid | **confirmed** | 222x fewer expansions, 106x faster; 100% of queries |
| H2 random obstacles | **confirmed** | 21.4x fewer expansions, 12.8x faster; 98% of queries |
| H3 clustered/rooms | **confirmed** | both weaker than open; rooms 14.3x expansions vs random 21.4x |
| H4 maze | **confirmed** | expansion ratio 1.02, time ratio 0.81 (24% slower), only 17% faster |
| H5 distance band | **partly rejected** | open long > short, but random long is slightly lower than short |
| H6 ALT | **partly rejected** | K=8 strong; K=1 time not significant; K=16 still better than K=8, so no plateau at 8 |
| H7 tie-break | **rejected as a median claim** | grid median 1.00 but a 45% tail up to 286x; road median 1.14x, 27/30 worse; `larger_g` is a safe default |

## Exploratory extensions

These were added after the frozen main run and are labelled exploratory.

### What makes a heuristic informative?
For 300 grid queries, reverse BFS gives the true remaining distance on 5,000
sampled vertices.  The normalised heuristic error has a pooled Spearman
correlation of **−0.81** with the paired time ratio; the heuristic-distance
rank correlation has **+0.78**.  A one-feature least-squares model has
**R² = 0.55**.  On the road graph with ALT K=8, the heuristic-distance
correlation is **0.98**, normalised MAE **0.155**, and absolute error correlates
**−0.77** with the time ratio.  The maze is the outlier: correlation 0.42,
normalised MAE 0.97, and A* is slower.  This explains the negative result:
Manhattan is admissible but uninformative.

### Price of optimality: weighted A*
Weighted A* uses `f = g + εh`.  The primary sweep uses the practical closed list
(a settled vertex is not reopened); reopening is a sensitivity check.  It covers
all five grid families (four sizes, 15 queries each) and the 30 frozen road
queries with ALT K=8.  Full tables are in `results/weighted_families.md`.

On a perfect maze the simple path is unique, so every returned path has the same
cost: the measured path-cost ratio is exactly **1.00** for all tested ε.  The
optimality price is therefore zero on this control; at ε=50 the search is still
1.10x faster than Dijkstra, but only because there is nothing to trade.

On graphs with alternatives, the first few percent of path cost buy a lot of
search when the heuristic is weak:

- random obstacles: ε=1.1 raises the median speed-up from 12.8x to **20.1x**
  for **1.8%** median cost; ε=1.25 reaches **38.2x** for **5.5%**;
- rooms: ε=1.25 reaches **24.8x** for **3.1%**;
- clustered obstacles: ε=1.5 reaches **26.5x** for **5.0%**;
- open grid: Manhattan is exact, so cost stays 1.00 and expansions do not fall;
- road graph: optimal ALT K=8 is **7.4x** faster than Dijkstra in this sweep,
  and the closed-list ε=1.1 variant reaches **8.8x** at **1.4%** median cost.

The road graph also exposes the policy choice.  Reopening at ε=1.1 collapses to
**5.4x**: where the heuristic is already tight, reheapifying settled vertices
costs more than the shorter search saves.  Weighting is therefore useful when
the heuristic is weak, not as a universal speed switch, and the closed list is
the better default for this implementation.  See
`figures/weighted_pareto.png`.

### Tie-break tail
On open grids, `node_id` is worse than `larger_g` on **45%** of queries, worse
by more than 10x on **37%**, and worst by **285.7x**.  The median ratio is 1.00,
which is exactly why the median alone is misleading.  On the real road graph,
`node_id` expands a median **1.14x** as many vertices, is worse on **27/30**
queries, and reaches **2.39x** on the worst query (p=4.4e-6); the time
difference points the same way but is noisy.  The real data therefore confirm
the grid conclusion.

### Weighted road graph: distance weights
The frozen California graph is unit-hop, so we add a separate DIMACS
`USA-road-d.NY` experiment: 264,346 nodes, 730,100 undirected weighted arcs,
coordinates, and integer distance weights.  The oracle, ALT landmarks, and query
bands all use weighted Dijkstra.  All 30 queries return the weighted optimum.

| treatment | time ratio | expansion ratio |
|---|---:|---:|
| Euclidean A* | 1.6x | 1.9x |
| ALT K=1 | 1.6x | 2.3x |
| ALT K=4 | 10.1x | 14.6x |
| ALT K=8 | **18.4x** | **25.8x** |
| ALT K=16 | **21.6x** | **31.1x** |

A free Euclidean heuristic is admissible but much weaker than ALT.  Weighted
edges also reduce the tie-break penalty: `node_id` expands 1.08x as many
vertices as `larger_g`, versus 1.14x on the unit-hop graph.  This is a different
graph and query set; absolute speedups are not directly comparable with the
frozen California results.  Details: `results/weighted_road.md`.

### Batch throughput with OpenMP
Independent queries are parallelised across 1, 2, 4, 8 and 16 threads.  Each
task still runs Dijkstra and A* on the same query with thread-local scratch and
heuristic state.  Batch time falls from **44.39 s** at 1 thread to **4.56 s** at
16 threads: **9.73x** throughput (grid 9.93x, road 9.36x, efficiency 0.61).
Median per-query latency inflates by 21% for Dijkstra and 18% for A*; the
Dijkstra/A* ratio rises from 14.8x to 17.9x, and the maze remains below parity.
This is a throughput result, not a single-query speedup; the frozen query-level
results remain serial.

### Host performance mode (robustness)
The frozen main run was made with the host in its lowest power mode.  We reran
the exact same grid queries at sizes 128 and 512 (150 queries) and all 30 road
queries with the host in its higher performance mode.  Because seeds, bands and
repeats were unchanged, every rerun pairs with its frozen counterpart.

The higher mode made both algorithms about **2.24x faster** (grid Dijkstra
2.24x, grid A* 2.23x; road Dijkstra 2.26x, road A* 2.23x).  The A* advantage
did not move: the high/low ratio change is **1.011** on grids (95% CI
0.994–1.028, p=0.90) and **0.993** on the road graph (95% CI 0.967–1.020,
p=0.21).  Expansion counts and distances are **identical** in the two modes on
all 2,250 paired raw rows.  The only visible family-level deviation is the open
grid (ratio change 0.954, p=0.058), where A* runs in tens of microseconds and is
therefore most sensitive to fixed overheads and cache state.  The reported
conclusions are not artefacts of the host power mode.

## Threats to validity and limitations

- The real graph is one sparse road network with **unit hop weights**.  SNAP
  does not provide road lengths or travel times; these results should not be
  read as a claim about weighted travel-time routing.
- The grid families are synthetic.  They are useful controls because they vary
  one structural property at a time, but they are not a random sample of maps.
- The real graph is symmetrised (one-way roads are not modelled).
- Only one machine and one implementation were used.  The analysis reports
  medians, IQRs, bootstrap intervals, paired tests and repeat variation; it does
  not claim hardware-independent constants.
- A* uses the same binary heap and lazy deletion as Dijkstra.  A different
  priority queue or a decrease-key implementation could change constant
  factors.
- Query sources are random within the largest component.  Different source
  selection could change the mix of short/medium/long pairs, although the
  paired design removes algorithm-independent selection effects.
- The open-grid advantage is partly a tie-breaking effect: with `node_id`,
  open-grid A* still expands 54x fewer vertices than Dijkstra, but with
  `larger_g` it expands 222x fewer.  The main treatment is fixed in the
  protocol; the tie-break comparison is reported separately.

## Final answer

A* is worth using when the heuristic captures the structure of the remaining
path.  On open grids it is spectacularly better.  On random or clustered
obstacles and room layouts it is still clearly better, though the time gain is
roughly half the expansion gain.  In a maze it is not worth using: it adds
heuristic work without reducing the search, and is slower.  On the California
road network, a landmark heuristic makes A* strongly worthwhile, and more
landmarks help up to the tested 16.  The practical rule is therefore not
“A* is faster than Dijkstra”, but **“A* is faster when the heuristic is
informative enough that the expansion reduction exceeds the per-expansion
overhead.”**  A matched rerun in the host's higher performance mode made both
algorithms about 2.24x faster and left this ranking unchanged.
