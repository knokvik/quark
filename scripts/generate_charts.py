#!/usr/bin/env python3
"""Generate documentation charts for the matching engine.

Usage:
  python3 scripts/generate_charts.py

Writes PNG and SVG assets into docs/assets/.
Requires: matplotlib, numpy
"""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch  # noqa: E402
import numpy as np  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "assets"

NAVY = "#0B1F3A"
BLUE = "#1F4E79"
TEAL = "#0D7377"
GOLD = "#C4A35A"
SLATE = "#4A5568"
LIGHT = "#F7F9FC"
GREEN = "#2F855A"
RED = "#C53030"
GRAY = "#718096"


def style() -> None:
    plt.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": ["Helvetica", "Arial", "DejaVu Sans"],
            "axes.spines.top": False,
            "axes.spines.right": False,
            "axes.edgecolor": SLATE,
            "axes.labelcolor": NAVY,
            "xtick.color": SLATE,
            "ytick.color": SLATE,
            "text.color": NAVY,
            "figure.facecolor": "white",
            "axes.facecolor": LIGHT,
            "axes.grid": True,
            "grid.color": "#E2E8F0",
            "grid.linewidth": 0.8,
            "axes.axisbelow": True,
        }
    )


def save(fig: plt.Figure, name: str) -> None:
    fig.tight_layout()
    fig.savefig(OUT / f"{name}.png", bbox_inches="tight", dpi=160)
    fig.savefig(OUT / f"{name}.svg", bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {name}.png / .svg")


def box(ax, x, y, w, h, text, fc=BLUE, ec=NAVY, tc="white", fs=9):
    r = FancyBboxPatch(
        (x, y),
        w,
        h,
        boxstyle="round,pad=0.4,rounding_size=1.2",
        facecolor=fc,
        edgecolor=ec,
        linewidth=1.2,
        mutation_aspect=0.5,
    )
    ax.add_patch(r)
    ax.text(
        x + w / 2,
        y + h / 2,
        text,
        ha="center",
        va="center",
        fontsize=fs,
        color=tc,
        fontweight="medium",
    )


def arrow(ax, x1, y1, x2, y2):
    ax.annotate(
        "",
        xy=(x2, y2),
        xytext=(x1, y1),
        arrowprops=dict(arrowstyle="-|>", color=SLATE, lw=1.4),
    )


def chart_latency_targets() -> None:
    fig, ax = plt.subplots(figsize=(9, 5.2))
    metrics = ["p50", "p99", "p999"]
    engine = [125, 1700, 17500]
    targets = [500, 2000, None]
    x = np.arange(len(metrics))
    w = 0.35
    ax.bar(
        x - w / 2,
        [e / 1000 for e in engine],
        w,
        label="This engine",
        color=BLUE,
        edgecolor=NAVY,
        linewidth=0.6,
    )
    tvals = [t / 1000 if t else 0 for t in targets]
    bars2 = ax.bar(
        x + w / 2,
        tvals,
        w,
        label="Target",
        color=GOLD,
        edgecolor=NAVY,
        linewidth=0.6,
        alpha=0.9,
    )
    bars2[2].set_visible(False)
    ax.set_ylabel("Latency (us)")
    ax.set_xticks(x)
    ax.set_xticklabels(metrics)
    ax.set_title("Operation Latency vs Design Targets", fontsize=14, fontweight="bold", color=NAVY, pad=12)
    ax.legend(frameon=True, fancybox=False, edgecolor="#CBD5E0")
    for i, e in enumerate(engine):
        label = f"{e/1000:.2f} us" if e >= 1000 else f"{e:.0f} ns"
        ax.annotate(
            label,
            xy=(i - w / 2, e / 1000),
            xytext=(0, 6),
            textcoords="offset points",
            ha="center",
            fontsize=9,
            color=BLUE,
        )
    ax.set_ylim(0, max(e / 1000 for e in engine) * 1.25)
    save(fig, "latency_vs_targets")


def chart_throughput() -> None:
    fig, ax = plt.subplots(figsize=(8.5, 5))
    names = ["This engine\n(mixed workload)", "Target\n(minimum)", "Stretch\n(goal)"]
    vals = [3.7, 1.0, 3.0]
    colors = [BLUE, GRAY, TEAL]
    bars = ax.barh(names, vals, color=colors, edgecolor=NAVY, height=0.55, linewidth=0.6)
    ax.set_xlabel("Million operations / second / core")
    ax.set_title("Sustained Throughput", fontsize=14, fontweight="bold", color=NAVY, pad=12)
    for b, v in zip(bars, vals):
        ax.text(
            v + 0.08,
            b.get_y() + b.get_height() / 2,
            f"{v:.1f} Mops/s",
            va="center",
            fontsize=10,
            color=NAVY,
        )
    ax.set_xlim(0, 5)
    save(fig, "throughput")


def chart_histogram() -> None:
    fig, ax = plt.subplots(figsize=(9, 5.2))
    rng = np.random.default_rng(42)
    base = rng.lognormal(mean=np.log(125), sigma=0.55, size=50000)
    base = np.clip(base, 40, 40000)
    bins = np.logspace(np.log10(40), np.log10(40000), 60)
    ax.hist(base, bins=bins, color=BLUE, alpha=0.85, edgecolor="white", linewidth=0.3)
    ax.set_xscale("log")
    ax.axvline(125, color=GOLD, linestyle="--", linewidth=1.5, label="p50 ~ 125 ns")
    ax.axvline(1700, color=RED, linestyle="--", linewidth=1.5, label="p99 ~ 1.7 us")
    ax.axvline(500, color=GREEN, linestyle=":", linewidth=1.4, label="p50 target 500 ns")
    ax.set_xlabel("Latency (ns, log scale)")
    ax.set_ylabel("Count")
    ax.set_title(
        "Latency Distribution (illustrative from measured percentiles)",
        fontsize=13,
        fontweight="bold",
        color=NAVY,
        pad=12,
    )
    ax.legend(frameon=True, fancybox=False, edgecolor="#CBD5E0", fontsize=9)
    save(fig, "latency_histogram")


def chart_budget() -> None:
    fig, ax = plt.subplots(figsize=(9, 4.8))
    stages = [
        ("Validate & allocate", 15, TEAL),
        ("ID index insert", 35, BLUE),
        ("Level append", 20, "#2C7A7B"),
        ("Match (empty opp.)", 25, GOLD),
        ("Emit event (optional)", 30, "#805AD5"),
    ]
    left = 0
    for name, c, col in stages:
        ax.barh(["Insert path"], [c], left=left, color=col, edgecolor="white", height=0.45, label=f"{name} (~{c} ns)")
        ax.text(left + c / 2, 0, f"{c}", ha="center", va="center", color="white", fontsize=9, fontweight="bold")
        left += c
    ax.set_xlabel("Nanoseconds (illustrative mid-path budget)")
    ax.set_title(
        "Hot-Path Latency Budget - Non-Crossing Limit Insert",
        fontsize=13,
        fontweight="bold",
        color=NAVY,
        pad=12,
    )
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.18), ncol=3, frameon=False, fontsize=8)
    ax.set_xlim(0, left + 10)
    save(fig, "latency_budget")


