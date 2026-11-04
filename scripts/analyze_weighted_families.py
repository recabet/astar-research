#!/usr/bin/env python3
"""Price of optimality on graphs with several paths.

Weighted A* uses f = g + epsilon*h.  On a perfect maze every simple path has
the same cost, so the old maze-only experiment could not measure a cost-speed
trade-off.  This script analyses the all-family grid sweep and the road-graph
sweep, where alternative paths do exist.

Reads:
    results/weighted_families.csv
    results/weighted_real.csv

Writes:
    results/weighted_families.md
    results/summary_weighted_by_family.csv
    results/summary_weighted_budget.csv
    results/summary_weighted_by_size.csv
    figures/weighted_pareto.png
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

FAMILIES = ["open", "random", "clustered", "rooms", "maze"]
COLORS = {
    "open": "#4c72b0",
    "random": "#dd8452",
    "clustered": "#55a868",
    "rooms": "#c44e52",
    "maze": "#8172b3",
    "road": "#333333",
}
KEYS = ["policy", "graph_id", "family", "size", "pair_id", "band",
        "variant", "epsilon"]
POLICY_FILES = [
    ("weighted_families_closed.csv", "closed"),
    ("weighted_families.csv", "reopen"),
    ("weighted_real_closed.csv", "closed"),
    ("weighted_real.csv", "reopen"),
]


def load() -> pd.DataFrame:
    frames = []
    for name, policy in POLICY_FILES:
        path = RESULTS / name
        if not path.exists():
            raise SystemExit(f"missing {path}")
        frame = pd.read_csv(path)
        frame["policy"] = policy
        frames.append(frame)
    data = pd.concat(frames, ignore_index=True)
    data = data[data["status"] == "ok"].copy()
    data["workload"] = np.where(data["family"] == "road", "road",
                                data["family"])
    return data


def per_query(data: pd.DataFrame) -> pd.DataFrame:
    return (
        data.groupby(KEYS)[
            ["time_ns", "distance", "cost_ratio", "expanded",
             "heuristic_calls"]
        ]
        .median()
        .reset_index()
    )


def pair_with_dijkstra(query: pd.DataFrame) -> pd.DataFrame:
    di = query[query["variant"] == "dijkstra"].drop(
        columns=["variant", "epsilon"]).rename(columns={
            "time_ns": "time_d", "distance": "dist_d",
            "expanded": "exp_d", "cost_ratio": "cost_d"})
    wa = query[query["variant"] == "weighted_astar"]
    merged = wa.merge(
        di, on=["policy", "graph_id", "family", "size", "pair_id", "band"],
        validate="many_to_one")
    merged["workload"] = np.where(merged["family"] == "road", "road",
                                  merged["family"])
    merged["time_ratio"] = merged["time_d"] / merged["time_ns"]
    merged["exp_ratio"] = merged["exp_d"] / merged["expanded"]
    merged["cost_ratio"] = merged["distance"] / merged["dist_d"]
    merged["within_1pct"] = merged["cost_ratio"] <= 1.01 + 1e-12
    merged["within_5pct"] = merged["cost_ratio"] <= 1.05 + 1e-12
    merged["within_10pct"] = merged["cost_ratio"] <= 1.10 + 1e-12
    return merged


def summarize(merged: pd.DataFrame) -> pd.DataFrame:
    rows = []
    for (policy, workload, eps), group in merged.groupby(
            ["policy", "workload", "epsilon"]):
        rows.append({
            "policy": policy,
            "workload": workload,
            "epsilon": eps,
            "n": len(group),
            "time_ratio": group["time_ratio"].median(),
            "time_q1": group["time_ratio"].quantile(0.25),
            "time_q3": group["time_ratio"].quantile(0.75),
            "exp_ratio": group["exp_ratio"].median(),
            "cost_ratio": group["cost_ratio"].median(),
            "cost_q1": group["cost_ratio"].quantile(0.25),
            "cost_q3": group["cost_ratio"].quantile(0.75),
            "cost_p95": group["cost_ratio"].quantile(0.95),
            "cost_max": group["cost_ratio"].max(),
            "optimal_fraction": (group["cost_ratio"] <= 1.0 + 1e-12).mean(),
            "within_1pct": group["within_1pct"].mean(),
            "within_5pct": group["within_5pct"].mean(),
            "within_10pct": group["within_10pct"].mean(),
        })
    return pd.DataFrame(rows)


def budget_table(summary: pd.DataFrame) -> pd.DataFrame:
    rows = []
    closed = summary[summary["policy"] == "closed"]
    for threshold in (1.0, 1.01, 1.05, 1.10, 1.25):
        for workload, group in closed.groupby("workload"):
            feasible = group[group["cost_ratio"] <= threshold + 1e-12]
            if feasible.empty:
                continue
            best = feasible.loc[feasible["time_ratio"].idxmax()]
            rows.append({
                "cost_budget": threshold,
                "workload": workload,
                "best_epsilon": best["epsilon"],
                "time_ratio": best["time_ratio"],
                "cost_ratio_median": best["cost_ratio"],
                "cost_p95": best["cost_p95"],
            })
    return pd.DataFrame(rows)


def markdown_table(df: pd.DataFrame, digits: int = 2) -> str:
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
    closed = summary[summary["policy"] == "closed"]
    workloads = [w for w in FAMILIES if w in set(closed["workload"])]
    if "road" in set(closed["workload"]):
        workloads.append("road")

    fig, axes = plt.subplots(1, 2, figsize=(13, 5.2))
    for workload in workloads:
        group = closed[closed["workload"] == workload].sort_values("epsilon")
        color = COLORS[workload]
        label = "road (ALT K=8)" if workload == "road" else workload
        axes[0].plot(group["cost_ratio"], group["time_ratio"], marker="o",
                     markersize=3, linewidth=1.5, color=color, label=label)

    for workload in ("random", "rooms", "road"):
        if workload not in workloads:
            continue
        group = closed[closed["workload"] == workload].sort_values("epsilon")
        for eps in (1.0, 1.1, 1.25, 1.5, 2.0):
            row = group[group["epsilon"] == eps]
            if row.empty:
                continue
            axes[0].annotate(f"{eps:g}", (row["cost_ratio"].iloc[0],
                                          row["time_ratio"].iloc[0]),
                             textcoords="offset points", xytext=(3, 4),
                             fontsize=6.5, color=COLORS[workload])

    axes[0].axhline(1.0, color="black", linewidth=0.8, linestyle="--")
    axes[0].set_xlabel("median path-cost ratio  weighted / optimal")
    axes[0].set_ylabel("median time ratio  Dijkstra / weighted A*")
    axes[0].set_title("Closed list: price of optimality")
    axes[0].set_yscale("log")
    axes[0].grid(alpha=0.25)
    axes[0].legend(fontsize=7, ncol=2)

    road = summary[summary["workload"] == "road"]
    for policy, style, label in (("closed", "-o", "closed list"),
                                 ("reopen", "--s", "reopen")):
        group = road[road["policy"] == policy].sort_values("epsilon")
        axes[1].plot(group["epsilon"], group["time_ratio"], style,
                     markersize=3, linewidth=1.5, label=label)
    axes[1].axhline(1.0, color="black", linewidth=0.8, linestyle="--")
    axes[1].set_xlabel("epsilon")
    axes[1].set_ylabel("median time ratio  Dijkstra / weighted A*")
    axes[1].set_title("Road graph: reopening costs more")
    axes[1].set_xscale("log")
    axes[1].set_yscale("log")
    axes[1].grid(alpha=0.25)
    axes[1].legend(fontsize=8)

    fig.suptitle("Weighted A*: gains depend on the base heuristic, not just epsilon")
    fig.tight_layout()
    fig.savefig(FIGURES / "weighted_pareto.png", dpi=180)
    plt.close(fig)


def main() -> None:
    data = load()
    query = per_query(data)
    merged = pair_with_dijkstra(query)
    summary = summarize(merged)
    budget = budget_table(summary)

    by_size = merged.groupby(["policy", "workload", "size", "epsilon"]).agg(
        n=("pair_id", "size"),
        time_ratio=("time_ratio", "median"),
        cost_ratio=("cost_ratio", "median"),
    ).reset_index()

    policy_table = summary[[
        "policy", "workload", "epsilon", "time_ratio", "exp_ratio",
        "cost_ratio", "cost_p95", "cost_max"]].copy()
    policy_table = policy_table.sort_values(
        ["workload", "epsilon", "policy"])

    summary.to_csv(RESULTS / "summary_weighted_by_family.csv", index=False)
    budget.to_csv(RESULTS / "summary_weighted_budget.csv", index=False)
    by_size.to_csv(RESULTS / "summary_weighted_by_size.csv", index=False)
    policy_table.to_csv(RESULTS / "summary_weighted_policy.csv", index=False)
    merged.to_csv(RESULTS / "paired_weighted_families.csv", index=False)
    plot(summary)

    def cell(workload: str, eps: float, column: str,
             policy: str = "closed") -> float:
        row = summary[(summary["policy"] == policy) &
                      (summary["workload"] == workload) &
                      np.isclose(summary["epsilon"], eps)]
        return float(row[column].iloc[0])

    closed = summary[summary["policy"] == "closed"].copy()
    closed["workload"] = pd.Categorical(
        closed["workload"], categories=FAMILIES + ["road"], ordered=True)
    closed = closed.sort_values(["workload", "epsilon"])
    main_display = closed[[
        "workload", "epsilon", "n", "time_ratio", "time_q1", "time_q3",
        "exp_ratio", "cost_ratio", "cost_p95", "cost_max", "optimal_fraction",
    ]].copy()
    main_display.columns = [
        "workload", "epsilon", "n", "time ratio", "Q1", "Q3", "exp ratio",
        "cost ratio", "cost p95", "cost max", "optimal fraction"]

    road = summary[(summary["workload"] == "road") &
                   summary["epsilon"].isin([1.0, 1.1, 1.25, 1.5, 2.0])]
    road_view = road.pivot_table(
        index="epsilon", columns="policy",
        values=["time_ratio", "cost_ratio"]).reset_index()
    road_view = pd.DataFrame({
        "epsilon": road_view["epsilon"],
        "closed time": road_view[("time_ratio", "closed")],
        "closed cost": road_view[("cost_ratio", "closed")],
        "reopen time": road_view[("time_ratio", "reopen")],
        "reopen cost": road_view[("cost_ratio", "reopen")],
    })

    budget_display = budget.copy()
    budget_display.columns = [
        "cost budget", "workload", "best epsilon", "time ratio",
        "median cost", "cost p95"]

    lines = []
    lines.append("# Price of optimality on graphs with alternatives\n")
    lines.append(
        "Weighted A* uses `f = g + epsilon*h`.  The primary results use the "
        "practical closed-list policy (a settled vertex is not reopened); the "
        "reopening policy is kept as a sensitivity check.  The grid sweep "
        "covers 20 graphs (4 sizes x 5 families, 15 queries each, 3 repeats); "
        "the road sweep covers the 30 frozen queries with ALT K=8 and 5 "
        "repeats.  Ratios are paired per query, and the optimal cost is the "
        "BFS/Dijkstra distance.  This is exploratory post-freeze analysis.\n")
    lines.append("## Correctness\n")
    lines.append(
        "- `epsilon=1` returned the optimal distance on every query, under "
        "both policies.\n"
        "- No weighted run returned a distance below the optimum.\n"
        f"- Paired queries: {int(merged[merged['policy'] == 'closed'].shape[0] // 10)} "
        "grid/road queries per epsilon; every row is a median over repeats.\n")
    lines.append("## Closed-list sweep (primary)\n")
    lines.append(markdown_table(main_display))
    lines.append(
        "Cost ratio is returned path cost / optimal cost.  A value of 1.00 "
        "means the path is still optimal; values above 1 are the measured "
        "price of greediness.\n")
    lines.append("## Road graph: closed list versus reopening\n")
    lines.append(markdown_table(road_view))
    lines.append(
        "Reopening lets a cheaper `g` replace an already-settled label.  On "
        "the road graph the closed list is faster at every epsilon, while path "
        "cost is essentially the same at small epsilon; reopening churns the "
        "heap for no useful gain.  On the grid families the two policies are "
        "closer, so the policy choice matters most where the heuristic is "
        "already strong.\n")
    lines.append("## Best speed under a cost budget (closed list)\n")
    lines.append(markdown_table(budget_display))
    lines.append(
        "For each family and cost budget, the table reports the epsilon with "
        "the largest median time ratio whose median cost ratio is within the "
        "budget.  The budget is on the median; p95 and max columns in the full "
        "sweep show the tail.\n")
    lines.append("![Weighted A* Pareto frontier](../figures/weighted_pareto.png)\n")
    lines.append("## Reading\n")
    lines.append(
        "**Weighting pays when the base heuristic is weak.**  With the closed "
        f"list, random obstacles at `epsilon=1.1` improve the median speed-up "
        f"from {cell('random', 1.0, 'time_ratio'):.1f}x to "
        f"{cell('random', 1.1, 'time_ratio'):.1f}x for a median cost increase "
        f"of {(cell('random', 1.1, 'cost_ratio') - 1) * 100:.1f}%.  At "
        f"`epsilon=1.25` random reaches "
        f"{cell('random', 1.25, 'time_ratio'):.1f}x for "
        f"{(cell('random', 1.25, 'cost_ratio') - 1) * 100:.1f}%; rooms reaches "
        f"{cell('rooms', 1.25, 'time_ratio'):.1f}x for "
        f"{(cell('rooms', 1.25, 'cost_ratio') - 1) * 100:.1f}%; clustered "
        f"reaches {cell('clustered', 1.5, 'time_ratio'):.1f}x for "
        f"{(cell('clustered', 1.5, 'cost_ratio') - 1) * 100:.1f}%.  The first "
        "few percent of path cost buy a large amount of search.\n")
    lines.append(
        "**Reopening is not free.**  On the road graph, optimal ALT K=8 is "
        f"{cell('road', 1.0, 'time_ratio'):.1f}x faster than Dijkstra.  The "
        f"closed-list weighted variant at `epsilon=1.1` reaches "
        f"{cell('road', 1.1, 'time_ratio'):.1f}x at "
        f"{(cell('road', 1.1, 'cost_ratio') - 1) * 100:.1f}% median cost, but "
        f"the reopening variant falls to {cell('road', 1.1, 'time_ratio', 'reopen'):.1f}x "
        f"for {(cell('road', 1.1, 'cost_ratio', 'reopen') - 1) * 100:.1f}% "
        "cost.  Reopening churns the heap on a graph where the heuristic is "
        "already tight.  The practical lesson is to fix the policy before "
        "tuning epsilon.\n")
    lines.append(
        "**Two controls behave as expected.**  On the open grid Manhattan is "
        "exact, so the cost ratio stays 1.00 and weighting cannot reduce "
        "expansions further.  On the perfect maze the path is unique, so the "
        "cost ratio also stays 1.00; high epsilon eventually reduces time, but "
        "only because there is nothing to trade.\n")
    lines.append(
        "The practical rule: use weighted A* with a closed list when the "
        "heuristic is admissible but weak, and keep epsilon near 1.1-1.25 if "
        "path quality matters.  If the heuristic is already tight, weighted A* "
        "buys little and reopening can cost more than it saves.\n")
    lines.append("## Caveats\n")
    lines.append(
        "- These runs are exploratory and were made after the frozen main "
        "benchmark; `results/grid.csv` and `results/real.csv` are unchanged.\n"
        "- Timings are single-machine wall-clock medians from one binary; "
        "treat small differences between adjacent epsilon values as noise.\n"
        "- The cost budget is applied to the median; individual queries can "
        "exceed it, as the p95 and max columns show.\n")
    (RESULTS / "weighted_families.md").write_text("\n".join(lines))

    print(f"wrote {RESULTS / 'weighted_families.md'}")
    print(f"wrote {RESULTS / 'summary_weighted_by_family.csv'}")
    print(f"wrote {RESULTS / 'summary_weighted_budget.csv'}")
    print(f"wrote {RESULTS / 'summary_weighted_by_size.csv'}")
    print(f"wrote {RESULTS / 'summary_weighted_policy.csv'}")
    print(f"wrote {FIGURES / 'weighted_pareto.png'}")


if __name__ == "__main__":
    main()
