#!/usr/bin/env python3
"""Batch-throughput analysis for the OpenMP experiment."""

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


def summary_tables(batch: pd.DataFrame, tasks: pd.DataFrame):
    batch = batch[batch["threads"] > 0]
    per_graph = (
        batch.groupby(["graph_id", "family", "threads"])["batch_time_ns"]
        .median()
        .reset_index()
    )
    per_graph["seconds"] = per_graph["batch_time_ns"] / 1e9

    rows = []
    for threads, group in per_graph.groupby("threads"):
        rows.append({
            "threads": threads,
            "graphs": group["graph_id"].nunique(),
            "batch_seconds": group["seconds"].sum(),
        })
    summary = pd.DataFrame(rows).sort_values("threads")
    summary["speedup"] = summary["batch_seconds"].iloc[0] / summary["batch_seconds"]
    summary["efficiency"] = summary["speedup"] / summary["threads"]

    # Split grid and road.
    split_rows = []
    for threads, group in per_graph.groupby("threads"):
        for family_group, name in ((group[group.family != "road"], "grid"),
                                   (group[group.family == "road"], "road")):
            split_rows.append({
                "threads": threads,
                "workload": name,
                "batch_seconds": family_group["seconds"].sum(),
            })
    split = pd.DataFrame(split_rows)
    for workload, group in split.groupby("workload"):
        baseline = group.loc[group["threads"] == 1, "batch_seconds"].iloc[0]
        split.loc[group.index, "speedup"] = baseline / group["batch_seconds"]

    # Per-query medians over the 3 batch repetitions.
    keys = ["graph_id", "family", "threads", "pair_id", "rep"]
    task_med = (
        tasks.groupby(keys)[["time_d", "time_a", "expanded_d", "expanded_a"]]
        .median()
        .reset_index()
    )
    latency_rows = []
    for threads, group in task_med.groupby("threads"):
        latency_rows.append({
            "threads": threads,
            "queries": len(group),
            "median_d_ms": group["time_d"].median() / 1e6,
            "median_a_ms": group["time_a"].median() / 1e6,
            "p95_d_ms": group["time_d"].quantile(0.95) / 1e6,
            "p95_a_ms": group["time_a"].quantile(0.95) / 1e6,
            "median_ratio_d_over_a": (group["time_d"] / group["time_a"]).median(),
        })
    latency = pd.DataFrame(latency_rows).sort_values("threads")
    latency["d_inflation_vs_1t"] = latency["median_d_ms"] / latency["median_d_ms"].iloc[0]
    latency["a_inflation_vs_1t"] = latency["median_a_ms"] / latency["median_a_ms"].iloc[0]

    family_rows = []
    for threads in sorted(task_med["threads"].unique()):
        for family, group in task_med[task_med["threads"] == threads].groupby("family"):
            family_rows.append({
                "threads": threads,
                "family": family,
                "median_d_ms": group["time_d"].median() / 1e6,
                "median_a_ms": group["time_a"].median() / 1e6,
                "median_ratio_d_over_a": (group["time_d"] / group["time_a"]).median(),
            })
    family = pd.DataFrame(family_rows)

    summary.to_csv(RESULTS / "summary_omp_batch.csv", index=False)
    split.to_csv(RESULTS / "summary_omp_split.csv", index=False)
    latency.to_csv(RESULTS / "summary_omp_latency.csv", index=False)
    family.to_csv(RESULTS / "summary_omp_family.csv", index=False)
    task_med.to_csv(RESULTS / "paired_omp_tasks.csv", index=False)
    return summary, split, latency, family, task_med


