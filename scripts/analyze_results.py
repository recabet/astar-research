#!/usr/bin/env python3
"""Paired analysis of the A* / Dijkstra experiments.

The script reads the raw CSV files, forms per-query medians over repeats, pairs
every A* run with the Dijkstra run on the same graph and query, and writes
summary tables, confidence intervals and figures.  It deliberately does not
average away the query-level distribution.
"""

from __future__ import annotations

import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"
FIGURES = ROOT / "figures"
FIGURES.mkdir(exist_ok=True)

BASE_KEYS = [
    "experiment", "graph_id", "family", "width", "pair_id", "band",
    "source", "target",
]
VALUE_COLUMNS = [
    "time_ns", "expanded", "pushed", "popped", "stale_popped",
    "relaxed_edges", "improved", "heuristic_calls", "heap_peak",
]


def read_results() -> pd.DataFrame:
    frames = []
    for name in ("grid.csv", "real.csv"):
        path = RESULTS / name
        if path.exists():
            frames.append(pd.read_csv(path))
    if not frames:
        raise SystemExit("no results/grid.csv or results/real.csv found")
    data = pd.concat(frames, ignore_index=True)
    data["family"] = data["family"].astype(str)
    data["band"] = data["band"].astype(str)
    data["variant"] = data["variant"].astype(str)
    data["size"] = data["width"].replace(0, np.nan)
    return data


def check_correctness(data: pd.DataFrame) -> pd.DataFrame:
    bad = data[(data["status"] != "ok") | (data["correct"] != 1)]
    if bad.empty:
        print("correctness: all recorded runs have status=ok and match the BFS oracle")
    else:
        print(f"correctness: {len(bad)} non-ok/incorrect rows (reported, not dropped silently)")
        print(bad.groupby(["variant", "status"]).size().to_string())
    return data


def medians_over_repeats(data: pd.DataFrame) -> pd.DataFrame:
    keys = BASE_KEYS + ["variant", "algorithm", "tie_break", "k"]
    grouped = (
        data.groupby(keys, dropna=False)[VALUE_COLUMNS]
        .median()
        .reset_index()
    )
    # `k` is a treatment property, not a numeric grouping key to aggregate.
    return grouped


def pair_with_dijkstra(per_query: pd.DataFrame, variant: str) -> pd.DataFrame:
    di = (
        per_query[per_query["variant"] == "dijkstra"][BASE_KEYS + VALUE_COLUMNS]
        .rename(columns={c: f"{c}_d" for c in VALUE_COLUMNS})
    )
    a = (
        per_query[per_query["variant"] == variant][
            BASE_KEYS + VALUE_COLUMNS + ["variant", "k", "tie_break"]]
        .rename(columns={c: f"{c}_a" for c in VALUE_COLUMNS})
    )
    merged = di.merge(a, on=BASE_KEYS, how="inner", validate="one_to_one")
    merged["size"] = merged["width"].replace(0, np.nan)
    merged["time_ratio"] = merged["time_ns_d"] / merged["time_ns_a"]
    merged["exp_ratio"] = merged["expanded_d"] / merged["expanded_a"]
    merged["time_saving_pct"] = 100.0 * (1.0 - merged["time_ns_a"] / merged["time_ns_d"])
    merged["exp_saving_pct"] = 100.0 * (1.0 - merged["expanded_a"] / merged["expanded_d"])
    merged["a_faster"] = merged["time_ns_a"] < merged["time_ns_d"]
    merged["a_fewer"] = merged["expanded_a"] < merged["expanded_d"]
    merged["time_ms_d"] = merged["time_ns_d"] / 1e6
    merged["time_ms_a"] = merged["time_ns_a"] / 1e6
    return merged


def bootstrap_median_ci(values: np.ndarray, rng: np.random.Generator,
                        draws: int = 10000) -> tuple[float, float]:
    values = np.asarray(values, dtype=float)
    if len(values) == 0:
        return (math.nan, math.nan)
    indices = rng.integers(0, len(values), size=(draws, len(values)))
    medians = np.median(values[indices], axis=1)
    return (float(np.percentile(medians, 2.5)), float(np.percentile(medians, 97.5)))


