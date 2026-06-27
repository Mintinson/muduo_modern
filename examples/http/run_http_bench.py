#!/usr/bin/env python3
"""
run_http_bench.py — HTTP server benchmark runner using 'hey'.

Usage:
    # Test chaoxi only
    python3 run_http_bench.py --quick

    # Compare chaoxi with an external server (e.g. original muduo)
    python3 run_http_bench.py --quick --compare ./muduo_http_srv --tag muduo

    # Compare with multiple external servers
    python3 run_http_bench.py --compare ./srv_a --tag libevent \\
                              --compare ./srv_b --tag asio

Output:
    results/http_bench_chaoxi_YYYYMMDD_HHMMSS.csv
    results/http_bench_muduo_YYYYMMDD_HHMMSS.csv (if --compare)
"""

import argparse
import csv
import multiprocessing
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
BUILD_DIR = SCRIPT_DIR.parent.parent / "build"
RESULTS_DIR = SCRIPT_DIR / "results"
HOST = "127.0.0.1"
PORT_BASE = 8000


def cpu_count():
    return multiprocessing.cpu_count()


def generate_matrix(quick=False):
    max_t = cpu_count() // 2
    if quick:
        workers = [1, 2]
        concurrencies = [10, 100, 500]
    else:
        workers = [1, 2, 4, 8, max_t]
        concurrencies = [1, 10, 50, 100, 500, 1000, 1500, 2000]
    workers = sorted(set(w for w in workers if w <= max_t))
    return workers, concurrencies


def start_server(binary, threads, port):
    """Start an HTTP server binary. Binary must accept thread count as argv[1]."""
    proc = subprocess.Popen(
        [str(binary), str(threads)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)
    return proc


def run_hey(hey_bin, concurrency, total_req, port, path="/hello"):
    """Run hey and return (rps, avg_latency_ms, p99_latency_ms)."""
    url = f"http://{HOST}:{port}{path}"
    cmd = [hey_bin, "-n", str(total_req), "-c", str(concurrency), url]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True,
                                timeout=total_req // 1000 + 30)
        return _parse_hey_output(result.stdout)
    except subprocess.TimeoutExpired:
        return 0.0, 0.0, 0.0


def _parse_hey_output(output):
    rps, avg_ms, p99_ms = 0.0, 0.0, 0.0
    for line in output.splitlines():
        if "Requests/sec:" in line:
            try:
                rps = float(line.strip().split()[-1])
            except (ValueError, IndexError):
                pass
        if "Average:" in line:
            try:
                # print(line)
                avg_ms = float(line.strip().split()[-2]) * 1000
            except (ValueError, IndexError):
                pass
        if "99%% in" in line:
            try:
                p99_ms = float(line.strip().split()[-2]) * 1000
            except (ValueError, IndexError):
                pass
    return rps, avg_ms, p99_ms


def kill_server(proc):
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()


def run_benchmark(hey_bin, total_req, tag, binary, workers, concurrencies,
                  port=PORT_BASE):
    """Run benchmark for one server binary, save results to CSV."""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = RESULTS_DIR / f"http_bench_{tag}_{timestamp}.csv"
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    total = len(workers) * len(concurrencies)
    test_num = 0

    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["server", "threads", "concurrency",
                         "rps", "avg_latency_ms", "p99_latency_ms"])

        for n_workers in workers:
            proc = start_server(binary, n_workers, port)

            for concurrency in concurrencies:
                test_num += 1
                label = (f"[{tag}/{test_num}/{total}] "
                         f"threads={n_workers}, c={concurrency}")
                print(f"{label} ... ", end="", flush=True)

                rps, avg_ms, p99_ms = run_hey(
                    hey_bin, concurrency, total_req, port)
                writer.writerow([tag, n_workers, concurrency,
                                 f"{rps:.1f}", f"{avg_ms:.2f}",
                                 f"{p99_ms:.2f}"])
                f.flush()
                print(f"  {rps:.1f} req/s, avg={avg_ms:.2f}ms")

            kill_server(proc)
            time.sleep(0.3)

    print(f"\nResults saved to: {csv_path}")

    with open(csv_path) as f_in:
        rows = list(csv.DictReader(f_in))
    if rows:
        best = max(rows, key=lambda r: float(r["rps"]))
        print(f"Best RPS ({tag}): {best['rps']} "
              f"({best['threads']} threads, c={best['concurrency']})")
    return csv_path


def main():
    parser = argparse.ArgumentParser(description="HTTP Benchmark Runner")
    parser.add_argument("--quick", action="store_true",
                        help="Quick test with reduced matrix")
    parser.add_argument("--total", type=int, default=200000,
                        help="Total requests per test (default: 200000)")
    parser.add_argument("--hey-bin", type=str, default="hey",
                        help="Path to hey binary")
    parser.add_argument("--compare", action="append",
                        nargs=2, metavar=("BINARY", "TAG"),
                        default=[],
                        help="Add an external server for comparison. "
                             "Usage: --compare ./muduo_srv muduo")
    args = parser.parse_args()

    hey_bin = args.hey_bin
    total_req = args.total

    r = subprocess.run([hey_bin], capture_output=True)
    if r.returncode not in (0, 1):
        print(f"ERROR: '{hey_bin}' not found. "
              "Install: go install github.com/rakyll/hey@latest")
        sys.exit(1)

    workers, concurrencies = generate_matrix(args.quick)
    print(f"CPU cores: {cpu_count()}, max threads: {cpu_count() // 2}")
    print(f"Workers: {workers}")
    print(f"Concurrencies: {concurrencies}")
    print(f"Total requests per test: {total_req}")
    print(f"Tests per server: {len(workers) * len(concurrencies)}")
    print()

    # Collect all servers to benchmark: [(tag, binary_path)]
    servers = [("chaoxi", str(BUILD_DIR / "examples/http/http_server"))]

    for binary_path, tag in args.compare:
        p = Path(binary_path)
        if not p.exists():
            print(f"WARNING: '{tag}' binary not found at {p}, skipping")
            continue
        servers.append((tag, str(p)))

    csv_files = []
    for tag, binary in servers:
        # All servers use the same port (sequential execution, no conflict)
        csv_path = run_benchmark(hey_bin, total_req, tag, binary,
                                 workers, concurrencies, port=PORT_BASE)
        csv_files.append(str(csv_path))

    print(f"\nAll done. {len(csv_files)} result file(s):")
    for f in csv_files:
        print(f"  {f}")
    print(f"\nVisualize: streamlit run {SCRIPT_DIR / 'interactive_plot.py'}")


if __name__ == "__main__":
    main()
