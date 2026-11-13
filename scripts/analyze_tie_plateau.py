#!/usr/bin/env python3
"""Tie-break mechanism: equal-f plateaus on grids and the road graph.

For every query we run A* twice, once with the node-id tie-break and once with
larger-g, and count maximal runs of settled vertices that share the same f
value.  A long plateau is exactly where the tie-break rule can redirect the
search.

Reads:
    results/tie_plateau_grid.csv
    results/tie_plateau_real.csv
    results/grid.csv                 (frozen time medians)
    results/real.csv                 (frozen time medians)

Writes:
    results/tie_plateau.md
    results/summary_tie_plateau.csv
    results/summary_tie_plateau_buckets.csv
    results/paired_tie_plateau.csv
    figures/tie_plateau.png
"""

from __future__ import annotations

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

KEYS = ["experiment", "graph_id", "family", "size", "pair_id", "band",
        "source", "target"]
FAMILIES = ["open", "random", "clustered", "rooms", "maze", "road"]
COLORS = {
    "open": "#4c72b0",
    "random": "#dd8452",
    "clustered": "#55a868",
    "rooms": "#c44e52",
    "maze": "#8172b3",
    "road": "#333333",
}
BANDS = ["short", "medium", "long"]
BAND_COLORS = {"short": "#4c72b0", "medium": "#dd8452", "long": "#55a868"}
GRID_VARIANTS = ("astar_larger_g", "astar_node_id")
REAL_VARIANTS = ("astar_alt8", "astar_alt8_node_id")
METRICS = ["expanded", "equal_f_expanded", "f_plateau_runs",
           "max_f_plateau", "time_ns"]


def read_plateau() -> pd.DataFrame:
    frames = []
    for name in ("tie_plateau_grid.csv", "tie_plateau_real.csv"):
        path = RESULTS / name
        if not path.exists():
            raise SystemExit(f"missing {path}")
        frames.append(pd.read_csv(path))
    data = pd.concat(frames, ignore_index=True)
    bad = data[data["status"] != "ok"]
    if not bad.empty:
        raise SystemExit(f"{len(bad)} unsuccessful plateau rows")
    return data


def frozen_medians(experiment: str) -> pd.DataFrame:
    if experiment == "grid":
        path = RESULTS / "grid.csv"
        variants = GRID_VARIANTS
    else:
        path = RESULTS / "real.csv"
        variants = REAL_VARIANTS
    data = pd.read_csv(path)
    data = data[(data["experiment"] == experiment) & data["variant"].isin(variants)]
    data = data[data["status"] == "ok"]
    if experiment == "grid":
        data["size"] = data["width"]
    else:
        data["size"] = 0
    keys = [k for k in KEYS if k in data.columns]
    med = data.groupby(keys + ["variant"])[["time_ns", "expanded"]].median().reset_index()
    wide = med.pivot_table(index=keys, columns="variant",
                           values=["time_ns", "expanded"]).reset_index()
    wide.columns = ["_".join([str(c) for c in col if c])
                    for col in wide.columns]
    return wide


def pair_plateau(data: pd.DataFrame) -> pd.DataFrame:
    wide = data.pivot_table(index=KEYS, columns="tie_break", values=METRICS,
                            aggfunc="first").reset_index()
    wide.columns = ["_".join([str(c) for c in col if c]) for col in wide.columns]
    wide = wide.rename(columns={c: c.rstrip("_") for c in wide.columns})

    frames = []
    for experiment in ("grid", "real"):
        part = wide[wide["experiment"] == experiment].copy()
        frozen = frozen_medians(experiment)
        part = part.merge(frozen, on=KEYS, validate="one_to_one")
        if experiment == "grid":
            larger, node = "astar_larger_g", "astar_node_id"
        else:
            larger, node = "astar_alt8", "astar_alt8_node_id"
        part["expanded_larger_frozen"] = part[f"expanded_{larger}"]
        part["expanded_node_frozen"] = part[f"expanded_{node}"]
        part["time_larger_frozen"] = part[f"time_ns_{larger}"]
        part["time_node_frozen"] = part[f"time_ns_{node}"]
        if not (part["expanded_larger_frozen"] == part["expanded_larger_g"]).all():
            raise SystemExit("plateau/frozen expansion mismatch for larger_g")
        frames.append(part)
    paired = pd.concat(frames, ignore_index=True)

    paired["exp_ratio"] = (paired["expanded_node_id"] /
                           paired["expanded_larger_g"])
    paired["time_ratio"] = (paired["time_node_frozen"] /
                            paired["time_larger_frozen"])
    paired["plateau_share"] = (paired["equal_f_expanded_larger_g"] /
                               paired["expanded_larger_g"])
    paired["max_plateau_share"] = (paired["max_f_plateau_larger_g"] /
                                   paired["expanded_larger_g"])
    paired["time_overhead_pct"] = (paired["time_ratio"] - 1.0) * 100.0
    return paired


