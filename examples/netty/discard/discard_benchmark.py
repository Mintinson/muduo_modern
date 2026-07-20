#!/usr/bin/env python3
"""
Discard Benchmark — measure throughput vs message size for discard servers.

Usage:
    # Compare two implementations
    python3 discard_benchmark.py \\
        --impl "muduo_modern" \\
            /path/to/netty_discard_server \\
            /path/to/netty_discard_client \\
        --impl "muduo_old" \\
            /path/to/netty_discard_server \\
            /path/to/netty_discard_client

    # Custom parameters
    python3 discard_benchmark.py \\
        --impl "modern" ./server ./client \\
        --duration 15 --sizes "64,256,1024,4096,16384" \\
        --save plot.png

Output:
    - Console table of throughput vs message size
    - Matplotlib plot (interactive or saved to file)
    - CSV file with raw data
"""

import argparse
import csv
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path


DURATION = 12  # seconds per test run
MSG_SIZES = [16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384]
HOST = "127.0.0.1"
DEFAULT_PORT = 2009


def find_free_port():
    """Find a free TCP port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


def check_port_available(port):
    """Check if a port is available."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(("127.0.0.1", port)) != 0


def run_benchmark(server_bin, client_bin, msg_sizes, duration, threads=0, label="impl"):
    """
    Run benchmark across all message sizes for one implementation.
    Uses a temporary file for server output, redirects client to /dev/null.
    Uses port 2009 or a fallback if 2009 is in use.
    """
    print(f"\n{'='*60}")
    print(f"  Benchmark: {label}")
    print(f"  Server: {server_bin}")
    print(f"  Client: {client_bin}")
    print(f"{'='*60}")

    results = []
    n = len(msg_sizes)

    for idx, size in enumerate(msg_sizes, 1):
        print(f"  [{idx}/{n}] msg_size={size:5d} ... ", end="", flush=True)

        # Find a free port
        port = DEFAULT_PORT if check_port_available(DEFAULT_PORT) else find_free_port()
        if port != DEFAULT_PORT:
            print(f"(port {port}) ", end="", flush=True)

        # Start server — capture stdout+stderr to a temp file
        out_file = tempfile.NamedTemporaryFile(mode="w+", delete=False, suffix=".log")
        out_path = out_file.name

        try:
            # Use stdbuf -oL to force line-buffered stdout (needed because
            # C/C++ stdio defaults to full buffering when output is a pipe/file)
            server_cmd = _server_cmd_with_linebuf(server_bin, threads)
            server_proc = subprocess.Popen(
                server_cmd,
                stdout=out_file,
                stderr=subprocess.STDOUT,
            )

            # Wait briefly, then verify server is alive
            time.sleep(0.8)
            if server_proc.poll() is not None:
                out_file.close()
                with open(out_path) as f:
                    print(f"SERVER DIED: {f.read()[:200]}")
                os.unlink(out_path)
                results.append((size, 0.0))
                continue

            # Start client — redirect output to /dev/null (avoids buffer overflow)
            with open(os.devnull, "w") as devnull:
                client_proc = subprocess.Popen(
                    [str(client_bin), HOST, str(size)],
                    stdout=devnull,
                    stderr=devnull,
                )

            time.sleep(duration)

            # Kill client, then server
            _kill_proc(client_proc, "client")
            _kill_proc(server_proc, "server")

            out_file.close()

            # Read and parse server output
            with open(out_path) as f:
                server_text = f.read()
            os.unlink(out_path)

            throughput = _parse_throughput(server_text)

            if throughput is not None and throughput > 0:
                results.append((size, throughput))
                print(f"{throughput:8.3f} MiB/s")
            else:
                results.append((size, 0.0))
                # Print server output for debugging
                print("  FAILED")
                debug_lines = [l for l in server_text.splitlines()
                               if "MiB/s" in l or "FATAL" in l or "ERROR" in l][:5]
                if debug_lines:
                    for dl in debug_lines:
                        print(f"         {dl.strip()[:100]}")

        finally:
            # Cleanup temp file if it still exists
            if os.path.exists(out_path):
                try:
                    os.unlink(out_path)
                except OSError:
                    pass
            # Ensure both processes are dead
            for pname, proc in [("server", server_proc), ("client", client_proc)]:
                try:
                    _kill_proc(proc, pname)
                except (UnboundLocalError, NameError):
                    pass

    return results