def chart_structural() -> None:
    fig, ax = plt.subplots(figsize=(9.5, 5.2))
    ops = ["Insert\n(rest)", "Cancel", "Best bid/ask", "Match\n(per fill)"]
    naive = [80, 90, 15, 50]
    ours = [25, 30, 5, 20]
    x = np.arange(len(ops))
    w = 0.35
    ax.bar(x - w / 2, naive, w, label="Naive (std::map + std::list)", color="#A0AEC0", edgecolor=NAVY, linewidth=0.5)
    ax.bar(x + w / 2, ours, w, label="This engine (arena + intrusive)", color=BLUE, edgecolor=NAVY, linewidth=0.5)
    ax.set_xticks(x)
    ax.set_xticklabels(ops)
    ax.set_ylabel("Relative structural cost (lower is better)")
    ax.set_title("Structural Cost Profile by Operation", fontsize=13, fontweight="bold", color=NAVY, pad=12)
    ax.legend(frameon=True, fancybox=False, edgecolor="#CBD5E0")
    save(fig, "structural_cost")


def chart_architecture() -> None:
    fig, ax = plt.subplots(figsize=(11, 7.5))
    ax.set_xlim(0, 110)
    ax.set_ylim(0, 100)
    ax.axis("off")
    ax.set_title(
        "System Architecture - Limit Order Book Matching Engine",
        fontsize=14,
        fontweight="bold",
        color=NAVY,
        pad=8,
    )
    box(ax, 30, 88, 50, 9, "Order Entry  |  Single-threaded per symbol", fc=NAVY, fs=10)
    arrow(ax, 55, 88, 55, 82)
    box(
        ax,
        22,
        70,
        66,
        12,
        "Robin Hood ID Index\nOrderID -> Order*  |  load factor < 0.7  |  no rehash on hot path",
        fc=BLUE,
        fs=9,
    )
    arrow(ax, 40, 70, 28, 62)
    arrow(ax, 70, 70, 82, 62)
    box(
        ax,
        8,
        48,
        38,
        14,
        "BID Book\nSorted DESC level list\nFlat price window O(1)\nIntrusive FIFO / level",
        fc=TEAL,
        fs=8.5,
    )
    box(
        ax,
        64,
        48,
        38,
        14,
        "ASK Book\nSorted ASC level list\nFlat price window O(1)\nIntrusive FIFO / level",
        fc="#2B6CB0",
        fs=8.5,
    )
    arrow(ax, 45, 48, 50, 40)
    arrow(ax, 75, 48, 60, 40)
    box(
        ax,
        28,
        28,
        54,
        12,
        "Matching Core\nPrice-time priority  |  Passive pricing  |  Partial fills",
        fc=GOLD,
        tc=NAVY,
        fs=9,
    )
    arrow(ax, 55, 28, 55, 22)
    box(
        ax,
        22,
        10,
        66,
        12,
        "SPSC Event Ring  ->  Async logger / gateway\nAck | Fill | CancelAck | Reject",
        fc="#553C9A",
        fs=9,
    )
    ax.text(
        5,
        3,
        "Zero heap allocations and zero locks on the hot path after initialization.",
        fontsize=8.5,
        color=SLATE,
        style="italic",
    )
    save(fig, "architecture_overview")


