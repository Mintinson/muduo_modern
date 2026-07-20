#!/usr/bin/env python3
"""
Plot discard benchmark results from CSV file.

Usage:
    micromamba run -n lerobot python3 plot_results.py discard_comparison.csv

Produces:
    - discard_benchmark.png  (dual-panel plot)
    - Calculates and prints additional metrics
"""

import sys
from pathlib import Path

import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import pandas as pd


def plot_from_csv(csv_path, save_path=None):
    """Load CSV and plot throughput vs message size with analysis."""
    df = pd.read_csv(csv_path)

    # Determine which columns are data (skip msg_size)
    size_col = "msg_size"
    impl_cols = [c for c in df.columns if c != size_col]

    # Extract implementation names from column labels
    impls = []
    for col in impl_cols:
        # Remove common suffixes
        label = col.replace("_throughput_mibs", "").replace("_throughput", "")
        impls.append(label)

    # Create enhanced plot
    plt.style.use("ggplot")
    fig = plt.figure(figsize=(18, 12))

    # --- Grid layout ---
    gs = fig.add_gridspec(2, 2, hspace=0.30, wspace=0.28)

    # Panel 1: Throughput (MiB/s) vs Message Size — linear scale
    ax1 = fig.add_subplot(gs[0, 0])
    # Panel 2: Throughput (MiB/s) vs Message Size — log-log scale
    ax2 = fig.add_subplot(gs[0, 1])
    # Panel 3: Message Rate (Ki Msgs/s) vs Message Size
    ax3 = fig.add_subplot(gs[1, 0])
    # Panel 4: Efficiency ratio
    ax4 = fig.add_subplot(gs[1, 1])

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd"]
    markers = ["o", "s", "^", "D", "v"]

    sizes = df[size_col].values
    size_labels = [str(s) for s in sizes]

    for idx, (impl_label, col) in enumerate(zip(impls, impl_cols)):
        tps = df[col].values
        color = colors[idx % len(colors)]
        marker = markers[idx % len(markers)]

        # Calculate derived metrics
        mb_per_sec = tps  # MiB/s
        ki_msgs_per_sec = (tps * 1024 * 1024) / sizes / 1024  # Ki Msgs/s

        # --- Panel 1: Throughput (linear scale) ---
        ax1.plot(sizes, mb_per_sec, marker=marker, label=impl_label,
                 color=color, linewidth=2.5, markersize=8)
        ax1.set_xlabel("Message Size (bytes)", fontsize=13)
        ax1.set_ylabel("Throughput (MiB/s)", fontsize=13)
        ax1.set_title("Throughput vs Message Size (Linear Scale)", fontsize=14)
        ax1.legend(fontsize=11)
        ax1.grid(True, alpha=0.3)

        # --- Panel 2: Throughput (log-log) ---
        ax2.plot(sizes, mb_per_sec, marker=marker, label=impl_label,
                 color=color, linewidth=2.5, markersize=8)
        ax2.set_xlabel("Message Size (bytes)", fontsize=13)
        ax2.set_ylabel("Throughput (MiB/s)", fontsize=13)
        ax2.set_title("Throughput vs Message Size (Log-Log)", fontsize=14)
        ax2.legend(fontsize=11)
        ax2.grid(True, alpha=0.3, which="both")
        ax2.set_xscale("log", base=2)
        ax2.set_yscale("log", base=2)

        # --- Panel 3: Message Rate ---
        ax3.plot(sizes, ki_msgs_per_sec, marker=marker, label=impl_label,
                 color=color, linewidth=2.5, markersize=8)
        ax3.set_xlabel("Message Size (bytes)", fontsize=13)
        ax3.set_ylabel("Message Rate (Ki Msgs/s)", fontsize=13)
        ax3.set_title("Message Rate vs Message Size", fontsize=14)
        ax3.legend(fontsize=11)
        ax3.grid(True, alpha=0.3)

        # --- Panel 4: Efficiency (goodput utilization) ---
        # GbE theoretical max for line-rate:
        # 1 GbE = ~119 MiB/s after overhead
        # Actually, just show relative efficiency: MiB/s / (msg_size * 1e6 / 1e6) = simpler
        # Let's show "bytes per second" utilization
        # Total wire bytes per second (including TCP/IP overhead) ≈ msgsize * msgs_per_sec
        # For small messages the TCP/IP overhead dominates
        line_rate_gbps = mb_per_sec * 8 / 1000  # approximate Gbps
        ax4.plot(sizes, line_rate_gbps, marker=marker, label=impl_label,
                 color=color, linewidth=2.5, markersize=8)
        ax4.set_xlabel("Message Size (bytes)", fontsize=13)
        ax4.set_ylabel("Approx. Line Rate (Gbps)", fontsize=13)
        ax4.set_title("Network Utilization vs Message Size", fontsize=14)
        ax4.legend(fontsize=11)
        ax4.grid(True, alpha=0.3)
        ax4.set_xscale("log", base=2)

    # Format x-ticks for all axes
    for ax in [ax1, ax2, ax3, ax4]:
        ax.set_xticks(sizes)
        ax.set_xticklabels(size_labels, rotation=45, fontsize=9)

    # Add a horizontal line at 1 Gbps on the line rate panel
    ax4.axhline(y=1.0, color="gray", linestyle="--", alpha=0.5, label="1 Gbps")
    ax4.legend(fontsize=11)

    fig.suptitle("Discard Server Benchmark — Throughput vs Message Size",
                 fontsize=16, fontweight="bold", y=1.02)

    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches="tight")
        print(f"Plot saved to: {save_path}")

    plt.show()


