#!/usr/bin/env python3
"""Compare two performance-mode subset runs.

Example:
    python3 scripts/compare_performance_modes.py low high

It checks that expansion ratios are unchanged (the searches are deterministic)
and reports how much the paired time ratio moves between modes.  This is a
robustness check, not a replacement for the frozen main experiment.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parent.parent


def paired(path: Path) -> pd.DataFrame:
    data = pd.read_csv(path)
    data = data[data["status"] == "ok"]
    keys = ["graph_id", "family", "width", "pair_id", "variant"]
    med = (
        data.groupby(keys)[["time_ns", "expanded"]]
        .median()
        .reset_index()
    )
    di = med[med.variant == "dijkstra"].drop(columns="variant").rename(
        columns={"time_ns": "time_d", "expanded": "exp_d"})
    ast = med[med.variant.isin(["astar_larger_g", "astar_alt8"])].drop(
        columns="variant").rename(columns={"time_ns": "time_a",
                                           "expanded": "exp_a"})
    out = di.merge(ast, on=["graph_id", "family", "width", "pair_id"],
                   validate="one_to_one")
    out["time_ratio"] = out["time_d"] / out["time_a"]
    out["exp_ratio"] = out["exp_d"] / out["exp_a"]
    return out


def compare(name: str, low_path: Path, high_path: Path) -> None:
    low = paired(low_path)
    high = paired(high_path)
    keys = ["graph_id", "family", "width", "pair_id"]
    merged = low.merge(high, on=keys, suffixes=("_low", "_high"),
                       validate="one_to_one")
    merged["time_ratio_change"] = merged["time_ratio_high"] / merged["time_ratio_low"]
    merged["exp_ratio_change"] = merged["exp_ratio_high"] / merged["exp_ratio_low"]

    print(f"\n{name}: {len(merged)} paired queries")
    print("  median high/low time ratio: "
          f"{merged.time_ratio_change.median():.3f} "
          f"(IQR {merged.time_ratio_change.quantile(.25):.3f}--"
          f"{merged.time_ratio_change.quantile(.75):.3f})")
    print("  median high/low expansion ratio: "
          f"{merged.exp_ratio_change.median():.3f}")
    print("  expansion ratios equal: "
          f"{np.allclose(merged.exp_ratio_change, 1.0)}")
    print("\n  by family:")
    by_family = merged.groupby("family")["time_ratio_change"].median()
    for family, value in by_family.items():
        print(f"    {family:10s} {value:.3f}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("low", help="tag used for the low-performance run")
    parser.add_argument("high", help="tag used for the high-performance run")
    args = parser.parse_args()

    for kind in ("grid", "real"):
        low_path = ROOT / "results" / f"perf_mode_{args.low}_{kind}.csv"
        high_path = ROOT / "results" / f"perf_mode_{args.high}_{kind}.csv"
        if not low_path.exists() or not high_path.exists():
            raise SystemExit(f"missing {low_path} or {high_path}")
        compare(kind, low_path, high_path)


if __name__ == "__main__":
    main()
