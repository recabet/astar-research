#!/usr/bin/env python3
"""Novelty analysis: heuristic informativeness and the price of optimality."""

from __future__ import annotations

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy.stats import spearmanr, wilcoxon

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"
FIGURES = ROOT / "figures"
FIGURES.mkdir(exist_ok=True)


def r2_linear(df: pd.DataFrame, x: str, y: str) -> tuple[float, np.ndarray]:
    values = df[[x, y]].dropna()
    X = np.column_stack([np.ones(len(values)), values[x].to_numpy()])
    beta = np.linalg.lstsq(X, values[y].to_numpy(), rcond=None)[0]
    pred = X @ beta
    ss_res = ((values[y].to_numpy() - pred) ** 2).sum()
    ss_tot = ((values[y].to_numpy() - values[y].mean()) ** 2).sum()
    return float(1.0 - ss_res / ss_tot) if ss_tot > 0 else 0.0, beta


def grid_analysis() -> tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    diag = pd.read_csv(RESULTS / "grid_diagnostics.csv")
    paired = pd.read_csv(RESULTS / "paired_grid.csv")[
        ["graph_id", "pair_id", "time_ratio", "exp_ratio"]
    ].rename(columns={"time_ratio": "main_time_ratio",
                      "exp_ratio": "main_exp_ratio"})
    grid = diag.merge(paired, on=["graph_id", "pair_id"], how="inner",
                      validate="one_to_one")
    grid["log_time_ratio"] = np.log(grid["main_time_ratio"])

    by_family = grid.groupby("family").agg(
        n=("pair_id", "size"),
        h_corr=("h_corr", "median"),
        h_nmae=("h_nmae", "median"),
        h_mae=("h_mae", "median"),
        detour=("detour", "median"),
        time_ratio=("main_time_ratio", "median"),
        exp_ratio=("main_exp_ratio", "median"),
        ebf_a=("ebf_a", "median"),
    ).reset_index()

    correlations = []
    for feature in ["h_corr", "h_pearson", "h_nmae", "h_rmse", "detour", "ebf_a"]:
        corr = spearmanr(grid[feature], grid["main_time_ratio"]).statistic
        correlations.append({"feature": feature,
                             "spearman_vs_time_ratio": corr})
    correlations = pd.DataFrame(correlations)

    model_rows = []
    for feature in ["h_corr", "h_nmae", "detour"]:
        score, beta = r2_linear(grid, feature, "log_time_ratio")
        model_rows.append({"model": f"log(time_ratio) ~ {feature}",
                           "r2": score, "intercept": beta[0],
                           "slope": beta[1]})
    models = pd.DataFrame(model_rows)

    grid.to_csv(RESULTS / "paired_grid_diagnostics.csv", index=False)
    by_family.to_csv(RESULTS / "summary_grid_diagnostics.csv", index=False)
    correlations.to_csv(RESULTS / "summary_diagnostic_correlations.csv", index=False)
    models.to_csv(RESULTS / "summary_diagnostic_models.csv", index=False)
    return grid, by_family, correlations


def real_analysis() -> tuple[pd.DataFrame, pd.DataFrame]:
    diag = pd.read_csv(RESULTS / "real_diagnostics.csv")
    paired = pd.read_csv(RESULTS / "paired_real.csv")
    paired = paired[paired["variant"] == "astar_alt8"][
        ["pair_id", "time_ratio", "exp_ratio"]
    ].rename(columns={"time_ratio": "main_time_ratio",
                      "exp_ratio": "main_exp_ratio"})
    real = diag.merge(paired, on="pair_id", how="inner", validate="one_to_one")
    by_band = real.groupby("band").agg(
        n=("pair_id", "size"),
        h_corr=("h_corr", "median"),
        h_nmae=("h_nmae", "median"),
        h_mae=("h_mae", "median"),
        time_ratio=("main_time_ratio", "median"),
        exp_ratio=("main_exp_ratio", "median"),
    ).reset_index()
    overall = pd.DataFrame([{
        "n": len(real),
        "h_corr": real["h_corr"].median(),
        "h_nmae": real["h_nmae"].median(),
        "h_mae": real["h_mae"].median(),
        "time_ratio": real["main_time_ratio"].median(),
        "exp_ratio": real["main_exp_ratio"].median(),
    }])
    corr_rows = []
    for feature in ["h_corr", "h_nmae", "h_mae"]:
        corr_rows.append({
            "feature": feature,
            "spearman_vs_time_ratio": spearmanr(
                real[feature], real["main_time_ratio"]).statistic,
        })
    pd.DataFrame(corr_rows).to_csv(
        RESULTS / "summary_real_diagnostic_correlations.csv", index=False)
    real.to_csv(RESULTS / "paired_real_diagnostics.csv", index=False)
    by_band.to_csv(RESULTS / "summary_real_diagnostics.csv", index=False)
    return real, overall


