#!/usr/bin/env python3
"""Quark vs naive STL — measured bars from bench_summary.txt."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SUMMARY = ROOT / "build" / "bench_summary.txt"
if not SUMMARY.exists():
    SUMMARY = ROOT / "bench_summary.txt"
OUT = ROOT / "docs"


def load_summary(path: Path) -> dict:
    d = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            d[k.strip()] = float(v.strip())
    return d


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    if not SUMMARY.exists():
        raise SystemExit(f"Missing {SUMMARY}")
    d = load_summary(SUMMARY)

    eng_p50 = d["engine_p50_ns"]
    eng_p99 = d["engine_p99_ns"]
    eng_mops = d["engine_mops"]
    nai_p50 = d["naive_p50_ns"]
    nai_p99 = d["naive_p99_ns"]
    nai_mops = d["naive_mops"]

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8), dpi=150)

    # Latency (lower is better)
    metrics = ["p50 (ns)", "p99 (μs)"]
    engine = [eng_p50, eng_p99 / 1000]
    naive = [nai_p50, nai_p99 / 1000]
    x = np.arange(len(metrics))
    w = 0.35
    axes[0].bar(x - w / 2, naive, w, label="Textbook map+vector", color="#94A3B8", edgecolor="#334155")
    axes[0].bar(x + w / 2, engine, w, label="Quark", color="#27AE60", edgecolor="#14532D")
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(metrics)
    axes[0].set_ylabel("Latency")
    axes[0].set_title("Latency (lower is better)")
    axes[0].legend()
    axes[0].set_facecolor("#F8FAFC")
    for i, (n, e) in enumerate(zip(naive, engine)):
        axes[0].text(i - w / 2, n, f"{n:.0f}" if i == 0 else f"{n:.1f}", ha="center", va="bottom", fontsize=8)
        axes[0].text(i + w / 2, e, f"{e:.0f}" if i == 0 else f"{e:.1f}", ha="center", va="bottom", fontsize=8)

    # Throughput (higher is better)
    bars = axes[1].bar(
        ["Textbook\nmap+vector", "Quark"],
        [nai_mops, eng_mops],
        color=["#94A3B8", "#27AE60"],
        edgecolor="#334155",
        width=0.5,
    )
    axes[1].set_ylabel("Mops/s")
    axes[1].set_title("Throughput (higher is better)")
    axes[1].set_facecolor("#F8FAFC")
    for b, v in zip(bars, [nai_mops, eng_mops]):
        axes[1].text(
            b.get_x() + b.get_width() / 2,
            v,
            f"{v:.2f}",
            ha="center",
            va="bottom",
            fontsize=10,
            fontweight="bold",
        )

    speed_t = eng_mops / nai_mops if nai_mops else 0
    speed_p = nai_p50 / eng_p50 if eng_p50 else 0
    fig.suptitle(
        f"Churn workload vs textbook baseline (map + vector, O(n) cancel)\n"
        f"Throughput {speed_t:.2f}x  ·  p50 {speed_p:.2f}x",
        fontsize=12,
        fontweight="bold",
    )
    fig.tight_layout()
    fig.savefig(OUT / "comparison_measured.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Structural
    fig, ax = plt.subplots(figsize=(9, 4.8), dpi=150)
    labels = ["Hot-path\nheap allocs", "Locks", "Hot-path\nrehash", "Best price\ncost (arb.)"]
    naive_s = [3, 1, 1, 3]
    quark_s = [0, 0, 0, 1]
    x = np.arange(len(labels))
    w = 0.35
    ax.bar(x - w / 2, naive_s, w, label="Naive STL", color="#94A3B8", edgecolor="#334155")
    ax.bar(x + w / 2, quark_s, w, label="Quark", color="#27AE60", edgecolor="#14532D")
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_ylabel("Cost units (lower is better)")
    ax.set_title("Structural hot-path properties")
    ax.legend()
    ax.set_facecolor("#F8FAFC")
    fig.tight_layout()
    fig.savefig(OUT / "comparison_structural.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    print(f"Quark {eng_mops:.2f} Mops  p50={eng_p50:.0f}ns | Naive {nai_mops:.2f} Mops p50={nai_p50:.0f}ns")
    print(f"Wrote {OUT / 'comparison_measured.png'}")


if __name__ == "__main__":
    main()