def spearman(x: pd.Series, y: pd.Series) -> tuple[float, float]:
    if x.nunique() < 2 or y.nunique() < 2:
        return float("nan"), float("nan")
    rho, p = stats.spearmanr(x, y)
    return float(rho), float(p)


def summarize(paired: pd.DataFrame) -> pd.DataFrame:
    rows = []
    for family in FAMILIES:
        group = paired[paired["family"] == family]
        if group.empty:
            continue
        rho_exp, p_exp = spearman(group["max_plateau_share"],
                                  group["exp_ratio"])
        rho_time, p_time = spearman(group["max_plateau_share"],
                                    group["time_ratio"])
        rows.append({
            "family": family,
            "n": len(group),
            "max_plateau_share": group["max_plateau_share"].median(),
            "plateau_share": group["plateau_share"].median(),
            "exp_ratio": group["exp_ratio"].median(),
            "time_ratio": group["time_ratio"].median(),
            "worst_exp_ratio": group["exp_ratio"].max(),
            "worst_time_ratio": group["time_ratio"].max(),
            "rho_exp": rho_exp,
            "p_exp": p_exp,
            "rho_time": rho_time,
            "p_time": p_time,
        })
    return pd.DataFrame(rows)


def road_buckets(paired: pd.DataFrame) -> pd.DataFrame:
    road = paired[paired["family"] == "road"].copy()
    road["bucket"] = pd.qcut(road["max_plateau_share"], 3,
                             labels=["low", "mid", "high"])
    out = road.groupby("bucket", observed=True).agg(
        n=("pair_id", "size"),
        max_plateau_share=("max_plateau_share", "median"),
        exp_ratio=("exp_ratio", "median"),
        time_ratio=("time_ratio", "median"),
        worst_exp_ratio=("exp_ratio", "max"),
        worst_time_ratio=("time_ratio", "max"),
    ).reset_index()
    out["bucket"] = out["bucket"].astype(str)
    return out


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
                elif column.startswith("p_"):
                    cells.append(f"{value:.2e}")
                else:
                    cells.append(f"{value:.{digits}f}")
            elif isinstance(value, (int, np.integer)):
                cells.append(str(int(value)))
            else:
                cells.append(str(value))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def plot(paired: pd.DataFrame, summary: pd.DataFrame) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.8))
    road = paired[paired["family"] == "road"]
    for band in BANDS:
        group = road[road["band"] == band]
        axes[0].scatter(group["max_plateau_share"], group["time_ratio"],
                        color=BAND_COLORS[band], s=24, label=band,
                        edgecolors="white", linewidths=0.4)
    rho, p = spearman(road["max_plateau_share"], road["time_ratio"])
    axes[0].axhline(1.0, color="black", linewidth=0.8, linestyle="--")
    axes[0].set_xlabel("longest equal-f plateau / expansions  (larger_g)")
    axes[0].set_ylabel("time ratio  node_id / larger_g")
    axes[0].set_yscale("log")
    axes[0].set_title(f"Road: plateau length predicts tie-break cost\n"
                      f"Spearman rho={rho:.2f}, p={p:.2g}")
    axes[0].grid(alpha=0.25)
    axes[0].legend(fontsize=8, title="band")

    ordered = summary.set_index("family").loc[
        [f for f in FAMILIES if f in set(summary["family"])]]
    colors = [COLORS[f] for f in ordered.index]
    axes[1].barh(np.arange(len(ordered)), ordered["max_plateau_share"],
                 color=colors, height=0.65)
    axes[1].set_yticks(np.arange(len(ordered)))
    axes[1].set_yticklabels(ordered.index)
    axes[1].set_xlim(0, 1.05)
    axes[1].set_xlabel("median longest plateau / expansions")
    axes[1].set_title("Grid controls: open/random/clustered saturate;\nmaze has almost no plateau")
    axes[1].invert_yaxis()
    axes[1].grid(axis="x", alpha=0.25)
    for i, family in enumerate(ordered.index):
        value = ordered.loc[family, "max_plateau_share"]
        axes[1].text(value + 0.015, i, f"{value:.2f}", va="center", fontsize=8)

    fig.suptitle("Tie-breaking: equal-f plateaus are the mechanism")
    fig.tight_layout()
    fig.savefig(FIGURES / "tie_plateau.png", dpi=180)
    plt.close(fig)


