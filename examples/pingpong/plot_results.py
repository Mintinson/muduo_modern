#!/usr/bin/env python3
"""
plot_results.py — Visualize pingpong benchmark results.

Usage:
    python3 plot_results.py results/bench_20260621_120000.csv
    python3 plot_results.py results/bench_*.csv  # Compare multiple runs

Generates 4 charts:
    1. Throughput vs Sessions (by thread count)
    2. Throughput vs Block Size (by thread count)
    3. Throughput vs Threads (by session count)
    4. Throughput heatmap (sessions vs block size)
"""

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

def load_csv(filepath):
    """Load benchmark CSV and return list of dicts."""
    rows = []
    with open(filepath) as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append({
                "threads": int(row["threads"]),
                "sessions": int(row["sessions"]),
                "blockSize": int(row["blockSize"]),
                "throughput_mbps": float(row["throughput_mbps"]),
                "avg_msg_size": float(row["avg_msg_size"]),
            })
    return rows


def plot_throughput_vs_sessions(rows, output_dir):
    """Chart 1: Throughput vs Sessions, different thread count lines."""
    fixed_block = _most_common(rows, "blockSize")
    subset = [r for r in rows if r["blockSize"] == fixed_block]
    threads_set = sorted(set(r["threads"] for r in subset))

    fig, ax = plt.subplots(figsize=(10, 6))
    for t in threads_set:
        pts = [(r["sessions"], r["throughput_mbps"])
               for r in subset if r["threads"] == t]
        pts.sort()
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        ax.plot(xs, ys, marker="o", label=f"threads={t}")

    ax.set_xlabel("Sessions (connections)")
    ax.set_ylabel("Throughput (MiB/s)")
    ax.set_title(f"Throughput vs Sessions (blockSize={fixed_block}B)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_xscale("log")
    fig.tight_layout()
    fig.savefig(output_dir / "throughput_vs_sessions.png", dpi=150)
    plt.close(fig)
    print(f"  Saved: {output_dir / 'throughput_vs_sessions.png'}")


def plot_throughput_vs_blocksize(rows, output_dir):
    """Chart 2: Throughput vs Block Size, different thread count lines."""
    fixed_sessions = _most_common(rows, "sessions")
    subset = [r for r in rows if r["sessions"] == fixed_sessions]
    threads_set = sorted(set(r["threads"] for r in subset))

    fig, ax = plt.subplots(figsize=(10, 6))
    for t in threads_set:
        pts = [(r["blockSize"], r["throughput_mbps"])
               for r in subset if r["threads"] == t]
        pts.sort()
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        ax.plot(xs, ys, marker="s", label=f"threads={t}")

    ax.set_xlabel("Block Size (bytes)")
    ax.set_ylabel("Throughput (MiB/s)")
    ax.set_title(f"Throughput vs Block Size (sessions={fixed_sessions})")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_xscale("log")
    fig.tight_layout()
    fig.savefig(output_dir / "throughput_vs_blocksize.png", dpi=150)
    plt.close(fig)
    print(f"  Saved: {output_dir / 'throughput_vs_blocksize.png'}")


def plot_throughput_vs_threads(rows, output_dir):
    """Chart 3: Throughput vs Threads, different session count lines."""
    fixed_block = _most_common(rows, "blockSize")
    subset = [r for r in rows if r["blockSize"] == fixed_block]
    sessions_set = sorted(set(r["sessions"] for r in subset))

    fig, ax = plt.subplots(figsize=(10, 6))
    for s in sessions_set:
        pts = [(r["threads"], r["throughput_mbps"])
               for r in subset if r["sessions"] == s]
        pts.sort()
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        ax.plot(xs, ys, marker="D", label=f"sessions={s}")

    ax.set_xlabel("Threads")
    ax.set_ylabel("Throughput (MiB/s)")
    ax.set_title(f"Throughput vs Threads (blockSize={fixed_block}B)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_dir / "throughput_vs_threads.png", dpi=150)
    plt.close(fig)
    print(f"  Saved: {output_dir / 'throughput_vs_threads.png'}")


def plot_heatmap(rows, output_dir):
    """Chart 4: Heatmap — sessions vs blockSize, color = throughput."""
    best_threads = _best_threads(rows)
    subset = [r for r in rows if r["threads"] == best_threads]

    sessions_vals = sorted(set(r["sessions"] for r in subset))
    blocks_vals = sorted(set(r["blockSize"] for r in subset))

    heatmap = np.zeros((len(blocks_vals), len(sessions_vals)))
    for i, b in enumerate(blocks_vals):
        for j, s in enumerate(sessions_vals):
            vals = [r["throughput_mbps"] for r in subset
                    if r["blockSize"] == b and r["sessions"] == s]
            heatmap[i][j] = vals[0] if vals else 0

    fig, ax = plt.subplots(figsize=(10, 8))
    im = ax.imshow(heatmap, aspect="auto", cmap="viridis", origin="lower")

    ax.set_xticks(range(len(sessions_vals)))
    ax.set_xticklabels([str(s) for s in sessions_vals])
    ax.set_yticks(range(len(blocks_vals)))
    ax.set_yticklabels([str(b) for b in blocks_vals])
    ax.set_xlabel("Sessions")
    ax.set_ylabel("Block Size (bytes)")
    ax.set_title(f"Throughput Heatmap (threads={best_threads})")

    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label("Throughput (MiB/s)")

    # Annotate cells with values
    for i in range(len(blocks_vals)):
        for j in range(len(sessions_vals)):
            val = heatmap[i][j]
            if val > 0:
                text_color = "white" if val < np.max(heatmap) * 0.6 else "black"
                ax.text(j, i, f"{val:.0f}", ha="center", va="center",
                        fontsize=8, color=text_color)

    fig.tight_layout()
    fig.savefig(output_dir / "throughput_heatmap.png", dpi=150)
    plt.close(fig)
    print(f"  Saved: {output_dir / 'throughput_heatmap.png'}")


def _most_common(rows, key):
    """Return the most common value of `key` in rows."""
    counts = defaultdict(int)
    for r in rows:
        counts[r[key]] += 1
    return max(counts, key=counts.get)


def _best_threads(rows):
    """Return the thread count with highest average throughput."""
    by_threads = defaultdict(list)
    for r in rows:
        by_threads[r["threads"]].append(r["throughput_mbps"])
    return max(by_threads, key=lambda t: sum(by_threads[t]) / len(by_threads[t]))


def main():
    parser = argparse.ArgumentParser(description="PingPong Result Visualizer")
    parser.add_argument("csv_files", nargs="+", help="CSV files to plot")
    parser.add_argument("--output", "-o", type=str, default=None,
                        help="Output directory for charts")
    args = parser.parse_args()

    # Load all data
    rows = []
    for fpath in args.csv_files:
        p = Path(fpath)
        if not p.exists():
            print(f"ERROR: File not found: {p}")
            sys.exit(1)
        rows.extend(load_csv(p))

    if not rows:
        print("ERROR: No data loaded")
        sys.exit(1)

    output_dir = Path(args.output) if args.output else Path(args.csv_files[0]).parent

    print(f"Loaded {len(rows)} data points from {len(args.csv_files)} file(s)")
    print(f"Generating charts in {output_dir}/ ...")

    plot_throughput_vs_sessions(rows, output_dir)
    plot_throughput_vs_blocksize(rows, output_dir)
    plot_throughput_vs_threads(rows, output_dir)
    plot_heatmap(rows, output_dir)

    print("\nDone. Charts saved to:", output_dir)


if __name__ == "__main__":
    main()