def wilcoxon_p(a: np.ndarray, b: np.ndarray) -> float:
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    diff = a - b
    if np.allclose(diff, 0.0):
        return 1.0
    try:
        return float(stats.wilcoxon(diff, zero_method="wilcox", alternative="two-sided").pvalue)
    except ValueError:
        return 1.0


def summarize(group: pd.DataFrame, rng: np.random.Generator) -> pd.Series:
    time_lo, time_hi = bootstrap_median_ci(group["time_ratio"].to_numpy(), rng)
    exp_lo, exp_hi = bootstrap_median_ci(group["exp_ratio"].to_numpy(), rng)
    return pd.Series({
        "n_pairs": len(group),
        "time_ratio_median": group["time_ratio"].median(),
        "time_ratio_q1": group["time_ratio"].quantile(0.25),
        "time_ratio_q3": group["time_ratio"].quantile(0.75),
        "time_ratio_ci_lo": time_lo,
        "time_ratio_ci_hi": time_hi,
        "exp_ratio_median": group["exp_ratio"].median(),
        "exp_ratio_q1": group["exp_ratio"].quantile(0.25),
        "exp_ratio_q3": group["exp_ratio"].quantile(0.75),
        "exp_ratio_ci_lo": exp_lo,
        "exp_ratio_ci_hi": exp_hi,
        "time_saving_pct_median": group["time_saving_pct"].median(),
        "exp_saving_pct_median": group["exp_saving_pct"].median(),
        "a_faster_fraction": group["a_faster"].mean(),
        "a_fewer_fraction": group["a_fewer"].mean(),
        "time_ms_d_median": group["time_ms_d"].median(),
        "time_ms_a_median": group["time_ms_a"].median(),
        "expanded_d_median": group["expanded_d"].median(),
        "expanded_a_median": group["expanded_a"].median(),
        "ns_per_exp_d_median": (group["time_ns_d"] / group["expanded_d"]).median(),
        "ns_per_exp_a_median": (group["time_ns_a"] / group["expanded_a"]).median(),
        "time_ratio_over_exp_ratio": (group["time_ratio"] / group["exp_ratio"]).median(),
        "wilcoxon_time_p": wilcoxon_p(group["time_ns_a"], group["time_ns_d"]),
        "wilcoxon_exp_p": wilcoxon_p(group["expanded_a"], group["expanded_d"]),
    })


def summarize_groups(df: pd.DataFrame, groups: list[str],
                     rng: np.random.Generator) -> pd.DataFrame:
    rows = []
    for key, group in df.groupby(groups, dropna=False):
        if not isinstance(key, tuple):
            key = (key,)
        row = dict(zip(groups, key))
        row.update(summarize(group, rng).to_dict())
        rows.append(row)
    return pd.DataFrame(rows)


def markdown_table(df: pd.DataFrame, digits: int = 2) -> str:
    if df.empty:
        return "_(empty)_\n"
    columns = list(df.columns)
    lines = ["| " + " | ".join(columns) + " |",
             "|" + "|".join(["---"] * len(columns)) + "|"]
    for _, row in df.iterrows():
        cells = []
        for column in columns:
            value = row[column]
            if isinstance(value, (float, np.floating)):
                if math.isnan(value):
                    cells.append("—")
                elif column.endswith("_p"):
                    cells.append(f"{value:.2g}")
                else:
                    cells.append(f"{value:.{digits}f}")
            else:
                cells.append(str(value))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def plot_grid_boxplots(grid_pairs: pd.DataFrame) -> None:
    families = ["open", "random", "clustered", "rooms", "maze"]
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.2))
    for axis, metric, title in (
        (axes[0], "exp_ratio", "Expansion ratio  Dijkstra / A*"),
        (axes[1], "time_ratio", "Time ratio  Dijkstra / A*"),
    ):
        values = [grid_pairs.loc[grid_pairs.family == f, metric].to_numpy()
                  for f in families]
        axis.boxplot(values, tick_labels=families, showfliers=False)
        axis.axhline(1.0, color="black", linewidth=1, linestyle="--")
        axis.set_yscale("log")
        axis.set_title(title)
        axis.set_ylabel("ratio (log scale)")
        axis.grid(axis="y", alpha=0.25)
    fig.suptitle("A* with Manhattan on grids; all sizes and bands pooled")
    fig.tight_layout()
    fig.savefig(FIGURES / "grid_ratios_by_family.png", dpi=180)
    plt.close(fig)


