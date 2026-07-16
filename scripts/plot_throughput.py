#!/usr/bin/env python3
"""Throughput vs symbol count — 1-core serial multi-book vs ideal N-core."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
CSV = ROOT / "build" / "throughput_vs_symbols.csv"
if not CSV.exists():
    CSV = ROOT / "throughput_vs_symbols.csv"
OUT = ROOT / "docs"


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    if not CSV.exists():
        raise SystemExit(f"Missing {CSV}. Run ./build/me_bench first.")

    # symbols,quark_1core_serial_mops,ideal_ncore_mops
    data = np.genfromtxt(CSV, delimiter=",", names=True)
    symbols = data["symbols"]
    serial = data["quark_1core_serial_mops"]
    ideal = data["ideal_ncore_mops"]

    fig, ax = plt.subplots(figsize=(9.5, 5.2), dpi=150)
    x = np.arange(len(symbols))
    w = 0.35
    ax.bar(
        x - w / 2,
        serial,
        w,
        label="Quark on 1 core (N books, serial)",
        color="#64748B",
        edgecolor="#1E293B",
    )
    ax.bar(
        x + w / 2,
        ideal,
        w,
        label="Quark ideal (1 book / core)",
        color="#27AE60",
        edgecolor="#14532D",
    )
    ax.plot(x + w / 2, ideal, "o--", color="#166534", linewidth=1.5, markersize=6, alpha=0.9)

    ax.set_xticks(x)
    ax.set_xticklabels([str(int(s)) for s in symbols])
    ax.set_xlabel("Number of symbol shards", fontsize=12)
    ax.set_ylabel("Aggregate throughput (Mops/s)", fontsize=12)
    ax.set_title(
        "Throughput vs Symbol Count\n"
        "Grey = all books on one core · Green = projected multi-core sharding",
        fontsize=12,
        fontweight="bold",
    )
    ax.legend(frameon=True)
    ax.set_facecolor("#F8FAFC")
    ax.grid(True, axis="y", alpha=0.35)
    fig.tight_layout()
    fig.savefig(OUT / "throughput_vs_symbols.png", dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"Wrote {OUT / 'throughput_vs_symbols.png'}")
    for s, a, b in zip(symbols, serial, ideal):
        print(f"  symbols={int(s)}: serial={a:.2f}  ideal={b:.2f} Mops/s")


if __name__ == "__main__":
    main()