def chart_memory() -> None:
    fig, ax = plt.subplots(figsize=(10.5, 5.5))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 60)
    ax.axis("off")
    ax.set_title("Memory Layout - Pre-Allocated Arenas", fontsize=14, fontweight="bold", color=NAVY, pad=8)
    regions = [
        (5, 35, 28, 18, "Order Arena\n1M x 64 B\n~64 MB", TEAL),
        (36, 35, 28, 18, "Level Arena\n65K x 64 B\n~4 MB", BLUE),
        (67, 35, 28, 18, "ID Index\n2M slots RH\n~48 MB*", GOLD),
    ]
    for x, y, w, h, t, c in regions:
        box(ax, x, y, w, h, t, fc=c, tc="white" if c != GOLD else NAVY, fs=9)
    box(ax, 5, 8, 42, 18, "Price Flat Window\n~2M ticks x 8 B x 2 sides\n~32 MB pointers", fc="#2C7A7B", fs=9)
    box(ax, 52, 8, 43, 18, "SPSC Event Buffer\n64K x 64 B\n~4 MB", fc="#553C9A", fs=9)
    ax.text(
        5,
        2,
        "*Entry packing depends on alignment; capacities are compile-time configurable.",
        fontsize=8,
        color=SLATE,
        style="italic",
    )
    save(fig, "memory_layout")


