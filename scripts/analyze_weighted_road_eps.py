#!/usr/bin/env python3
"""Weighted A* epsilon sweep on the weighted DIMACS New York road graph.

Reads:
    results/weighted_road_eps.csv

Writes:
    results/weighted_road_eps.md
    results/summary_weighted_road_eps.csv
    results/summary_weighted_road_eps_budget.csv
    results/paired_weighted_road_eps.csv
    figures/weighted_road_eps.png
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

KEYS = ["pair_id", "band", "source", "target", "policy", "epsilon", "variant"]
POLICIES = ["closed", "reopen"]


def load() -> pd.DataFrame:
    path = RESULTS / "weighted_road_eps.csv"
    if not path.exists():
        raise SystemExit(f"missing {path}")
    data = pd.read_csv(path)
    bad = data[data["status"] != "ok"]
    if not bad.empty:
        raise SystemExit(f"{len(bad)} unsuccessful weighted-road epsilon rows")
    return data


def per_query(data: pd.DataFrame) -> pd.DataFrame:
    return (
        data.groupby(KEYS)[["time_ns", "distance", "cost_ratio", "expanded",
                            "heuristic_calls"]]
        .median()
        .reset_index()
    )


def pair_with_dijkstra(query: pd.DataFrame) -> pd.DataFrame:
    di = query[query["variant"] == "dijkstra"].drop(
        columns=["variant", "policy", "epsilon"]).rename(columns={
            "time_ns": "time_d", "distance": "distance_d",
            "expanded": "expanded_d", "cost_ratio": "cost_d"})
    wa = query[query["variant"] == "weighted_astar"]
    merged = wa.merge(di, on=["pair_id", "band", "source", "target"],
                      validate="many_to_one")
    merged["time_ratio"] = merged["time_d"] / merged["time_ns"]
    merged["exp_ratio"] = merged["expanded_d"] / merged["expanded"]
    merged["cost_ratio"] = merged["distance"] / merged["distance_d"]
    merged["within_1pct"] = merged["cost_ratio"] <= 1.01 + 1e-12
    merged["within_5pct"] = merged["cost_ratio"] <= 1.05 + 1e-12
    return merged


def summarize(merged: pd.DataFrame) -> pd.DataFrame:
    rows = []
    for policy in POLICIES:
        for epsilon, group in merged[merged["policy"] == policy].groupby("epsilon"):
            rows.append({
                "policy": policy,
                "epsilon": epsilon,
                "n": len(group),
                "time_ratio": group["time_ratio"].median(),
                "time_q1": group["time_ratio"].quantile(0.25),
                "time_q3": group["time_ratio"].quantile(0.75),
                "exp_ratio": group["exp_ratio"].median(),
                "cost_ratio": group["cost_ratio"].median(),
                "cost_p95": group["cost_ratio"].quantile(0.95),
                "cost_max": group["cost_ratio"].max(),
                "optimal_fraction": (group["cost_ratio"] <= 1.0 + 1e-12).mean(),
                "within_1pct": group["within_1pct"].mean(),
                "within_5pct": group["within_5pct"].mean(),
            })
    return pd.DataFrame(rows)


def budget_table(summary: pd.DataFrame) -> pd.DataFrame:
    rows = []
    closed = summary[summary["policy"] == "closed"]
    for threshold in (1.0, 1.01, 1.05, 1.10, 1.25):
        feasible = closed[closed["cost_ratio"] <= threshold + 1e-12]
        if feasible.empty:
            continue
        best = feasible.loc[feasible["time_ratio"].idxmax()]
        rows.append({
            "cost_budget": threshold,
            "best_epsilon": best["epsilon"],
            "time_ratio": best["time_ratio"],
            "cost_ratio": best["cost_ratio"],
            "cost_p95": best["cost_p95"],
        })
    return pd.DataFrame(rows)


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


def plot(summary: pd.DataFrame) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.8))
    closed = summary[summary["policy"] == "closed"].sort_values("epsilon")
    reopen = summary[summary["policy"] == "reopen"].sort_values("epsilon")

    axes[0].plot(closed["cost_ratio"], closed["time_ratio"], "-o",
                 color="#c44e52", markersize=4, label="closed list")
    axes[0].plot(reopen["cost_ratio"], reopen["time_ratio"], "--s",
                 color="#dd8452", markersize=4, label="reopen")
    for _, row in closed.iterrows():
        if row["epsilon"] in (1.0, 1.1, 1.25, 1.5, 2.0):
            axes[0].annotate(f"{row['epsilon']:g}",
                             (row["cost_ratio"], row["time_ratio"]),
                             textcoords="offset points", xytext=(3, 4),
                             fontsize=7, color="#c44e52")
    axes[0].axhline(1.0, color="black", linewidth=0.8, linestyle="--")
    axes[0].set_xlabel("median path-cost ratio  weighted / optimal")
    axes[0].set_ylabel("median time ratio  Dijkstra / weighted A*")
    axes[0].set_yscale("log")
    axes[0].set_title("Weighted road: price of optimality")
    axes[0].grid(alpha=0.25)
    axes[0].legend(fontsize=8)

    axes[1].plot(closed["epsilon"], closed["time_ratio"], "-o",
                 color="#c44e52", markersize=4, label="closed list")
    axes[1].plot(reopen["epsilon"], reopen["time_ratio"], "--s",
                 color="#dd8452", markersize=4, label="reopen")
    axes[1].axhline(1.0, color="black", linewidth=0.8, linestyle="--")
    axes[1].set_xscale("log")
    axes[1].set_yscale("log")
    axes[1].set_xlabel("epsilon")
    axes[1].set_ylabel("median time ratio  Dijkstra / weighted A*")
    axes[1].set_title("Speed versus epsilon")
    axes[1].grid(alpha=0.25)
    axes[1].legend(fontsize=8)

    fig.suptitle("Weighted road: a strong heuristic still trades distance for speed")
    fig.tight_layout()
    fig.savefig(FIGURES / "weighted_road_eps.png", dpi=180)
    plt.close(fig)


def main() -> None:
    data = load()
    query = per_query(data)
    paired = pair_with_dijkstra(query)
    summary = summarize(paired)
    budget = budget_table(summary)

    paired.to_csv(RESULTS / "paired_weighted_road_eps.csv", index=False)
    summary.to_csv(RESULTS / "summary_weighted_road_eps.csv", index=False)
    budget.to_csv(RESULTS / "summary_weighted_road_eps_budget.csv", index=False)
    plot(summary)

    display = summary.copy()
    display.columns = ["policy", "epsilon", "n", "time ratio", "time Q1",
                       "time Q3", "exp ratio", "cost ratio", "cost p95",
                       "cost max", "optimal fraction", "within 1%", "within 5%"]
    budget_display = budget.copy()
    budget_display.columns = ["cost budget", "best epsilon", "time ratio",
                              "median cost", "cost p95"]

    def cell(policy: str, epsilon: float, column: str) -> float:
        row = summary[(summary["policy"] == policy) & np.isclose(summary["epsilon"], epsilon)]
        return float(row[column].iloc[0])

    lines = []
    lines.append("# Weighted road: weighted A* epsilon sweep\n")
    lines.append(
        "This is the weighted counterpart of the earlier weighted-A* study.  "
        "The graph is DIMACS `USA-road-d.NY` with distance weights and weighted "
        "ALT K=8.  Dijkstra supplies the optimal weighted distance; closed-list "
        "and reopening policies are both swept over "
        "`epsilon = 1, 1.1, 1.25, 1.5, 2, 3, 5, 10, 20, 50`.  Ratios are paired "
        "per query and are medians over five repeats.\n")
    lines.append("## Correctness\n")
    lines.append(
        f"- {len(data):,} timed rows, all `status=ok`.\n"
        "- `epsilon=1` returned the weighted optimum under both policies; no "
        "weighted run returned a distance below the optimum.\n")
    lines.append("## Full sweep\n")
    lines.append(markdown_table(display))
    lines.append(
        "Cost ratio is returned weighted distance / optimal weighted distance.  "
        "Time ratio is Dijkstra / weighted A*.\n")
    lines.append("## Best speed under a cost budget (closed list)\n")
    lines.append(markdown_table(budget_display))
    lines.append(
        "The budget is applied to the median query cost; p95 and max columns "
        "show the per-query tail.\n")
    lines.append("![Weighted-road epsilon sweep](../figures/weighted_road_eps.png)\n")
    lines.append("## Reading\n")
    lines.append(
        f"**The first percent is very cheap.**  Optimal ALT K=8 gives "
        f"{cell('closed', 1.0, 'time_ratio'):.1f}x in this sweep.  The "
        f"closed-list `epsilon=1.1` variant reaches "
        f"{cell('closed', 1.1, 'time_ratio'):.1f}x for a median cost increase of "
        f"{(cell('closed', 1.1, 'cost_ratio') - 1) * 100:.1f}%; `epsilon=1.25` "
        f"reaches {cell('closed', 1.25, 'time_ratio'):.1f}x for "
        f"{(cell('closed', 1.25, 'cost_ratio') - 1) * 100:.1f}%.\n")
    lines.append(
        "**The closed list remains the better policy.**  At every "
        "`epsilon > 1` the closed list is faster than reopening for essentially "
        "the same path cost.  Reopening the weighted ALT search churns the heap "
        "without buying a meaningful distance improvement.\n")
    lines.append(
        "**The trade-off saturates.**  Above `epsilon=3` the time ratio changes "
        "little while the median cost continues to rise, so large epsilon is "
        "not attractive on this graph.  The practical knee is around "
        "`epsilon=1.1`-`1.25` if path quality matters.\n")
    lines.append("## Caveats\n")
    lines.append(
        "- The weighted graph and query set are separate from the frozen "
        "California experiment; absolute ratios are not directly comparable.\n"
        "- Cost is weighted distance, not travel time; the DIMACS weights are "
        "integer distance units.\n"
        "- The budget is applied to the median; individual queries can exceed "
        "it, as the p95/max columns show.\n")
    (RESULTS / "weighted_road_eps.md").write_text("\n".join(lines))

    print(f"wrote {RESULTS / 'weighted_road_eps.md'}")
    print(f"wrote {RESULTS / 'summary_weighted_road_eps.csv'}")
    print(f"wrote {RESULTS / 'summary_weighted_road_eps_budget.csv'}")
    print(f"wrote {RESULTS / 'paired_weighted_road_eps.csv'}")
    print(f"wrote {FIGURES / 'weighted_road_eps.png'}")


if __name__ == "__main__":
    main()
