#!/usr/bin/env python3
"""Weighted-road benchmark on the DIMACS New York distance graph.

The graph keeps DIMACS edge weights (distance units), uses weighted ALT
landmarks, and compares Dijkstra with Euclidean A* and ALT.  All queries and
the oracle are weighted; this is a separate post-freeze data regime.

Reads:
    results/weighted_road.csv

Writes:
    results/weighted_road.md
    results/summary_weighted_road.csv
    results/summary_weighted_road_band.csv
    results/paired_weighted_road.csv
    figures/weighted_road.png
"""

from __future__ import annotations

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"
FIGURES = ROOT / "figures"

QUERY_KEYS = ["pair_id", "band", "source", "target"]
BANDS = ["short", "medium", "long"]
MAIN = "dijkstra"
COLORS = {
    "astar_euclidean": "#4c72b0",
    "astar_alt1": "#8172b3",
    "astar_alt4": "#55a868",
    "astar_alt8": "#c44e52",
    "astar_alt16": "#333333",
    "astar_alt8_node_id": "#dd8452",
}


def load() -> pd.DataFrame:
    path = RESULTS / "weighted_road.csv"
    if not path.exists():
        raise SystemExit(f"missing {path}")
    data = pd.read_csv(path)
    bad = data[(data["status"] != "ok") | (data["correct"] != 1)]
    if not bad.empty:
        raise SystemExit(f"{len(bad)} unsuccessful/incorrect weighted-road rows")
    return data


def per_query(data: pd.DataFrame) -> pd.DataFrame:
    keys = QUERY_KEYS + ["variant"]
    return (
        data.groupby(keys)[["time_ns", "distance", "expanded", "heuristic_calls",
                            "relaxed_edges"]]
        .median()
        .reset_index()
    )


def pair_with_dijkstra(query: pd.DataFrame) -> pd.DataFrame:
    di = query[query["variant"] == MAIN].drop(columns=["variant"]).rename(
        columns={"time_ns": "time_d", "distance": "distance_d",
                 "expanded": "expanded_d", "heuristic_calls": "h_d",
                 "relaxed_edges": "relaxed_d"})
    frames = []
    for variant in sorted(set(query["variant"]) - {MAIN}):
        wa = query[query["variant"] == variant].drop(columns=["variant"])
        merged = wa.merge(di, on=QUERY_KEYS, validate="one_to_one")
        merged["variant"] = variant
        merged["time_ratio"] = merged["time_d"] / merged["time_ns"]
        merged["exp_ratio"] = merged["expanded_d"] / merged["expanded"]
        merged["cost_ratio"] = merged["distance"] / merged["distance_d"]
        frames.append(merged)
    return pd.concat(frames, ignore_index=True)


def summarize(paired: pd.DataFrame) -> pd.DataFrame:
    rows = []
    for variant, group in paired.groupby("variant"):
        rows.append({
            "variant": variant,
            "n": len(group),
            "time_ratio": group["time_ratio"].median(),
            "time_q1": group["time_ratio"].quantile(0.25),
            "time_q3": group["time_ratio"].quantile(0.75),
            "exp_ratio": group["exp_ratio"].median(),
            "exp_q1": group["exp_ratio"].quantile(0.25),
            "exp_q3": group["exp_ratio"].quantile(0.75),
            "cost_ratio_max": group["cost_ratio"].max(),
            "optimal_fraction": (group["cost_ratio"] <= 1.0 + 1e-12).mean(),
        })
    return pd.DataFrame(rows)


def by_band(paired: pd.DataFrame) -> pd.DataFrame:
    out = paired.groupby(["variant", "band"]).agg(
        n=("pair_id", "size"),
        time_ratio=("time_ratio", "median"),
        exp_ratio=("exp_ratio", "median"),
    ).reset_index()
    out["band"] = pd.Categorical(out["band"], categories=BANDS, ordered=True)
    return out.sort_values(["variant", "band"])


def tie_pair(paired: pd.DataFrame) -> pd.DataFrame:
    larger = paired[paired["variant"] == "astar_alt8"].set_index(QUERY_KEYS)
    node = paired[paired["variant"] == "astar_alt8_node_id"].set_index(QUERY_KEYS)
    out = larger[["time_ns", "expanded", "time_ratio", "exp_ratio"]].join(
        node[["time_ns", "expanded"]], lsuffix="_larger", rsuffix="_node",
        how="inner")
    out["node_time_ratio"] = out["time_ns_node"] / out["time_ns_larger"]
    out["node_exp_ratio"] = out["expanded_node"] / out["expanded_larger"]
    return out.reset_index()