def weighted_analysis() -> tuple[pd.DataFrame, pd.DataFrame]:
    raw = pd.read_csv(RESULTS / "weighted_maze.csv")
    raw = raw[raw["status"] == "ok"]
    query = raw.groupby(
        ["graph_id", "size", "pair_id", "band", "variant", "epsilon"]
    )[["time_ns", "expanded", "distance", "cost_ratio"]].median().reset_index()
    di = query[query["variant"] == "dijkstra"][
        ["graph_id", "pair_id", "time_ns", "expanded", "distance"]
    ].rename(columns={"time_ns": "time_d", "expanded": "exp_d",
                      "distance": "dist_d"})
    wa = query[query["variant"] == "weighted_astar"]
    merged = wa.merge(di, on=["graph_id", "pair_id"], validate="many_to_one")
    merged["time_ratio"] = merged["time_d"] / merged["time_ns"]
    merged["exp_ratio"] = merged["exp_d"] / merged["expanded"]

    by_epsilon = merged.groupby("epsilon").agg(
        n=("pair_id", "size"),
        time_ratio=("time_ratio", "median"),
        time_q1=("time_ratio", lambda x: x.quantile(0.25)),
        time_q3=("time_ratio", lambda x: x.quantile(0.75)),
        cost_ratio=("cost_ratio", "median"),
        cost_max=("cost_ratio", "max"),
        exp_ratio=("exp_ratio", "median"),
        expanded=("expanded", "median"),
    ).reset_index()
    by_size = merged.pivot_table(
        index="size", columns="epsilon", values="time_ratio", aggfunc="median"
    ).reset_index()
    by_size_band = merged.pivot_table(
        index=["size", "band"], columns="epsilon", values="time_ratio",
        aggfunc="median"
    ).reset_index()
    by_size.to_csv(RESULTS / "summary_weighted_by_size.csv", index=False)
    by_size_band.to_csv(RESULTS / "summary_weighted_by_size_band.csv",
                        index=False)
    sig_rows = []
    for eps in [10.0, 20.0, 50.0]:
        for size, group in merged[merged["epsilon"] == eps].groupby("size"):
            differences = group["time_ratio"].to_numpy() - 1.0
            p_value = 1.0 if np.allclose(differences, 0.0) else float(
                wilcoxon(differences, zero_method="wilcox",
                         alternative="two-sided").pvalue)
            sig_rows.append({"epsilon": eps, "size": size,
                             "n": len(group),
                             "time_ratio_median": group["time_ratio"].median(),
                             "wilcoxon_p": p_value})
    pd.DataFrame(sig_rows).to_csv(
        RESULTS / "summary_weighted_significance.csv", index=False)
    by_epsilon.to_csv(RESULTS / "summary_weighted_by_epsilon.csv", index=False)
    merged.to_csv(RESULTS / "paired_weighted_maze.csv", index=False)
    return merged, by_epsilon


def tie_tail() -> pd.DataFrame:
    tie = pd.read_csv(RESULTS / "paired_tie_break.csv")
    rows = []
    for family, group in tie.groupby("family"):
        x = group["exp_ratio_nodeid_over_largerg"]
        rows.append({
            "family": family,
            "n": len(x),
            "median": x.median(),
            "q90": x.quantile(0.90),
            "q95": x.quantile(0.95),
            "max": x.max(),
            "fraction_gt_2": (x > 2).mean(),
            "fraction_gt_10": (x > 10).mean(),
        })
    out = pd.DataFrame(rows)
    out.to_csv(RESULTS / "summary_tie_tail.csv", index=False)
    return out


