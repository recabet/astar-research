#!/usr/bin/env python3
"""Real-graph tie-break analysis: node_id versus larger_g under ALT K=8.

The frozen main run already contains both treatments on the same 30 road
queries: `astar_alt8` (larger_g tie-break, the main variant) and
`astar_alt8_node_id` (node_id tie-break).  This script pairs them per query,
takes medians over the five repeats, and reports how much the tie-break costs.

Reads:
    results/real.csv

Writes:
    results/real_tie_break.md
    results/summary_real_tie_break.csv
    results/paired_real_tie_break.csv
    figures/real_tie_break.png
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

BASE_KEYS = ["experiment", "graph_id", "family", "width", "pair_id", "band",
             "source", "target"]
BANDS = ["short", "medium", "long"]
COLORS = {"short": "#4c72b0", "medium": "#dd8452", "long": "#55a868"}
MAIN = "astar_alt8"
NODE_ID = "astar_alt8_node_id"


def load() -> pd.DataFrame:
    path = RESULTS / "real.csv"
    if not path.exists():
        raise SystemExit(f"missing {path}")
    data = pd.read_csv(path)
    data = data[data["experiment"] == "real"]
    data = data[data["variant"].isin([MAIN, NODE_ID])].copy()
    if data.empty:
        raise SystemExit("real.csv has no ALT K=8 tie-break treatments")
    bad = data[(data["status"] != "ok") | (data["correct"] != 1)]
    if not bad.empty:
        raise SystemExit(f"{len(bad)} unsuccessful/incorrect tie-break rows")
    return data


def per_query(data: pd.DataFrame) -> pd.DataFrame:
    keys = BASE_KEYS + ["variant"]
    columns = ["time_ns", "distance", "expanded", "pushed", "popped",
               "stale_popped", "relaxed_edges", "improved", "heuristic_calls",
               "heap_peak"]
    return data.groupby(keys)[columns].median().reset_index()


def pair(query: pd.DataFrame) -> pd.DataFrame:
    main = query[query["variant"] == MAIN].drop(columns=["variant"])
    node = query[query["variant"] == NODE_ID].drop(columns=["variant"])
    merged = node.merge(main, on=BASE_KEYS, suffixes=("_node_id", "_larger_g"),
                        validate="one_to_one")
    if len(merged) != main.shape[0] or len(merged) != node.shape[0]:
        raise SystemExit("tie-break pairing lost queries")
    if not (merged["distance_node_id"] == merged["distance_larger_g"]).all():
        raise SystemExit("tie-break treatments returned different distances")
    merged["exp_ratio"] = (
        merged["expanded_node_id"] / merged["expanded_larger_g"])
    merged["time_ratio"] = merged["time_ns_node_id"] / merged["time_ns_larger_g"]
    merged["exp_overhead_pct"] = (merged["exp_ratio"] - 1.0) * 100.0
    merged["time_overhead_pct"] = (merged["time_ratio"] - 1.0) * 100.0
    merged["exp_worse"] = merged["exp_ratio"] > 1.0
    merged["time_worse"] = merged["time_ratio"] > 1.0
    return merged


def bootstrap_median_ci(values: np.ndarray, rng: np.random.Generator,
                        draws: int = 10000) -> tuple[float, float]:
    values = np.asarray(values, dtype=float)
    if len(values) == 0:
        return (math.nan, math.nan)
    indices = rng.integers(0, len(values), size=(draws, len(values)))
    medians = np.median(values[indices], axis=1)
    return (float(np.percentile(medians, 2.5)),
            float(np.percentile(medians, 97.5)))


def wilcoxon_p(a: np.ndarray, b: np.ndarray) -> float:
    diff = np.asarray(a, dtype=float) - np.asarray(b, dtype=float)
    if np.allclose(diff, 0.0):
        return 1.0
    try:
        return float(stats.wilcoxon(diff, alternative="two-sided").pvalue)
    except ValueError:
        return 1.0


def summarize(group: pd.DataFrame, scope: str, rng: np.random.Generator) -> dict:
    exp_lo, exp_hi = bootstrap_median_ci(group["exp_ratio"].to_numpy(), rng)
    time_lo, time_hi = bootstrap_median_ci(group["time_ratio"].to_numpy(), rng)
    return {
        "scope": scope,
        "n": len(group),
        "exp_ratio_median": group["exp_ratio"].median(),
        "exp_ratio_ci_low": exp_lo,
        "exp_ratio_ci_high": exp_hi,
        "exp_ratio_q1": group["exp_ratio"].quantile(0.25),
        "exp_ratio_q3": group["exp_ratio"].quantile(0.75),
        "exp_ratio_max": group["exp_ratio"].max(),
        "exp_worse_fraction": group["exp_worse"].mean(),
        "exp_wins": int((group["exp_ratio"] < 1.0).sum()),
        "exp_losses": int((group["exp_ratio"] > 1.0).sum()),
        "exp_ties": int((group["exp_ratio"] == 1.0).sum()),
        "exp_p": wilcoxon_p(group["expanded_node_id"],
                            group["expanded_larger_g"]),
        "time_ratio_median": group["time_ratio"].median(),
        "time_ratio_ci_low": time_lo,
        "time_ratio_ci_high": time_hi,
        "time_ratio_q1": group["time_ratio"].quantile(0.25),
        "time_ratio_q3": group["time_ratio"].quantile(0.75),
        "time_ratio_max": group["time_ratio"].max(),
        "time_worse_fraction": group["time_worse"].mean(),
        "time_wins": int((group["time_ratio"] < 1.0).sum()),
        "time_losses": int((group["time_ratio"] > 1.0).sum()),
        "time_ties": int((group["time_ratio"] == 1.0).sum()),
        "time_p": wilcoxon_p(group["time_ns_node_id"],
                             group["time_ns_larger_g"]),
    }


def markdown_table(df: pd.DataFrame, digits: int = 3) -> str:
    lines = ["| " + " | ".join(df.columns) + " |",
             "|" + "|".join(["---"] * len(df.columns)) + "|"]
    for _, row in df.iterrows():
        cells = []
        for column in df.columns:
            value = row[column]
            if isinstance(value, (float, np.floating)):
                if np.isnan(value):
                    cells.append("-")
                else:
                    cells.append(f"{value:.{digits}f}")
            elif isinstance(value, (int, np.integer)):
                cells.append(str(int(value)))
            else:
                cells.append(str(value))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def plot(pairs: pd.DataFrame, overall: pd.Series) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.8))
    for axis, metric, median_column, title in (
        (axes[0], "exp_ratio", "exp_ratio_median", "Expansions"),
        (axes[1], "time_ratio", "time_ratio_median", "Wall-clock time"),
    ):
        ordered = pairs.sort_values(metric).reset_index(drop=True)
        colors = [COLORS[band] for band in ordered["band"]]
        axis.bar(ordered.index, ordered[metric], color=colors, width=0.8)
        axis.axhline(1.0, color="black", linewidth=0.8, linestyle="--")
        axis.axhline(overall[median_column], color="black", linewidth=1.0,
                     linestyle=":", label=f"median {overall[median_column]:.2f}x")
        axis.set_yscale("log")
        axis.set_ylim(0.55, 2.9)
        axis.set_yticks([0.6, 0.8, 1.0, 1.25, 1.5, 2.0, 2.5])
        axis.set_yticklabels(["0.6", "0.8", "1.0", "1.25", "1.5", "2.0", "2.5"])
        axis.minorticks_off()
        axis.set_xlabel("road query (sorted)")
        axis.set_ylabel("node_id / larger_g  (log scale)")
        axis.set_title(title)
        axis.grid(axis="y", alpha=0.25)
        axis.legend(fontsize=8)
    handles = [plt.Rectangle((0, 0), 1, 1, color=COLORS[b]) for b in BANDS]
    handles.append(plt.Line2D([0], [0], color="black", linestyle=":",
                              label="median ratio"))
    labels = [b for b in BANDS] + ["median ratio"]
    axes[0].legend(handles=handles, labels=labels, fontsize=7, title="band",
                   loc="upper left")
    fig.suptitle("Real road graph, ALT K=8: node-id tie-break loses to larger-g")
    fig.tight_layout()
    fig.savefig(FIGURES / "real_tie_break.png", dpi=180)
    plt.close(fig)


def main() -> None:
    rng = np.random.default_rng(20261030)
    data = load()
    query = per_query(data)
    pairs = pair(query)

    rows = [summarize(pairs, "overall", rng)]
    for band in BANDS:
        rows.append(summarize(pairs[pairs["band"] == band], band, rng))
    summary = pd.DataFrame(rows)

    export = summary[[
        "scope", "n", "exp_ratio_median", "exp_ratio_ci_low",
        "exp_ratio_ci_high", "exp_ratio_q1", "exp_ratio_q3", "exp_ratio_max",
        "exp_worse_fraction", "exp_wins", "exp_losses", "exp_ties", "exp_p",
        "time_ratio_median", "time_ratio_ci_low", "time_ratio_ci_high",
        "time_ratio_q1", "time_ratio_q3", "time_ratio_max",
        "time_worse_fraction", "time_wins", "time_losses", "time_ties",
        "time_p"]]
    export.to_csv(RESULTS / "summary_real_tie_break.csv", index=False)
    pairs.to_csv(RESULTS / "paired_real_tie_break.csv", index=False)
    plot(pairs, summary.iloc[0])

    overall = summary.iloc[0]
    display = export.copy()
    display.columns = [
        "scope", "n", "exp median", "exp CI low", "exp CI high", "exp Q1",
        "exp Q3", "exp max", "exp worse", "exp wins", "exp losses",
        "exp ties", "exp p", "time median", "time CI low", "time CI high",
        "time Q1", "time Q3", "time max", "time worse", "time wins",
        "time losses", "time ties", "time p"]
    for column in ("exp CI low", "exp CI high", "time CI low", "time CI high"):
        display[column] = display[column].map(
            lambda value: f"{value:.3f}" if pd.notna(value) else "-")
    for column in ("exp p", "time p"):
        display[column] = display[column].map(lambda value: f"{value:.2e}")

    lines = []
    lines.append("# Real-graph tie-break: node_id versus larger_g\n")
    lines.append(
        "This is exploratory post-freeze analysis of the frozen "
        "`results/real.csv`.  Both treatments use ALT with K=8 on the same 30 "
        "road queries and differ only in the tie-break: `astar_alt8` breaks "
        "equal f-values toward larger g, `astar_alt8_node_id` toward the "
        "smaller vertex id.  Each value is a per-query median over five "
        "repeats; ratios above 1 mean the node-id rule expanded more vertices "
        "or took longer.  The returned path cost is identical on every query.\n")
    lines.append("## Summary\n")
    lines.append(markdown_table(display))
    lines.append(
        "The error bars are 95% bootstrap intervals for the median ratio "
        "(10,000 resamples); the p-values are two-sided paired Wilcoxon tests "
        "on the raw counts/times.\n")
    lines.append("## Paired detail\n")
    detail = pairs[["pair_id", "band", "source", "target",
                    "expanded_larger_g", "expanded_node_id", "exp_ratio",
                    "time_ns_larger_g", "time_ns_node_id", "time_ratio"]].copy()
    detail["time_ms_larger_g"] = detail["time_ns_larger_g"] / 1e6
    detail["time_ms_node_id"] = detail["time_ns_node_id"] / 1e6
    detail = detail.drop(columns=["time_ns_larger_g", "time_ns_node_id"])
    detail["band"] = pd.Categorical(detail["band"], categories=BANDS,
                                    ordered=True)
    detail = detail.sort_values(["band", "pair_id"])
    detail.columns = ["pair", "band", "source", "target", "exp larger_g",
                      "exp node_id", "exp ratio", "time larger_g (ms)",
                      "time node_id (ms)", "time ratio"]
    lines.append(markdown_table(detail, 3))
    lines.append("![Real-graph tie-break](../figures/real_tie_break.png)\n")
    lines.append("## Reading\n")
    exp_pct = (overall["exp_ratio_median"] - 1) * 100
    time_pct = (overall["time_ratio_median"] - 1) * 100
    lines.append(
        f"**The node-id tie-break is worse, not neutral.**  Across the 30 "
        f"queries it expands a median {overall['exp_ratio_median']:.2f}x as "
        f"many vertices (+{exp_pct:.1f}%) and takes "
        f"{overall['time_ratio_median']:.2f}x the wall-clock time "
        f"(+{time_pct:.1f}%).  It is worse on "
        f"{int(overall['exp_losses'])}/{int(overall['n'])} queries by "
        f"expansions and on {int(overall['time_losses'])}/{int(overall['n'])} "
        f"by time.  The expansion difference is decisive (paired Wilcoxon "
        f"p={overall['exp_p']:.3g}); the time difference points the same way "
        f"but is noisy (p={overall['time_p']:.3g}), so expansions are the "
        "more reliable measure here.\n")
    lines.append(
        "**The tail is the risk, not the median.**  The largest expansion "
        f"ratio is {overall['exp_ratio_max']:.2f}x and the largest time ratio "
        f"is {overall['time_ratio_max']:.2f}x.  A tie-break rule is a cheap "
        "change, but on a road graph with many equal f-values it can multiply "
        "the work on individual queries.\n")
    lines.append(
        "**Why larger_g helps here.**  When several frontier vertices share "
        "the same f-value, preferring the one with larger g means preferring "
        "the vertex closer to the target.  That tends to follow a single "
        "promising route instead of fanning out across plateau regions, which "
        "is exactly the tie pattern produced by a strong ALT bound.\n")
    lines.append(
        "This confirms the grid result on the real graph: the tie-break is not "
        "a cosmetic detail.  The main benchmark's `larger_g` choice is "
        "justified empirically, and a node-id rule should not be used as the "
        "default.\n")
    lines.append("## Caveats\n")
    lines.append(
        "- The analysis reuses the frozen main run; no new timing data were "
        "collected, and `results/real.csv` is unchanged.\n"
        "- Wall-clock noise is larger than expansion-count noise, so the time "
        "ratios are the less stable of the two measures.\n"
        "- There are only 30 queries (10 per distance band); the bootstrap "
        "interval describes this query set, not all possible road queries.\n")
    (RESULTS / "real_tie_break.md").write_text("\n".join(lines))

    print(f"wrote {RESULTS / 'real_tie_break.md'}")
    print(f"wrote {RESULTS / 'summary_real_tie_break.csv'}")
    print(f"wrote {RESULTS / 'paired_real_tie_break.csv'}")
    print(f"wrote {FIGURES / 'real_tie_break.png'}")


if __name__ == "__main__":
    main()