def plot_time_vs_expansion(grid_pairs: pd.DataFrame) -> None:
    families = ["open", "random", "clustered", "rooms", "maze"]
    markers = {"open": "o", "random": "s", "clustered": "^", "rooms": "D", "maze": "x"}
    fig, axis = plt.subplots(figsize=(7.2, 6))
    for family in families:
        group = grid_pairs[grid_pairs.family == family]
        axis.scatter(group["exp_ratio"], group["time_ratio"], s=18,
                     alpha=0.55, marker=markers[family], label=family)
    axis.axhline(1.0, color="black", linewidth=1, linestyle="--")
    axis.axvline(1.0, color="black", linewidth=1, linestyle="--")
    axis.set_xscale("log")
    axis.set_yscale("log")
    axis.set_xlabel("expansion ratio  Dijkstra / A*")
    axis.set_ylabel("time ratio  Dijkstra / A*")
    axis.set_title("Why time savings differ from expansion savings")
    axis.legend(title="grid family")
    axis.grid(alpha=0.25)
    fig.tight_layout()
    fig.savefig(FIGURES / "grid_time_vs_expansion.png", dpi=180)
    plt.close(fig)


def plot_grid_scaling(grid_pairs: pd.DataFrame) -> None:
    families = ["open", "random", "clustered", "rooms", "maze"]
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    for family in families:
        group = grid_pairs[grid_pairs.family == family].copy()
        group = group.groupby("size")[["time_ms_d", "time_ms_a", "expanded_d", "expanded_a"]].median()
        axes[0].plot(group.index, group["time_ms_d"], marker="o", label=f"{family} Dijkstra")
        axes[0].plot(group.index, group["time_ms_a"], marker="s", linestyle="--", label=f"{family} A*")
        axes[1].plot(group.index, group["expanded_d"], marker="o", label=f"{family} Dijkstra")
        axes[1].plot(group.index, group["expanded_a"], marker="s", linestyle="--", label=f"{family} A*")
    for axis, ylabel in ((axes[0], "median time (ms)"), (axes[1], "median expanded vertices")):
        axis.set_xscale("log")
        axis.set_yscale("log")
        axis.set_xlabel("grid side length")
        axis.set_ylabel(ylabel)
        axis.grid(alpha=0.25)
    axes[0].set_title("Runtime scaling")
    axes[1].set_title("Expansion scaling")
    axes[0].legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(FIGURES / "grid_scaling.png", dpi=180)
    plt.close(fig)


def plot_tie_break(tie: pd.DataFrame) -> None:
    families = ["open", "random", "clustered", "rooms", "maze"]
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    for axis, metric, title in (
        (axes[0], "exp_ratio_nodeid_over_largerg", "expansion ratio  node_id / larger_g"),
        (axes[1], "time_ratio_nodeid_over_largerg", "time ratio  node_id / larger_g"),
    ):
        values = [tie.loc[tie.family == f, metric].dropna().to_numpy()
                  for f in families]
        axis.boxplot(values, tick_labels=families, showfliers=False)
        axis.axhline(1.0, color="black", linestyle="--", linewidth=1)
        axis.set_title(title)
        axis.set_ylabel("ratio (log scale)")
        axis.set_yscale("log")
        axis.grid(axis="y", alpha=0.25)
    fig.suptitle("Tie-breaking matters most on the open grid")
    fig.tight_layout()
    fig.savefig(FIGURES / "grid_tie_break.png", dpi=180)
    plt.close(fig)


def plot_real(real_pairs: pd.DataFrame) -> None:
    variants = ["astar_alt1", "astar_alt4", "astar_alt8", "astar_alt16"]
    bands = ["short", "medium", "long"]
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    width = 0.19
    x = np.arange(len(bands))
    for index, variant in enumerate(variants):
        group = real_pairs[real_pairs.variant == variant]
        exp_med = [group.loc[group.band == b, "exp_ratio"].median() for b in bands]
        time_med = [group.loc[group.band == b, "time_ratio"].median() for b in bands]
        axes[0].bar(x + (index - 1.5) * width, exp_med, width, label="K=" + variant.rsplit("alt", 1)[1])
        axes[1].bar(x + (index - 1.5) * width, time_med, width, label="K=" + variant.rsplit("alt", 1)[1])
    for axis, title in ((axes[0], "Expansion ratio  Dijkstra / A*"),
                        (axes[1], "Time ratio  Dijkstra / A*")):
        axis.set_xticks(x, bands)
        axis.set_yscale("log")
        axis.axhline(1.0, color="black", linewidth=1, linestyle="--")
        axis.set_title(title)
        axis.set_xlabel("BFS-distance band")
        axis.grid(axis="y", alpha=0.25)
    axes[0].legend(title="landmarks")
    fig.suptitle("California road network: ALT heuristic strength")
    fig.tight_layout()
    fig.savefig(FIGURES / "real_ratios_by_band_k.png", dpi=180)
    plt.close(fig)