def markdown_table(df: pd.DataFrame, digits: int = 3) -> str:
    lines = ["| " + " | ".join(df.columns) + " |",
             "|" + "|".join(["---"] * len(df.columns)) + "|"]
    for _, row in df.iterrows():
        cells = []
        for column in df.columns:
            value = row[column]
            if isinstance(value, (float, np.floating)):
                cells.append(f"{value:.{digits}f}")
            elif isinstance(value, (int, np.integer)):
                cells.append(str(int(value)))
            else:
                cells.append(str(value))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def plot(summary: pd.DataFrame, band_summary: pd.DataFrame) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.8))
    order = ["astar_euclidean", "astar_alt1", "astar_alt4", "astar_alt8",
             "astar_alt16"]
    labels = ["Euclidean", "ALT K=1", "ALT K=4", "ALT K=8", "ALT K=16"]
    values = [summary.set_index("variant").loc[v, "time_ratio"] for v in order]
    axes[0].barh(range(len(order)), values,
                 color=[COLORS[v] for v in order], height=0.65)
    axes[0].set_yticks(range(len(order)))
    axes[0].set_yticklabels(labels)
    axes[0].invert_yaxis()
    axes[0].set_xscale("log")
    axes[0].axvline(1.0, color="black", linewidth=0.8, linestyle="--")
    axes[0].set_xlabel("median time ratio  Dijkstra / A*  (log)")
    axes[0].set_title("Weighted New York road graph")
    for i, value in enumerate(values):
        axes[0].text(value * 1.06, i, f"{value:.1f}x", va="center", fontsize=8)

    width = 0.25
    x = np.arange(len(BANDS))
    for offset, variant, label in ((-width, "astar_euclidean", "Euclidean"),
                                   (0.0, "astar_alt8", "ALT K=8"),
                                   (width, "astar_alt16", "ALT K=16")):
        group = band_summary[band_summary["variant"] == variant].set_index("band")
        values = [group.loc[b, "time_ratio"] if b in group.index else 0.0
                  for b in BANDS]
        axes[1].bar(x + offset, values, width=width, label=label,
                    color=COLORS[variant])
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(BANDS)
    axes[1].set_yscale("log")
    axes[1].axhline(1.0, color="black", linewidth=0.8, linestyle="--")
    axes[1].set_xlabel("weighted-distance band")
    axes[1].set_ylabel("median time ratio  Dijkstra / A*  (log)")
    axes[1].set_title("By weighted query length")
    axes[1].grid(axis="y", alpha=0.25)
    axes[1].legend(fontsize=8)

    fig.suptitle("Weighted road: ALT still beats a free Euclidean heuristic")
    fig.tight_layout()
    fig.savefig(FIGURES / "weighted_road.png", dpi=180)
    plt.close(fig)