def _server_cmd_with_linebuf(server_bin, threads):
    """Build server command, using stdbuf -oL if available for line-buffered output."""
    stdbuf = shutil.which("stdbuf")
    if stdbuf:
        return [stdbuf, "-oL", str(server_bin), str(threads)]
    return [str(server_bin), str(threads)]


def _kill_proc(proc, name=""):
    """Safely terminate a process."""
    if proc is None:
        return
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except (subprocess.TimeoutExpired, ProcessLookupError, OSError):
        try:
            proc.kill()
            proc.wait(timeout=3)
        except (subprocess.TimeoutExpired, ProcessLookupError, OSError):
            pass


def _parse_throughput(server_output):
    """
    Extract average throughput (MiB/s) from server output.

    Server lines look like:
        27.376 MiB/s 51.712 Ki Msgs/s 542.09 bytes per msg
    Skips the first sample (warm-up) and returns the average of subsequent ones.
    """
    samples = []
    for line in server_output.splitlines():
        if "MiB/s" in line and "Ki Msgs/s" in line:
            try:
                parts = line.strip().split()
                for i, p in enumerate(parts):
                    if p == "MiB/s":
                        val = float(parts[i - 1])
                        samples.append(val)
                        break
            except (ValueError, IndexError):
                pass

    if len(samples) >= 2:
        # Skip first sample (warm-up), average the rest
        return sum(samples[1:]) / len(samples[1:])
    elif len(samples) == 1:
        return samples[0]
    return None


def plot_results(all_results, save_path=None, msg_sizes=None):
    """Plot throughput vs message size for all implementations."""
    import matplotlib
    matplotlib.use("TkAgg")
    import matplotlib.pyplot as plt

    plt.style.use("ggplot")
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd"]
    markers = ["o", "s", "^", "D", "v"]

    for idx, (label, results) in enumerate(all_results):
        sizes = [r[0] for r in results]
        tps = [r[1] for r in results]
        color = colors[idx % len(colors)]
        marker = markers[idx % len(markers)]

        # Left: Throughput (MiB/s) vs Message Size
        ax1.plot(sizes, tps, marker=marker, label=label,
                 color=color, linewidth=2, markersize=8)
        ax1.set_xlabel("Message Size (bytes)", fontsize=13)
        ax1.set_ylabel("Throughput (MiB/s)", fontsize=13)
        ax1.set_title("Throughput vs Message Size", fontsize=14)
        ax1.legend(fontsize=11)
        ax1.grid(True, alpha=0.3)
        ax1.set_xscale("log", base=2)

        # Right: Messages per second (Ki Msgs/s)
        msgs_per_sec = []
        for s, tp in results:
            if tp > 0 and s > 0:
                ki_msgs = (tp * 1024 * 1024) / s / 1024
                msgs_per_sec.append(ki_msgs)
            else:
                msgs_per_sec.append(0)

        ax2.plot(sizes, msgs_per_sec, marker=marker, label=label,
                 color=color, linewidth=2, markersize=8)
        ax2.set_xlabel("Message Size (bytes)", fontsize=13)
        ax2.set_ylabel("Throughput (Ki Msgs/s)", fontsize=13)
        ax2.set_title("Message Rate vs Message Size", fontsize=14)
        ax2.legend(fontsize=11)
        ax2.grid(True, alpha=0.3)
        ax2.set_xscale("log", base=2)

    # Format x-axis ticks for both
    all_sizes = msg_sizes or MSG_SIZES
    for ax in [ax1, ax2]:
        ax.set_xticks(all_sizes)
        ax.set_xticklabels([str(s) for s in all_sizes], rotation=45)

    plt.tight_layout()

    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches="tight")
        print(f"\nPlot saved to: {save_path}")

    return fig


