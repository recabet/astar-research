# Pre-registered hypotheses (written before the main run)

Project T01: **When does A* save work?**
Pair: Elnur Haqqverdiyev – Rajab Iskandarli.

The pilot phase (small grids and a six-query real-graph smoke test) was used only
to check that the implementation, query selection and time budget work.  The
main experiment below was frozen before its data were collected.  A hypothesis
is a prediction that the main data can reject; the pilot is not used as evidence
for or against it.

## H1 — Open grids

On an obstacle-free 4-neighbour unit grid with the Manhattan heuristic, A*
expands substantially fewer vertices than Dijkstra.  With the `larger_g`
tie-break it should follow one shortest-path corridor rather than the whole
Manhattan diamond, so the expansion saving should be large.  Runtime should also
fall, but by a smaller factor than expansions, because every A* expansion pays
for a heuristic evaluation and the heap entries carry a larger key.

**Reject if:** the median paired expansion ratio Dijkstra/A* is below 2 on the
open family, or the median time ratio is not positive.

## H2 — Random obstacles

With independent random obstacles at 20% density, Manhattan remains admissible
and consistent, so A* must still return shortest paths.  The heuristic becomes
less informative near obstacle clusters: detours are not visible to Manhattan.
I expect A* to keep a clear expansion advantage over Dijkstra, but a smaller one
than on the open grid, and a time advantage that is smaller again because the
constant cost per expansion is higher for A*.

**Reject if:** A* expands more vertices than Dijkstra on the median random-grid
query, or the median time ratio is below 1.

## H3 — Clustered obstacles and rooms

When obstacles form clusters or room walls, shortest paths contain forced
detours that Manhattan does not anticipate.  I expect both clustered and room
grids to save fewer expansions than the open grid, and rooms to show a weaker
time saving than random obstacles because room walls create long detours.  A*
is still expected to be no worse than Dijkstra in distance, because the
heuristic remains admissible.

**Reject if:** rooms do not save fewer expansions than random at the same size,
or if A* ever returns a distance different from the BFS oracle.

## H4 — Maze

A perfect maze has long, narrow corridors and a single route between most pairs.
Manhattan gives almost no information about the next forced turn.  I predict
that A* will expand approximately as many vertices as Dijkstra (ratio near 1)
and will be *slower* in wall-clock time because it evaluates the heuristic
without reducing the search.  This is the expected negative result of the
study.

**Reject if:** the median maze expansion ratio is above 2 or the median maze
time ratio is above 1.

## H5 — Distance band

For a fixed family, the absolute number of expanded vertices grows with the
source–target distance.  I expect the relative expansion saving to be larger
for long queries than for short queries on open and random grids, because a
long A* corridor is a smaller fraction of a large Dijkstra ball.  In the maze
and rooms families I expect the band effect to be weak.

**Reject if:** the median long-query expansion ratio is not larger than the
median short-query ratio on open or random grids.

## H6 — Real road network with ALT

The real graph is the California road network.  With exact landmark distances,
the ALT heuristic is admissible and consistent.  I expect A* to reduce both
expansions and time relative to Dijkstra on medium and long queries.  With one
landmark the heuristic is weak and the extra work may make A* no faster than
Dijkstra; with 4–8 landmarks the saving should be large; with 16 landmarks the
additional memory and per-expansion work should give diminishing returns.  The
real graph is sparse and road-like, so it is the case where A* is expected to be
most useful in practice.

**Reject if:** A* with 8 landmarks does not reduce median expansions on the real
graph, or its median time ratio is below 1.

## H7 — Tie-breaking

The `larger_g` rule should reduce expansions relative to `node_id` on open
grids, where many vertices share the same f-value.  On maze and clustered
graphs the f-values are more distinct, so the tie-break should matter less.
This is a controlled secondary comparison; it does not change the primary
Dijkstra-vs-A* treatment.

**Reject if:** `larger_g` does not reduce median expansions on open grids, or if
the two tie-breaks return different distances on any query.

## Correctness predictions (not optional)

Before any timing, every query used in the main experiment is checked against an
independent BFS distance oracle.  A* must return the same distance as Dijkstra
and BFS.  Paths reconstructed for the validation sample must be contiguous and
their weights must sum to the reported distance.  A mismatch is a failure, not a
data point.