def plot_real_boxplots(real_pairs: pd.DataFrame) -> None:
    variants = ["astar_alt1", "astar_alt4", "astar_alt8", "astar_alt16"]
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    for axis, metric, title in (
        (axes[0], "exp_ratio", "expansion ratio"),
        (axes[1], "time_ratio", "time ratio"),
    ):
        values = [real_pairs.loc[real_pairs.variant == v, metric].to_numpy() for v in variants]
        axis.boxplot(values, tick_labels=[v.replace("astar_", "") for v in variants], showfliers=False)
        axis.set_yscale("log")
        axis.axhline(1.0, color="black", linestyle="--", linewidth=1)
        axis.set_title(f"Real graph: {title}  Dijkstra / A*")
        axis.set_ylabel("ratio (log scale)")
        axis.set_xlabel("ALT landmarks")
        axis.grid(axis="y", alpha=0.25)
    fig.tight_layout()
    fig.savefig(FIGURES / "real_ratios_boxplots.png", dpi=180)
    plt.close(fig)


def main() -> None:
    data = check_correctness(read_results())
    per_query = medians_over_repeats(data)

    rng = np.random.default_rng(20261030)
    grid_pairs = pair_with_dijkstra(per_query[per_query.experiment == "grid"],
                                    "astar_larger_g")
    grid_pairs_node = pair_with_dijkstra(per_query[per_query.experiment == "grid"],
                                         "astar_node_id")

    real_pairs = []
    for variant in ["astar_alt1", "astar_alt4", "astar_alt8", "astar_alt16"]:
        real_pairs.append(pair_with_dijkstra(per_query[per_query.experiment == "real"],
                                             variant))
    real_pairs = pd.concat(real_pairs, ignore_index=True)

    # Main grid summaries.
    grid_family = summarize_groups(grid_pairs, ["family"], rng)
    grid_family_size = summarize_groups(grid_pairs, ["family", "size"], rng)
    grid_family_band = summarize_groups(grid_pairs, ["family", "band"], rng)
    grid_family.to_csv(RESULTS / "summary_grid_family.csv", index=False)
    grid_family_size.to_csv(RESULTS / "summary_grid_family_size.csv", index=False)
    grid_family_band.to_csv(RESULTS / "summary_grid_family_band.csv", index=False)
    grid_pairs.to_csv(RESULTS / "paired_grid.csv", index=False)

    # Tie-break: node_id / larger_g.  A value > 1 means larger_g expanded or
    # spent fewer resources.
    tie = grid_pairs_node.merge(
        grid_pairs, on=BASE_KEYS, suffixes=("_nodeid", "_largerg"),
        validate="one_to_one")
    tie["exp_ratio_nodeid_over_largerg"] = tie["expanded_a_nodeid"] / tie["expanded_a_largerg"]
    tie["time_ratio_nodeid_over_largerg"] = tie["time_ns_a_nodeid"] / tie["time_ns_a_largerg"]
    tie_summary = tie.groupby("family").agg(
        exp_ratio_nodeid_over_largerg_median=("exp_ratio_nodeid_over_largerg", "median"),
        exp_ratio_nodeid_over_largerg_q1=("exp_ratio_nodeid_over_largerg", lambda x: x.quantile(0.25)),
        exp_ratio_nodeid_over_largerg_q3=("exp_ratio_nodeid_over_largerg", lambda x: x.quantile(0.75)),
        time_ratio_nodeid_over_largerg_median=("time_ratio_nodeid_over_largerg", "median"),
        time_ratio_nodeid_over_largerg_q1=("time_ratio_nodeid_over_largerg", lambda x: x.quantile(0.25)),
        time_ratio_nodeid_over_largerg_q3=("time_ratio_nodeid_over_largerg", lambda x: x.quantile(0.75)),
    ).reset_index()
    tie_summary.to_csv(RESULTS / "summary_tie_break.csv", index=False)
    tie.to_csv(RESULTS / "paired_tie_break.csv", index=False)

    # Real graph summaries.
    real_band = summarize_groups(real_pairs, ["band", "variant"], rng)
    real_variant = summarize_groups(real_pairs, ["variant"], rng)
    real_band.to_csv(RESULTS / "summary_real_band.csv", index=False)
    real_variant.to_csv(RESULTS / "summary_real_variant.csv", index=False)
    real_pairs.to_csv(RESULTS / "paired_real.csv", index=False)

    plot_grid_boxplots(grid_pairs)
    plot_time_vs_expansion(grid_pairs)
    plot_grid_scaling(grid_pairs)
    plot_tie_break(tie)
    plot_real(real_pairs)
    plot_real_boxplots(real_pairs)

    # Markdown report with compact tables.
    lines = ["# Generated analysis\n",
             "All tables use per-query medians over repeated runs and paired\n",
             "Dijkstra/A* comparisons.  Ratio = Dijkstra / A*; values above 1 mean\n",
             "A* wins.  Time is wall-clock time around the search call.\n\n",
             "## Correctness\n\n"]
    bad = data[(data["status"] != "ok") | (data["correct"] != 1)]
    lines.append(f"Rows: {len(data)}; non-ok/incorrect rows: {len(bad)}.\n\n")
    if not bad.empty:
        lines.append(markdown_table(
            bad.groupby(["variant", "status"]).size().reset_index(name="rows")))
    lines.append("## Grid: main treatment (Manhattan, larger_g)\n\n")
    cols = ["family", "n_pairs", "time_ratio_median", "time_ratio_q1",
            "time_ratio_q3", "exp_ratio_median", "exp_ratio_q1", "exp_ratio_q3",
            "time_saving_pct_median", "exp_saving_pct_median",
            "a_faster_fraction", "a_fewer_fraction",
            "ns_per_exp_d_median", "ns_per_exp_a_median",
            "time_ratio_over_exp_ratio"]
    lines.append(markdown_table(grid_family[cols]))
    lines.append("\n## Grid by family and size\n\n")
    cols2 = ["family", "size", "n_pairs", "time_ratio_median", "exp_ratio_median",
             "a_faster_fraction", "time_ms_d_median", "time_ms_a_median"]
    lines.append(markdown_table(grid_family_size[cols2]))
    lines.append("\n## Grid by family and distance band\n\n")
    cols3 = ["family", "band", "n_pairs", "time_ratio_median", "exp_ratio_median",
             "a_faster_fraction"]
    lines.append(markdown_table(grid_family_band[cols3]))
    lines.append("\n## Tie-break (node_id / larger_g)\n\n")
    lines.append(markdown_table(tie_summary))
    lines.append("\n## Real graph\n\n")
    cols4 = ["band", "variant", "n_pairs", "time_ratio_median", "time_ratio_q1",
             "time_ratio_q3", "exp_ratio_median", "exp_ratio_q1", "exp_ratio_q3",
             "a_faster_fraction", "time_ms_d_median", "time_ms_a_median"]
    lines.append(markdown_table(real_band[cols4]))
    lines.append("\n## Real graph by K\n\n")
    cols5 = ["variant", "n_pairs", "time_ratio_median", "exp_ratio_median",
             "time_saving_pct_median", "exp_saving_pct_median", "a_faster_fraction"]
    lines.append(markdown_table(real_variant[cols5]))
    lines.append("\n## Figures\n\n")
    lines.append("![Grid ratios](../figures/grid_ratios_by_family.png)\n\n")
    lines.append("![Time vs expansions](../figures/grid_time_vs_expansion.png)\n\n")
    lines.append("![Scaling](../figures/grid_scaling.png)\n\n")
    lines.append("![Tie-break](../figures/grid_tie_break.png)\n\n")
    lines.append("![Real graph](../figures/real_ratios_by_band_k.png)\n\n")
    (RESULTS / "analysis.md").write_text("".join(lines), encoding="utf-8")
    print(f"wrote {RESULTS / 'analysis.md'} and figures in {FIGURES}")


if __name__ == "__main__":
    main()