def main():
    parser = argparse.ArgumentParser(
        description="Discard Benchmark — Throughput vs Message Size"
    )
    parser.add_argument(
        "--impl",
        action="append",
        nargs=3,
        metavar=("LABEL", "SERVER", "CLIENT"),
        help="Add an implementation: label server_bin client_bin. "
             "Can be specified multiple times for comparison.",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=DURATION,
        help=f"Test duration per size in seconds (default: {DURATION})",
    )
    parser.add_argument(
        "--threads",
        type=int,
        default=0,
        help="Server thread count (default: 0 = single thread in loop)",
    )
    parser.add_argument(
        "--sizes",
        type=str,
        default=None,
        help="Comma-separated message sizes, e.g. '16,64,256,1024,4096'",
    )
    parser.add_argument(
        "--save",
        type=str,
        default=None,
        help="Save plot to file (default: show interactive window)",
    )
    parser.add_argument(
        "--csv",
        type=str,
        default=None,
        help="Save raw data to CSV file",
    )
    args = parser.parse_args()

    # Determine message sizes
    msg_sizes = MSG_SIZES
    if args.sizes:
        msg_sizes = [int(s.strip()) for s in args.sizes.split(",")]

    # Resolve implementations
    if args.impl:
        impls = []
        for label, server, client in args.impl:
            server_path = Path(server).resolve()
            client_path = Path(client).resolve()
            if not server_path.exists():
                print(f"ERROR: Server not found: {server_path}")
                sys.exit(1)
            if not client_path.exists():
                print(f"ERROR: Client not found: {client_path}")
                sys.exit(1)
            impls.append((label, str(server_path), str(client_path)))
    else:
        print("No --impl given. Enter implementation paths (leave empty when done):")
        impls = []
        while True:
            label = input("  Label (e.g. 'muduo_modern'): ").strip()
            if not label:
                if not impls:
                    print("Need at least one implementation. Try again.")
                    continue
                break
            server = input("  Server binary: ").strip()
            client = input("  Client binary: ").strip()
            if not Path(server).exists():
                print(f"  WARNING: Server not found: {server}")
            if not Path(client).exists():
                print(f"  WARNING: Client not found: {client}")
            impls.append((label, server, client))

    print(f"\nNumber of implementations: {len(impls)}")
    print(f"Test duration per size: {args.duration}s")
    print(f"Message sizes ({len(msg_sizes)}): {msg_sizes}")
    print(f"Server threads: {args.threads}")

    all_results = []
    for label, server_bin, client_bin in impls:
        results = run_benchmark(
            server_bin, client_bin, msg_sizes, args.duration, args.threads, label
        )
        all_results.append((label, results))

    # Save CSV
    csv_path = args.csv or f"discard_results_{datetime.now():%Y%m%d_%H%M%S}.csv"
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        headers = ["msg_size"]
        for label, _ in all_results:
            headers.append(f"{label}_throughput_mibs")
        writer.writerow(headers)
        for i in range(len(msg_sizes)):
            row = [msg_sizes[i]]
            for _, results in all_results:
                if i < len(results):
                    row.append(f"{results[i][1]:.3f}")
                else:
                    row.append("N/A")
            writer.writerow(row)
    print(f"\nCSV saved to: {csv_path}")

    # Show results table
    print(f"\n{'─' * 60}")
    print(f"  Results Summary")
    print(f"{'─' * 60}")
    header = f"{'Size(B)':>8}"
    for label, _ in all_results:
        header += f" {label:>20}"
    print(header)
    for i in range(len(msg_sizes)):
        row = f"{msg_sizes[i]:>8}"
        for _, results in all_results:
            tp = results[i][1]
            row += f" {tp:>20.3f}" if tp > 0 else f" {'N/A':>20}"
        print(row)

    # Plot
    fig = plot_results(all_results, save_path=args.save, msg_sizes=msg_sizes)
    if not args.save:
        print("\nClose the plot window to exit.")
        plt.show()

    return 0


if __name__ == "__main__":
    main()