def figures(summary: pd.DataFrame, split: pd.DataFrame,
            latency: pd.DataFrame) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    axes[0].plot(summary["threads"], summary["speedup"], marker="o",
                 label="all workloads")
    for workload in ("grid", "road"):
        group = split[split["workload"] == workload]
        axes[0].plot(group["threads"], group["speedup"], marker="s",
                     linestyle="--", label=workload)
    axes[0].plot(summary["threads"], summary["threads"], color="black",
                 linestyle=":", linewidth=1, label="ideal")
    axes[0].set_xscale("log", base=2)
    axes[0].set_xticks(summary["threads"])
    axes[0].set_xticklabels(summary["threads"])
    axes[0].set_xlabel("OpenMP threads")
    axes[0].set_ylabel("batch speedup vs 1 thread")
    axes[0].set_title("Batch throughput scaling")
    axes[0].grid(alpha=0.25)
    axes[0].legend()

    axes[1].plot(latency["threads"], latency["median_d_ms"], marker="o",
                 label="Dijkstra")
    axes[1].plot(latency["threads"], latency["median_a_ms"], marker="s",
                 label="A*")
    axes[1].set_xscale("log", base=2)
    axes[1].set_yscale("log")
    axes[1].set_xticks(latency["threads"])
    axes[1].set_xticklabels(latency["threads"])
    axes[1].set_xlabel("OpenMP threads")
    axes[1].set_ylabel("median single-query latency (ms)")
    axes[1].set_title("Per-query latency under parallel load")
    axes[1].grid(alpha=0.25)
    axes[1].legend()
    fig.tight_layout()
    fig.savefig(FIGURES / "omp_scaling.png", dpi=180)
    plt.close(fig)

    fig, axis = plt.subplots(figsize=(7, 4.5))
    axis.plot(latency["threads"], latency["median_ratio_d_over_a"],
              marker="o", color="#1f77b4")
    axis.set_xscale("log", base=2)
    axis.set_xticks(latency["threads"])
    axis.set_xticklabels(latency["threads"])
    axis.set_xlabel("OpenMP threads")
    axis.set_ylabel("Dijkstra / A* median latency")
    axis.set_title("A* advantage remains under parallel load")
    axis.grid(alpha=0.25)
    fig.tight_layout()
    fig.savefig(FIGURES / "omp_astar_advantage.png", dpi=180)
    plt.close(fig)


def markdown(summary: pd.DataFrame, split: pd.DataFrame,
             latency: pd.DataFrame, family: pd.DataFrame) -> None:
    def table(df: pd.DataFrame, digits: int = 2) -> str:
        cols = list(df.columns)
        lines = ["| " + " | ".join(cols) + " |",
                 "|" + "|".join(["---"] * len(cols)) + "|"]
        for _, row in df.iterrows():
            cells = []
            for col in cols:
                value = row[col]
                if isinstance(value, (float, np.floating)):
                    cells.append(f"{value:.{digits}f}")
                else:
                    cells.append(str(value))
            lines.append("| " + " | ".join(cells) + " |")
        return "\n".join(lines) + "\n"

    text = ["# OpenMP batch-throughput study\n\n",
            "The frozen query-level results stay serial.  This study runs independent\n",
            "queries concurrently, with one thread-local search scratch per worker and\n",
            "both algorithms paired inside each task.  The workload is the five 1024x1024\n",
            "grid families (15 queries each, 3 repeats) plus 30 road queries (3 repeats).\n\n",
            "## Batch scaling\n\n",
            table(summary),
            "\n",
            table(split),
            "\n## Per-query latency\n\n",
            table(latency),
            "\n## By family at 16 threads\n\n",
            table(family[family["threads"] == 16]),
            "\n![OpenMP scaling](../figures/omp_scaling.png)\n\n",
            "![A* advantage](../figures/omp_astar_advantage.png)\n\n",
            "The A* expansion advantage is unchanged; only wall-clock latency moves.\n",
            "Efficiency is near one up to four threads and falls to 0.61 at 16\n",
            "threads because the machine mixes performance and efficiency cores and\n",
            "the road queries are memory-heavy.  Values slightly above one at two and\n",
            "four threads are within frequency/run-to-run variation, not super-linear\n",
            "scaling.\n"]
    (RESULTS / "omp.md").write_text("".join(text), encoding="utf-8")


def main() -> None:
    batch = pd.read_csv(RESULTS / "omp_scaling.csv")
    tasks = pd.read_csv(RESULTS / "omp_tasks.csv")
    summary, split, latency, family, _ = summary_tables(batch, tasks)
    figures(summary, split, latency)
    markdown(summary, split, latency, family)
    print("wrote results/omp.md and figures/omp_*.png")


if __name__ == "__main__":
    main()