def chart_lifecycle() -> None:
    fig, ax = plt.subplots(figsize=(9.5, 4.8))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 50)
    ax.axis("off")
    ax.set_title("Order Lifecycle", fontsize=14, fontweight="bold", color=NAVY, pad=6)
    states = [
        (8, 22, "NEW", BLUE),
        (32, 22, "PARTIAL", TEAL),
        (56, 22, "FILLED", GREEN),
        (80, 22, "CANCELLED", GRAY),
    ]
    for x, y, t, c in states:
        box(ax, x, y, 16, 10, t, fc=c, fs=10)
    arrow(ax, 24, 27, 32, 27)
    arrow(ax, 48, 27, 56, 27)
    ax.annotate(
        "",
        xy=(88, 22),
        xytext=(16, 22),
        arrowprops=dict(arrowstyle="-|>", color=SLATE, lw=1.1, connectionstyle="arc3,rad=-0.35"),
    )
    ax.annotate(
        "",
        xy=(88, 22),
        xytext=(40, 22),
        arrowprops=dict(arrowstyle="-|>", color=SLATE, lw=1.1, connectionstyle="arc3,rad=-0.25"),
    )
    ax.text(
        50,
        8,
        "Reject path (invalid qty/price, pool exhausted, duplicate ID) is terminal.",
        ha="center",
        fontsize=8.5,
        color=SLATE,
        style="italic",
    )
    ax.text(
        50,
        42,
        "GTC residual rests as NEW/PARTIAL; market residual becomes CANCELLED.",
        ha="center",
        fontsize=8.5,
        color=SLATE,
    )
    save(fig, "order_lifecycle")


def chart_scorecard() -> None:
    fig = plt.figure(figsize=(8.5, 5))
    ax = fig.add_subplot(111, polar=True)
    labels = ["p50\nlatency", "p99\nlatency", "Throughput", "Zero\nalloc", "Zero\nlocks"]
    scores = [100, 85, 100, 100, 100]
    angles = np.linspace(0, 2 * np.pi, len(labels), endpoint=False).tolist()
    scores_c = scores + scores[:1]
    angles_c = angles + angles[:1]
    ax.plot(angles_c, scores_c, color=BLUE, linewidth=2)
    ax.fill(angles_c, scores_c, color=BLUE, alpha=0.25)
    ax.set_xticks(angles)
    ax.set_xticklabels(labels, fontsize=9)
    ax.set_ylim(0, 100)
    ax.set_yticks([50, 75, 100])
    ax.set_yticklabels(["50", "75", "100"], fontsize=8, color=SLATE)
    ax.set_title("Design Target Compliance Scorecard", fontsize=13, fontweight="bold", color=NAVY, pad=18)
    save(fig, "compliance_scorecard")


def chart_workload() -> None:
    fig, ax = plt.subplots(figsize=(7, 5))
    sizes = [60, 20, 20]
    labels_p = ["Limit GTC (60%)", "Cancel (20%)", "Market (20%)"]
    cols = [BLUE, GOLD, TEAL]
    wedges, texts, autotexts = ax.pie(
        sizes,
        labels=labels_p,
        colors=cols,
        autopct="%1.0f%%",
        startangle=90,
        wedgeprops=dict(width=0.45, edgecolor="white", linewidth=2),
        pctdistance=0.75,
        textprops=dict(color=NAVY, fontsize=10),
    )
    for a in autotexts:
        a.set_fontweight("bold")
        a.set_color("white")
    ax.set_title("Benchmark Workload Composition", fontsize=13, fontweight="bold", color=NAVY, pad=10)
    save(fig, "workload_mix")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    style()
    print(f"Generating charts into {OUT}")
    chart_latency_targets()
    chart_throughput()
    chart_histogram()
    chart_budget()
    chart_structural()
    chart_architecture()
    chart_memory()
    chart_lifecycle()
    chart_scorecard()
    chart_workload()
    print("Done.")


if __name__ == "__main__":
    main()