def figures(grid: pd.DataFrame, weighted: pd.DataFrame) -> None:
    families = ["open", "random", "clustered", "rooms", "maze"]
    colors = {"open": "#1f77b4", "random": "#ff7f0e",
              "clustered": "#2ca02c", "rooms": "#d62728", "maze": "#9467bd"}
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    for family in families:
        group = grid[grid.family == family]
        axes[0].scatter(group["h_corr"], group["main_time_ratio"], s=16,
                        alpha=0.6, color=colors[family], label=family)
        axes[1].scatter(group["h_nmae"], group["main_time_ratio"], s=16,
                        alpha=0.6, color=colors[family], label=family)
    for axis, xlabel in ((axes[0], "Spearman correlation  h(v) vs d(v,t)"),
                         (axes[1], "normalised MAE of h")):
        axis.axhline(1.0, color="black", linestyle="--", linewidth=1)
        axis.set_yscale("log")
        axis.set_xlabel(xlabel)
        axis.set_ylabel("time ratio  Dijkstra / A*")
        axis.grid(alpha=0.25)
    axes[0].set_title("More informative heuristic -> more speedup")
    axes[1].set_title("Larger heuristic error -> less speedup")
    axes[0].legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(FIGURES / "novelty_heuristic_speedup.png", dpi=180)
    plt.close(fig)

    # Weighted A*: time ratio by epsilon and size; path cost is exactly optimal
    # on a perfect maze because the simple path is unique.
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    for size, group in weighted.groupby("size"):
        group = group.groupby("epsilon")["time_ratio"].median().reset_index()
        axes[0].plot(group["epsilon"], group["time_ratio"], marker="o",
                     label=f"{size}x{size}")
    axes[0].axhline(1.0, color="black", linestyle="--", linewidth=1)
    axes[0].set_xscale("log")
    axes[0].set_xlabel("epsilon in f = g + epsilon*h")
    axes[0].set_ylabel("time ratio  Dijkstra / weighted A*")
    axes[0].set_title("Weighted A* on perfect mazes")
    axes[0].grid(alpha=0.25)
    axes[0].legend(title="maze size")

    eps = weighted.groupby("epsilon").agg(
        cost=("cost_ratio", "median"),
        cost_max=("cost_ratio", "max")).reset_index()
    axes[1].plot(eps["epsilon"], eps["cost"], marker="o", label="median")
    axes[1].plot(eps["epsilon"], eps["cost_max"], marker="s",
                  linestyle="--", label="maximum")
    axes[1].set_xscale("log")
    axes[1].set_ylim(0.95, 1.15)
    axes[1].set_xlabel("epsilon")
    axes[1].set_ylabel("path cost / optimal cost")
    axes[1].set_title("Optimality price is zero: unique maze path")
    axes[1].grid(alpha=0.25)
    axes[1].legend()
    fig.tight_layout()
    fig.savefig(FIGURES / "novelty_weighted_maze.png", dpi=180)
    plt.close(fig)


def markdown(grid_by_family: pd.DataFrame, correlations: pd.DataFrame,
             models: pd.DataFrame, real_by_band: pd.DataFrame,
             real_overall: pd.DataFrame, weighted_by_epsilon: pd.DataFrame,
             weighted_by_size: pd.DataFrame, tie: pd.DataFrame) -> None:
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

    text = ["# Novelty analysis\n\n",
            "## Heuristic informativeness\n\n",
            table(grid_by_family),
            "\nSpearman correlations with time ratio:\n\n",
            table(correlations),
            "\nSimple explanatory models:\n\n",
            table(models, 3),
            "\n![Heuristic informativeness](../figures/novelty_heuristic_speedup.png)\n\n",
            "## Real road network\n\n",
            table(real_overall, 3),
            "\n",
            table(real_by_band),
            "\nSpearman correlations with time ratio:\n\n",
            table(pd.read_csv(RESULTS / "summary_real_diagnostic_correlations.csv")),
            "\n## Weighted A* on perfect mazes\n\n",
            table(weighted_by_epsilon),
            "\nTime ratio by size:\n\n",
            table(weighted_by_size),
            "\nPaired Wilcoxon tests for the larger epsilon values:\n\n",
            table(pd.read_csv(RESULTS / "summary_weighted_significance.csv")),
            "\n![Weighted A*](../figures/novelty_weighted_maze.png)\n\n",
            "The path-cost ratio is 1.0 for every epsilon: a perfect maze has a\n",
            "unique simple source-target path, so weighted A* cannot return a\n",
            "different path.  Large epsilon changes the search order and can reduce\n",
            "expansions, especially for short queries in large mazes.\n\n",
            "## Tie-break tail\n\n",
            table(tie),
            "\n"]
    (RESULTS / "novelty.md").write_text("".join(text), encoding="utf-8")


def main() -> None:
    grid, grid_by_family, correlations = grid_analysis()
    real, real_overall = real_analysis()
    weighted, weighted_by_epsilon = weighted_analysis()
    tie = tie_tail()
    figures(grid, weighted)
    markdown(grid_by_family, correlations,
             pd.read_csv(RESULTS / "summary_diagnostic_models.csv"),
             pd.read_csv(RESULTS / "summary_real_diagnostics.csv"),
             real_overall,
             weighted_by_epsilon,
             pd.read_csv(RESULTS / "summary_weighted_by_size.csv"),
             tie)
    print("wrote results/novelty.md and novelty figures")


if __name__ == "__main__":
    main()