def analyze_results(csv_path):
    """Print analysis of benchmark results."""
    df = pd.read_csv(csv_path)
    size_col = "msg_size"
    impl_cols = [c for c in df.columns if c != size_col]

    sizes = df[size_col].values
    print("=" * 72)
    print("  Discard Benchmark Analysis")
    print("=" * 72)

    for col in impl_cols:
        label = col.replace("_throughput_mibs", "")
        tps = df[col].values
        print(f"\n  [{label}]")
        print(f"  {'Msg Size':>10} {'Throughput':>15} {'Msg Rate':>15} {'Latency*':>15}")
        print(f"  {'(bytes)':>10} {'(MiB/s)':>15} {'(Ki Msgs/s)':>15} {'(us)':>15}")
        print(f"  {'-' * 55}")

        for i, s in enumerate(sizes):
            tp = tps[i]
            ki_msg = (tp * 1024 * 1024) / s / 1024 if tp > 0 and s > 0 else 0
            # Estimated round-trip latency from throughput:
            # throughput = (msg_size * msg_count) / time
            # msg_count / time = throughput / msg_size
            # For a single connection, latency ≈ msg_size / throughput (simplified)
            # In reality, this is a pipeline of messages, so:
            # latency ≈ (msg_size * window_size) / throughput
            # We can't measure latency directly from this benchmark, so skip
            print(f"  {s:>10} {tp:>15.3f} {ki_msg:>15.3f} {'—':>15}")

        # Overall metrics
        max_tp = max(tps)
        max_idx = list(tps).index(max_tp)
        print(f"\n  Max throughput: {max_tp:.1f} MiB/s at msg_size={sizes[max_idx]}")
        print(f"  = {max_tp * 8 / 1000:.2f} Gbps")

    if len(impl_cols) >= 2:
        print(f"\n  {'─' * 55}")
        print("  Performance Difference (muduo_modern vs muduo_old):")
        print(f"  {'Msg Size':>10} {'Modern':>12} {'Old':>12} {'Diff':>12} {'Diff%':>10}")
        print(f"  {'─' * 56}")
        tp1 = df[impl_cols[0]].values
        tp2 = df[impl_cols[1]].values
        for i, s in enumerate(sizes):
            diff = tp1[i] - tp2[i]
            pct = (tp1[i] / tp2[i] - 1) * 100 if tp2[i] > 0 else 0
            print(f"  {s:>10} {tp1[i]:>12.3f} {tp2[i]:>12.3f} {diff:>+12.3f} {pct:>+9.2f}%")
        avg_diff = (sum(tp1) / sum(tp2) - 1) * 100
        print(f"  {'─' * 56}")
        print(f"  Weighted average improvement: {avg_diff:+.2f}%")

    print()


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <results.csv> [output_plot.png]")
        sys.exit(1)

    csv_path = sys.argv[1]
    if not Path(csv_path).exists():
        print(f"ERROR: CSV not found: {csv_path}")
        sys.exit(1)

    save_path = sys.argv[2] if len(sys.argv) > 2 else None

    analyze_results(csv_path)
    plot_from_csv(csv_path, save_path)


if __name__ == "__main__":
    main()
