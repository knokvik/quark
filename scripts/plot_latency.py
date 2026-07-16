#!/usr/bin/env python3
"""Plot latency histogram + CDF for Quark matching engine."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
CSV = ROOT / "build" / "latencies.csv"
if not CSV.exists():
    CSV = ROOT / "latencies.csv"
OUT = ROOT / "docs"


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    if not CSV.exists():
        raise SystemExit(f"Missing {CSV}. Run: ./build/me_bench && mv latencies.csv build/")

    latencies = np.loadtxt(CSV)
    latencies = latencies[np.isfinite(latencies)]
    latencies = latencies[latencies > 0]

    p50 = float(np.percentile(latencies, 50))
    p99 = float(np.percentile(latencies, 99))
    p999 = float(np.percentile(latencies, 99.9))

    # --- Histogram ---
    fig, ax = plt.subplots(figsize=(10, 5), dpi=150)
    # fixed ns bins (log-ish edges for readability)
    bins = np.array(
        [0, 50, 75, 100, 125, 150, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000, 10000, 20000, 50000],
        dtype=float,
    )
    counts, edges = np.histogram(latencies, bins=bins)
    centers = (edges[:-1] + edges[1:]) / 2
    widths = np.diff(edges)
    ax.bar(
        centers,
        counts,
        width=widths * 0.9,
        align="center",
        color="#27AE60",
        edgecolor="#1B4332",
        linewidth=0.6,
        log=True,
    )
    ax.axvline(p50, color="#2563EB", linestyle="--", linewidth=1.8, label=f"p50 = {p50:.0f} ns")
    ax.axvline(p99, color="#DC2626", linestyle="--", linewidth=1.8, label=f"p99 = {p99:.0f} ns")
    ax.axvline(p999, color="#7C3AED", linestyle=":", linewidth=1.6, label=f"p999 = {p999:.0f} ns")
    ax.set_xlabel("Latency (ns)", fontsize=12)
    ax.set_ylabel("Order count (log scale)", fontsize=12)
    ax.set_title(
        f"Quark Matching Engine — Latency Distribution\n"
        f"p50={p50:.0f} ns  |  p99={p99/1000:.2f} μs  |  p999={p999/1000:.2f} μs",
        fontsize=13,
        fontweight="bold",
    )
    ax.legend(frameon=True)
    ax.set_xlim(0, min(20000, edges[-1]))
    ax.set_facecolor("#F8FAFC")
    fig.patch.set_facecolor("white")
    fig.tight_layout()
    fig.savefig(OUT / "latency_histogram.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # --- CDF ---
    fig, ax = plt.subplots(figsize=(10, 5), dpi=150)
    s = np.sort(latencies)
    y = np.arange(1, len(s) + 1) / len(s)
    # downsample for plot speed
    step = max(1, len(s) // 5000)
    ax.plot(s[::step], y[::step], color="#1F4E79", linewidth=2.0)
    ax.axvline(p50, color="#2563EB", linestyle="--", linewidth=1.5, label=f"p50 = {p50:.0f} ns")
    ax.axvline(p99, color="#DC2626", linestyle="--", linewidth=1.5, label=f"p99 = {p99:.0f} ns")
    ax.axhline(0.5, color="#94A3B8", linestyle=":", linewidth=1)
    ax.axhline(0.99, color="#94A3B8", linestyle=":", linewidth=1)
    ax.set_xscale("log")
    ax.set_xlabel("Latency (ns, log scale)", fontsize=12)
    ax.set_ylabel("CDF", fontsize=12)
    ax.set_title("Latency CDF — flatter right tail is better", fontsize=13, fontweight="bold")
    ax.set_ylim(0, 1.02)
    ax.legend()
    ax.set_facecolor("#F8FAFC")
    fig.tight_layout()
    fig.savefig(OUT / "latency_cdf.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    print(f"p50={p50:.1f} p99={p99:.1f} p999={p999:.1f}")
    print(f"Wrote {OUT / 'latency_histogram.png'}")
    print(f"Wrote {OUT / 'latency_cdf.png'}")


if __name__ == "__main__":
    main()