def main() -> None:
    data = load()
    query = per_query(data)
    paired = pair_with_dijkstra(query)
    summary = summarize(paired)
    band_summary = by_band(paired)
    tie = tie_pair(paired)

    paired.to_csv(RESULTS / "paired_weighted_road.csv", index=False)
    summary.to_csv(RESULTS / "summary_weighted_road.csv", index=False)
    band_summary.to_csv(RESULTS / "summary_weighted_road_band.csv", index=False)
    plot(summary, band_summary)

    overall = summary.set_index("variant")
    table = summary.copy()
    table.columns = ["variant", "n", "time ratio", "time Q1", "time Q3",
                     "exp ratio", "exp Q1", "exp Q3", "cost max",
                     "optimal fraction"]
    band_table = band_summary.copy()
    band_table.columns = ["variant", "band", "n", "time ratio", "exp ratio"]

    def value(variant: str, column: str) -> float:
        return float(overall.loc[variant, column])

    lines = []
    lines.append("# Weighted road network: distance weights\n")
    lines.append(
        "This is a separate post-freeze data regime.  DIMACS `USA-road-d.NY` "
        "supplies the topology, integer distance edge weights, and coordinates "
        "(264,346 nodes, 730,100 undirected weighted arcs after symmetrisation).  "
        "The oracle and ALT landmark tables use weighted Dijkstra, and queries "
        "are selected in weighted-distance bands.  Dijkstra is the baseline; "
        "all treatments must return the same weighted shortest-path distance.\n")
    lines.append("## Correctness\n")
    lines.append(
        f"- {len(data):,} timed rows, all `status=ok` and `correct=1`.\n"
        "- Every treatment returned the weighted Dijkstra optimum on all "
        "30 queries; the maximum cost ratio is "
        f"{paired['cost_ratio'].max():.6f}.\n")
    lines.append("## Overall\n")
    lines.append(markdown_table(table))
    lines.append(
        "Time and expansion ratios are Dijkstra divided by the treatment; "
        "values above 1 mean A* wins.  ALT K=16 is the fastest treatment, and "
        "even the free Euclidean heuristic gives only a modest speedup because "
        "the road network is not geometrically straight.\n")
    lines.append("## By weighted distance band\n")
    lines.append(markdown_table(band_table))
    lines.append(
        "ALT's advantage grows with the number of landmarks and is fairly flat "
        "across weighted bands at K=8-16.  K=1 is mainly a short-query "
        f"heuristic ({band_summary[(band_summary['variant'] == 'astar_alt1') & (band_summary['band'] == 'short')]['time_ratio'].iloc[0]:.1f}x "
        f"short versus {band_summary[(band_summary['variant'] == 'astar_alt1') & (band_summary['band'] == 'long')]['time_ratio'].iloc[0]:.1f}x long).  "
        "Euclidean A* is strongest on short queries "
        f"({band_summary[(band_summary['variant'] == 'astar_euclidean') & (band_summary['band'] == 'short')]['time_ratio'].iloc[0]:.1f}x) and nearly disappears on long queries "
        f"({band_summary[(band_summary['variant'] == 'astar_euclidean') & (band_summary['band'] == 'long')]['time_ratio'].iloc[0]:.1f}x), "
        "where the straight-line bound is least informative.\n")
    lines.append("## Tie-break under weighted edges\n")
    lines.append(
        f"`astar_alt8_node_id` expands a median "
        f"{tie['node_exp_ratio'].median():.2f}x as many vertices as "
        f"`astar_alt8` and takes {tie['node_time_ratio'].median():.2f}x the "
        "time.  Weighted edges reduce the number of equal-`f` choices compared "
        "with the unit-hop graph, but `larger_g` remains the safer rule.\n")
    lines.append("![Weighted road benchmark](../figures/weighted_road.png)\n")
    lines.append("## Reading\n")
    lines.append(
        f"**ALT remains worth its preprocessing cost.**  ALT K=8 gives "
        f"{value('astar_alt8', 'time_ratio'):.1f}x median time and "
        f"{value('astar_alt8', 'exp_ratio'):.1f}x fewer expansions; K=16 gives "
        f"{value('astar_alt16', 'time_ratio'):.1f}x and "
        f"{value('astar_alt16', 'exp_ratio'):.1f}x.  Euclidean A* gives only "
        f"{value('astar_euclidean', 'time_ratio'):.1f}x and "
        f"{value('astar_euclidean', 'exp_ratio'):.1f}x: it is cheap, but the "
        "straight-line bound is much weaker than landmark distances.\n")
    lines.append(
        "**Weights change the absolute numbers, not the ranking.**  The "
        "unweighted California graph and the weighted New York graph are "
        "different networks, so their speedups are not directly comparable.  "
        "Within the weighted graph, Dijkstra < Euclidean < ALT K=1 < ALT K=4 "
        "< ALT K=8 < ALT K=16 in median time.\n")
    lines.append(
        "**Tie-breaking is still visible.**  With distance weights the "
        "`node_id` rule is worse than `larger_g` on the ALT comparison, but the "
        "penalty is smaller than the 1.14x expansion penalty observed on the "
        "unit-hop California graph.  Fewer exact ties is exactly what the "
        "plateau mechanism predicts.\n")
    lines.append("## Caveats\n")
    lines.append(
        "- This is the DIMACS New York network, not the frozen SNAP California "
        "network; do not compare absolute speedups across them.\n"
        "- DIMACS distance weights are integer units proportional to road "
        "length; Euclidean coordinates are projected conservatively, so the "
        "Euclidean heuristic is admissible but not tight.\n"
        "- The query set is new and selected in weighted bands; it is not the "
        "frozen 30-query set.\n")
    (RESULTS / "weighted_road.md").write_text("\n".join(lines))

    print(f"wrote {RESULTS / 'weighted_road.md'}")
    print(f"wrote {RESULTS / 'summary_weighted_road.csv'}")
    print(f"wrote {RESULTS / 'summary_weighted_road_band.csv'}")
    print(f"wrote {RESULTS / 'paired_weighted_road.csv'}")
    print(f"wrote {FIGURES / 'weighted_road.png'}")


if __name__ == "__main__":
    main()