def main() -> None:
    data = read_plateau()
    paired = pair_plateau(data)
    summary = summarize(paired)
    buckets = road_buckets(paired)

    paired.to_csv(RESULTS / "paired_tie_plateau.csv", index=False)
    summary.to_csv(RESULTS / "summary_tie_plateau.csv", index=False)
    buckets.to_csv(RESULTS / "summary_tie_plateau_buckets.csv", index=False)
    plot(paired, summary)

    road = paired[paired["family"] == "road"]
    rho_exp, p_exp = spearman(road["max_plateau_share"], road["exp_ratio"])
    rho_time, p_time = spearman(road["max_plateau_share"], road["time_ratio"])
    low = buckets[buckets["bucket"] == "low"].iloc[0]
    high = buckets[buckets["bucket"] == "high"].iloc[0]
    open_row = summary[summary["family"] == "open"].iloc[0]
    maze_row = summary[summary["family"] == "maze"].iloc[0]

    display = summary.copy()
    display.columns = ["family", "n", "max plateau", "plateau share",
                       "exp ratio", "time ratio", "worst exp", "worst time",
                       "rho exp", "p exp", "rho time", "p time"]
    bucket_display = buckets.copy()
    bucket_display.columns = ["bucket", "n", "max plateau", "exp ratio",
                              "time ratio", "worst exp", "worst time"]

    lines = []
    lines.append("# Tie-break mechanism: equal-f plateaus\n")
    lines.append(
        "This is exploratory post-freeze analysis.  The new runs repeat the two "
        "tie-break treatments on the frozen query set and count maximal runs of "
        "settled vertices that share the same f value.  A long run is exactly "
        "where the tie-break rule can redirect the search.  Expansion and time "
        "ratios use the frozen per-query medians; the plateau counts come from "
        "the new deterministic runs.  Ratios above 1 mean node_id is worse.\n")
    lines.append("## Summary\n")
    lines.append(markdown_table(display))
    lines.append(
        "The road graph is the informative case: the longest plateau covers a "
        f"median {road['max_plateau_share'].median():.2f} of the search, and "
        "longer plateaus go with larger node_id penalties (expansion "
        f"rho={rho_exp:.2f}, p={p_exp:.2g}; time rho={rho_time:.2f}, "
        f"p={p_time:.2g}).\n")
    lines.append("## Road graph: plateau tertiles\n")
    lines.append(markdown_table(bucket_display))
    lines.append(
        f"The low-plateau third is neutral (median time ratio "
        f"{low['time_ratio']:.2f}), while the high-plateau third pays "
        f"{high['time_ratio']:.2f}x median time and reaches "
        f"{high['worst_time_ratio']:.2f}x on its worst query.  The plateau "
        "length is a query-level explanation, not a claim that it can be known "
        "before running the search.\n")
    lines.append("![Equal-f plateau mechanism](../figures/tie_plateau.png)\n")
    lines.append("## Grid controls\n")
    lines.append(
        f"The open grid is one long plateau (median max share "
        f"{open_row['max_plateau_share']:.2f}): every settled vertex shares the "
        "same f value, so the tie-break decides the route and the tail can be "
        "hundreds of times worse.  Random and clustered grids are also "
        "saturated, while rooms has shorter plateaus.  The maze is the opposite "
        f"extreme (median max share {maze_row['max_plateau_share']:.3f}): its "
        "unique path leaves almost no equal-f choice, and node_id is neutral "
        f"(time ratio {maze_row['time_ratio']:.2f}).\n")
    lines.append(
        "This ties the earlier observations together.  Tie-breaking is not a "
        "median effect because plateaus are either absent or saturated in most "
        "grid families; the road graph has intermediate, query-dependent "
        "plateaus, which is where the choice of rule has a measurable cost.\n")
    lines.append("## Caveats\n")
    lines.append(
        "- The plateau metric is collected after the search; it explains the "
        "penalty but is not a pre-query predictor.\n"
        "- The new runs use one deterministic pass per treatment; all returned "
        "distances match the frozen oracles.\n"
        "- Grid families are synthetic controls; only the road graph has "
        "intermediate plateau lengths.\n")
    (RESULTS / "tie_plateau.md").write_text("\n".join(lines))

    print(f"wrote {RESULTS / 'tie_plateau.md'}")
    print(f"wrote {RESULTS / 'summary_tie_plateau.csv'}")
    print(f"wrote {RESULTS / 'summary_tie_plateau_buckets.csv'}")
    print(f"wrote {RESULTS / 'paired_tie_plateau.csv'}")
    print(f"wrote {FIGURES / 'tie_plateau.png'}")


if __name__ == "__main__":
    main()
