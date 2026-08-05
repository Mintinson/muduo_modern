#!/usr/bin/env python3
"""
run_benchmark.py — PingPong benchmark orchestration script.

Usage:
    python3 run_benchmark.py                    # Full benchmark
    python3 run_benchmark.py --quick             # Quick test (2 threads, 10 sessions)
    python3 run_benchmark.py --time 5            # 5-second runs
    python3 run_benchmark.py --server ./my_srv --client ./my_cli  # Custom binaries

Output:
    results/bench_YYYYMMDD_HHMMSS.csv           # Raw data
    results/bench_YYYYMMDD_HHMMSS_summary.txt   # Summary statistics
"""
import argparse
import csv
import multiprocessing
import os
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
BUILD_DIR = Path(__file__).resolve().parent.parent.parent / "build"
SERVER_BIN = BUILD_DIR / "examples" / "pingpong" / "pingpong_server"
CLIENT_BIN = BUILD_DIR / "examples" / "pingpong" / "pingpong_client"

if os.name == "nt":  # Windows
    SERVER_BIN = SERVER_BIN.with_suffix(".exe")
    CLIENT_BIN = CLIENT_BIN.with_suffix(".exe")
RESULTS_DIR = SCRIPT_DIR / "results"
HOST = "127.0.0.1"
BASE_PORT = 9981


def cpu_count():
    return multiprocessing.cpu_count()


def generate_matrix(quick=False):
    """Generate parameter matrix (threads, sessions, blockSize)."""
    max_t = cpu_count() // 2
    if quick:
        threads = [2]
        sessions = [10]
        blocks = [256, 1024]
    else:
        threads = [int(2**t) for t in range(int(max_t).bit_length())]
        sessions = [1, 10, 100, 500, 1000]
        blocks = [64, 256, 1024, 4096, 16384]

    # Deduplicate: only unique thread counts up to max_t
    threads = sorted(set(t for t in threads if t <= max_t))
    return threads, sessions, blocks


def run_server(server_bin, port, threads):
    """Start server process and return Popen object."""
    proc = subprocess.Popen(
        [str(server_bin), HOST, str(port), str(threads)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.5)  # Wait for server to be ready
    return proc


def run_client(client_bin, port, threads, block_size, sessions, duration):
    """Run client and return (throughput_mbps, avg_msg_size)."""
    try:
        result = subprocess.run(
            [
                str(client_bin),
                HOST,
                str(port),
                str(threads),
                str(block_size),
                str(sessions),
                str(duration),
            ],
            capture_output=True,
            text=True,
            timeout=duration + 10,
        )
        output = result.stdout + result.stderr
        return parse_client_output(output)
    except subprocess.TimeoutExpired:
        return (0.0, 0.0)


def parse_client_output(output):
    """Extract throughput (MiB/s) and avg message size from client output."""
    throughput = 0.0
    avg_msg = 0.0
    for line in output.splitlines():
        if "MiB/s" in line:
            try:
                parts = line.strip().split()
                # Find the number before "MiB/s"
                for i, p in enumerate(parts):
                    if "MiB/s" in p:
                        throughput = float(parts[i - 1])
                        break
            except (ValueError, IndexError):
                pass
        if "average message size" in line.lower():
            try:
                parts = line.strip().split()
                for i, p in enumerate(parts):
                    if "average" in p:
                        avg_msg = float(parts[i - 1])
                        break
                # avg_msg = float(parts[-1])
            except (ValueError, IndexError):
                pass
    return throughput, avg_msg


def main():
    parser = argparse.ArgumentParser(description="PingPong Benchmark Runner")
    parser.add_argument("--quick", action="store_true", help="Quick test with reduced matrix")
    parser.add_argument(
        "--time", type=int, default=10, help="Test duration in seconds (default: 10)"
    )
    parser.add_argument("--server", type=str, default=str(SERVER_BIN), help="Path to server binary")
    parser.add_argument("--client", type=str, default=str(CLIENT_BIN), help="Path to client binary")
    parser.add_argument(
        "--tags", type=str, default="", help="Comma-separated tags for this experiment"
    )
    args = parser.parse_args()

    server_bin = Path(args.server)
    client_bin = Path(args.client)

    if not server_bin.exists():
        print(f"ERROR: Server binary not found: {server_bin}")
        print("Build with: cd build && ninja pingpong_server pingpong_client")
        sys.exit(1)
    if not client_bin.exists():
        print(f"ERROR: Client binary not found: {client_bin}")
        sys.exit(1)

    threads, sessions, blocks = generate_matrix(args.quick)
    total_tests = len(threads) * len(sessions) * len(blocks)

    print(f"CPU cores: {cpu_count()}, max threads: {cpu_count() // 2}")
    print(f"Parameters: threads={threads}, sessions={sessions}, blockSizes={blocks}")
    print(f"Total tests: {total_tests}")
    print(f"Duration per test: {args.time}s")
    print(f"Estimated total time: ~{total_tests * (args.time + 2)}s")
    print()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = RESULTS_DIR / f"bench_{timestamp}.csv"
    if args.tags:
        tag_suffix = "_" + args.tags.replace(",", "_")
        csv_path = RESULTS_DIR / f"bench_{timestamp}{tag_suffix}.csv"

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["threads", "sessions", "blockSize", "throughput_mbps", "avg_msg_size"])
        port_offset = 0
        test_num = 0

        for n_threads in threads:
            for n_sessions in sessions:
                for block_size in blocks:
                    test_num += 1
                    port = BASE_PORT + port_offset
                    port_offset = (port_offset + 1) % 100

                    print(
                        f"[{test_num}/{total_tests}] "
                        f"threads={n_threads}, sessions={n_sessions}, "
                        f"blockSize={block_size} ... ",
                        end="",
                        flush=True,
                    )

                    server_proc = run_server(server_bin, port, n_threads)
                    throughput, avg_msg = run_client(
                        client_bin, port, n_threads, block_size, n_sessions, args.time
                    )
                    server_proc.send_signal(signal.SIGTERM)
                    server_proc.wait(timeout=5)

                    writer.writerow(
                        [n_threads, n_sessions, block_size, f"{throughput:.2f}", f"{avg_msg:.1f}"]
                    )
                    f.flush()

                    print(f"  {throughput:.2f} MiB/s, avg_msg={avg_msg:.1f}B")

    print(f"\nResults saved to: {csv_path}")

    # Print summary
    print("\n--- Summary ---")
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    if rows:
        best = max(rows, key=lambda r: float(r["throughput_mbps"]))
        print(
            f"Best throughput: {best['throughput_mbps']} MiB/s "
            f"(threads={best['threads']}, sessions={best['sessions']}, "
            f"blockSize={best['blockSize']})"
        )
        print(f"Total data points: {len(rows)}")


if __name__ == "__main__":
    main()
