#!/usr/bin/env python3
"""构建并运行可复现的 P0 基础组件与网络性能基线。"""

from __future__ import annotations

import argparse
import contextlib
import csv
import datetime as dt
import json
import math
import os
import pathlib
import platform
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any, Iterable


ROOT = pathlib.Path(__file__).resolve().parents[1]
GOOGLE_BENCHMARKS = {
    "base": (
        "LogStream_bench",
        "Logging_bench",
        "AsyncLogging_bench",
        "BlockingQueue_bench",
        "QueueComparison_bench",
    ),
    "net": (
        "Buffer_bench",
        "Http_bench",
        "EventLoopQueue_bench",
        "TimerQueue_bench",
        "TcpRoundtrip_bench",
    ),
}
STANDALONE_BENCHMARKS = {"base": ("LogFile_bench",), "net": ()}


def run(command: list[str], *, capture: bool = False, check: bool = True,
        env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    print("+", shlex.join(command), flush=True)
    return subprocess.run(command, cwd=ROOT, check=check, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None,
                          env=env)


def command_output(command: list[str]) -> str | None:
    try:
        return run(command, capture=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = fraction * (len(ordered) - 1)
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return ordered[low]
    return ordered[low] + (ordered[high] - ordered[low]) * (rank - low)


def distribution(values: list[float]) -> dict[str, float | int | None]:
    return {
        "samples": len(values),
        "mean": statistics.fmean(values) if values else None,
        "p50": percentile(values, 0.50),
        "p90": percentile(values, 0.90),
        "p99": percentile(values, 0.99),
        "p99_9": percentile(values, 0.999),
        "max": max(values) if values else None,
    }


def parse_time_file(path: pathlib.Path) -> dict[str, float | int | None]:
    fields: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if ": " in line:
            key, value = line.strip().split(": ", 1)
            fields[key] = value

    elapsed_text = fields.get("Elapsed (wall clock) time (h:mm:ss or m:ss)", "0")
    elapsed_parts = [float(part) for part in elapsed_text.split(":")]
    elapsed = sum(value * (60 ** index)
                  for index, value in enumerate(reversed(elapsed_parts)))
    user = float(fields.get("User time (seconds)", 0))
    system = float(fields.get("System time (seconds)", 0))
    return {
        "user_seconds": user,
        "system_seconds": system,
        "elapsed_seconds": elapsed,
        "cpu_percent": 100.0 * (user + system) / elapsed if elapsed else None,
        "max_rss_kib": int(fields.get("Maximum resident set size (kbytes)", 0)),
        "voluntary_context_switches": int(fields.get("Voluntary context switches", 0)),
        "involuntary_context_switches": int(fields.get("Involuntary context switches", 0)),
        "minor_page_faults": int(fields.get("Minor (reclaiming a frame) page faults", 0)),
        "major_page_faults": int(fields.get("Major (requiring I/O) page faults", 0)),
    }


def cmake_cache(build_dir: pathlib.Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(("//", "#")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        if (key == "CMAKE_BUILD_TYPE" or key.startswith("CMAKE_CXX_")
                or key.startswith("CHAOXI_")):
            result[key] = value
    return result


def metadata(build_dir: pathlib.Path) -> dict[str, Any]:
    cache = cmake_cache(build_dir)
    compile_commands = (build_dir / "compile_commands.json").read_text(encoding="utf-8")
    compiler = cache.get("CMAKE_CXX_COMPILER", "c++")
    machine = {
        "timestamp_utc": dt.datetime.now(dt.UTC).isoformat(),
        "git_commit": command_output(["git", "rev-parse", "HEAD"]),
        "git_status": command_output(["git", "status", "--short"]),
        "host": platform.node(),
        "os": platform.platform(),
        "kernel": platform.release(),
        "architecture": platform.machine(),
        "cpu": command_output(["lscpu"]),
        "compiler": command_output([compiler, "--version"]),
        "cmake": command_output(["cmake", "--version"]),
        "ninja": command_output(["ninja", "--version"]),
        "build_type": cache.get("CMAKE_BUILD_TYPE"),
        "ndebug": "-DNDEBUG" in compile_commands,
        "cxx_standard": 23,
        "optimization_flags": cache.get("CMAKE_CXX_FLAGS_RELEASE"),
        "lto": "-flto" in compile_commands,
        "pgo": any(flag in compile_commands for flag in ("-fprofile-use", "-fprofile-generate")),
        "event_backend": "epoll" if sys.platform.startswith("linux") else "platform default",
        "cmake_cache": cache,
        "tools": {name: shutil.which(name) for name in
                  ("taskset", "perf", "strace", "heaptrack", "heaptrack_print")},
    }
    if machine["build_type"] != "Release" or not machine["ndebug"]:
        raise RuntimeError("性能基线要求 Release 构建并启用 -DNDEBUG")
    return machine


def executable(build_dir: pathlib.Path, name: str) -> pathlib.Path:
    candidates = (build_dir / "benchmark" / name,
                  build_dir / "examples" / "pingpong" / name)
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError(name)


def timed_command(command: list[str], output: pathlib.Path,
                  cpus: str | None, *, capture: bool = False,
                  env: dict[str, str] | None = None
                  ) -> tuple[list[str], dict[str, Any], str]:
    if cpus is not None:
        command = ["taskset", "--cpu-list", cpus, *command]
    time_file = output.with_suffix(".time.txt")
    wrapped = ["/usr/bin/time", "-v", "-o", str(time_file), *command]
    completed = run(wrapped, capture=capture, env=env)
    return command, parse_time_file(time_file), completed.stdout or ""


def gbench_filter(name: str, quick: bool) -> str | None:
    if not quick:
        return None
    return {
        "TimerQueue_bench": r"TimerQueue/.*/1000/.*",
        "TcpRoundtrip_bench": r"Tcp/RoundTrip/(64|1024|16384).*$",
    }.get(name)


def run_google_benchmarks(build_dir: pathlib.Path, output_dir: pathlib.Path,
                          repetitions: int, quick: bool, cpus: str | None,
                          suites: tuple[str, ...], env: dict[str, str]
                          ) -> list[dict[str, Any]]:
    resource_rows: list[dict[str, Any]] = []
    raw_dir = output_dir / "google-benchmark"
    raw_dir.mkdir()
    for suite in suites:
        suite_dir = raw_dir / suite
        suite_dir.mkdir()
        for name in GOOGLE_BENCHMARKS[suite]:
            if name == "TcpRoundtrip_bench" and os.name == "nt":
                continue
            result_file = suite_dir / f"{name}.json"
            command = [str(executable(build_dir, name)),
                       f"--benchmark_repetitions={repetitions}",
                       f"--benchmark_min_time={'0.01s' if quick else '0.10s'}",
                       f"--benchmark_min_warmup_time={'0.01' if quick else '0.10'}",
                       "--benchmark_report_aggregates_only=false",
                       "--benchmark_out_format=json",
                       f"--benchmark_out={result_file}"]
            selected = gbench_filter(name, quick)
            if selected:
                command.append(f"--benchmark_filter={selected}")
            actual, resources, _ = timed_command(command, result_file, cpus,
                                                  env=env)
            resource_rows.append({"suite": suite, "benchmark": name,
                                  "command": actual, **resources})
    return resource_rows


def parse_key_values(output: str) -> dict[str, float]:
    result: dict[str, float] = {}
    for key, value in re.findall(r"([A-Za-z_]+)=([0-9]+(?:\.[0-9]+)?)", output):
        result[key.lower()] = float(value)
    return result


def parse_logfile_output(output: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    pattern = re.compile(
        r"^\s*(nop|/dev/null|tmpfs|page-cache|fsync)\s*\|"
        r"\s*([0-9.]+) w\s*\|"
        r"\s*([0-9.]+) MiB/s\s*\|\s*平均长度：\s*([0-9]+) 字节$")
    for line in output.splitlines():
        if match := pattern.match(line):
            rows.append({
                "case": match.group(1),
                "messages_per_second": float(match.group(2)) * 10_000.0,
                "throughput_mib_per_second": float(match.group(3)),
                "average_message_bytes": int(match.group(4)),
            })
    if len(rows) != 5:
        raise RuntimeError("无法解析 LogFile_bench 输出")
    return rows


def run_standalone_benchmarks(
        build_dir: pathlib.Path, output_dir: pathlib.Path, repetitions: int,
        cpus: str | None, suites: tuple[str, ...], env: dict[str, str]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    resources: list[dict[str, Any]] = []
    raw_root = output_dir / "standalone"
    raw_root.mkdir()
    for suite in suites:
        suite_dir = raw_root / suite
        suite_dir.mkdir()
        for name in STANDALONE_BENCHMARKS[suite]:
            if name == "LogFile_bench" and os.name == "nt":
                continue
            binary = str(executable(build_dir, name))
            warmup = [binary]
            if cpus is not None:
                warmup = ["taskset", "--cpu-list", cpus, *warmup]
            run(warmup, capture=True, env=env)
            for repetition in range(repetitions):
                result_path = suite_dir / f"{name}.{repetition}.txt"
                actual, usage, output = timed_command(
                    [binary], result_path, cpus, capture=True, env=env)
                result_path.write_text(output, encoding="utf-8")
                resources.append({"suite": suite, "benchmark": name,
                                  "repetition": repetition,
                                  "command": actual, **usage})
                for parsed in parse_logfile_output(output):
                    rows.append({"suite": suite, "benchmark": name,
                                 "repetition": repetition, **parsed})
    return rows, resources


def summarize_standalone(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str, str], list[dict[str, Any]]] = {}
    for row in rows:
        key = (str(row["suite"]), str(row["benchmark"]), str(row["case"]))
        groups.setdefault(key, []).append(row)
    summaries = []
    for (suite, benchmark_name, case), selected in sorted(groups.items()):
        summaries.append({
            "suite": suite,
            "benchmark": benchmark_name,
            "case": case,
            "messages_per_second": distribution(
                [float(row["messages_per_second"]) for row in selected]),
            "throughput_mib_per_second": distribution(
                [float(row["throughput_mib_per_second"]) for row in selected]),
            "average_message_bytes": selected[0]["average_message_bytes"],
        })
    return summaries


def run_e2e(build_dir: pathlib.Path, output_dir: pathlib.Path,
            repetitions: int, quick: bool, cpus: str | None,
            env: dict[str, str]) -> list[dict[str, Any]]:
    binary = str(executable(build_dir, "pingpong_bench"))
    result: list[dict[str, Any]] = []
    scenarios = [
        ("socketpair_latency", ["-m", "latency", "-n", "100", "-a", "1", "-w", "100"]),
        ("tcp_throughput_1k", ["-m", "throughput", "-t", "2", "-s", "10" if quick else "100",
                               "-b", "1024", "-d", "1" if quick else "5"]),
        ("tcp_throughput_64k", ["-m", "throughput", "-t", "2", "-s", "10" if quick else "100",
                                "-b", "65536", "-d", "1" if quick else "5"]),
        ("connection_storm", ["-m", "connrate", "-t", "2", "-s", "100" if quick else "10000"]),
    ]
    raw_dir = output_dir / "e2e"
    raw_dir.mkdir()
    for scenario, arguments in scenarios:
        warmup = [binary, *arguments]
        if cpus is not None:
            warmup = ["taskset", "--cpu-list", cpus, *warmup]
        run(warmup, capture=True, env=env)
        for repetition in range(repetitions):
            command = [binary, *arguments]
            if cpus is not None:
                command = ["taskset", "--cpu-list", cpus, *command]
            time_path = raw_dir / f"{scenario}.{repetition}.time.txt"
            measured = ["/usr/bin/time", "-v", "-o", str(time_path), *command]
            started = time.perf_counter()
            completed = run(measured, capture=True, env=env)
            elapsed = time.perf_counter() - started
            raw_path = raw_dir / f"{scenario}.{repetition}.txt"
            raw_path.write_text(completed.stdout, encoding="utf-8")
            values = parse_key_values(completed.stdout)
            values.update(scenario=scenario, repetition=repetition,
                          wall_seconds=elapsed, **parse_time_file(time_path))
            if scenario == "socketpair_latency":
                samples = []
                for line in completed.stdout.splitlines():
                    if re.fullmatch(r"\d+,\d+,\d+,[0-9.]+", line):
                        samples.append(float(line.rsplit(",", 1)[1]))
                values["latency_samples_us"] = samples
                values["latency_us"] = distribution(samples)
            result.append(values)
    return result


def summarize_e2e(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    summaries: list[dict[str, Any]] = []
    for scenario in sorted({str(row["scenario"]) for row in rows}):
        selected = [row for row in rows if row["scenario"] == scenario]
        summary: dict[str, Any] = {
            "scenario": scenario,
            "wall_seconds": distribution([float(row["wall_seconds"])
                                           for row in selected]),
        }
        for metric in ("throughput_mbps", "conn_per_sec", "elapsed",
                       "cpu_percent", "max_rss_kib",
                       "voluntary_context_switches",
                       "involuntary_context_switches"):
            values = [float(row[metric]) for row in selected
                      if row.get(metric) is not None]
            if values:
                summary[metric] = distribution(values)
        latency = [sample for row in selected
                   for sample in row.get("latency_samples_us", [])]
        if latency:
            summary["latency_us"] = distribution(latency)
        summaries.append(summary)
    return summaries


def google_summary(output_dir: pathlib.Path
                   ) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    grouped: dict[tuple[str, str, str], list[float]] = {}
    counter_groups: dict[tuple[str, str, str], dict[str, list[float]]] = {}
    units: dict[tuple[str, str, str], str] = {}
    standard_fields = {
        "name", "family_index", "per_family_instance_index", "run_name",
        "run_type", "repetitions", "repetition_index", "threads",
        "iterations", "real_time", "cpu_time", "time_unit",
    }
    root = output_dir / "google-benchmark"
    for path in sorted(root.glob("*/*.json")):
        suite = path.parent.name
        executable_name = path.stem
        document = json.loads(path.read_text(encoding="utf-8"))
        for item in document.get("benchmarks", []):
            if item.get("run_type") != "iteration":
                continue
            row = {"suite": suite, "benchmark": executable_name, **item}
            rows.append(row)
            name = item.get("run_name", item["name"])
            key = (suite, executable_name, name)
            grouped.setdefault(key, []).append(float(item["real_time"]))
            units[key] = item["time_unit"]
            counters = counter_groups.setdefault(key, {})
            for counter, value in item.items():
                if counter not in standard_fields and isinstance(value, (int, float)):
                    counters.setdefault(counter, []).append(float(value))
    summaries = [
        {"suite": suite, "benchmark": executable_name, "name": name,
         "time_unit": units[key], **distribution(values),
         "counters": {counter: distribution(samples)
                      for counter, samples in sorted(counter_groups[key].items())}}
        for key, values in sorted(grouped.items())
        for suite, executable_name, name in (key,)
    ]
    return rows, summaries


def write_csv(path: pathlib.Path, rows: Iterable[dict[str, Any]]) -> None:
    flattened = []
    for row in rows:
        flattened.append({key: json.dumps(value, ensure_ascii=False)
                          if isinstance(value, (dict, list)) else value
                          for key, value in row.items()})
    keys = sorted({key for row in flattened for key in row})
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=keys)
        writer.writeheader()
        writer.writerows(flattened)


def benchmark_diagnostics(build_dir: pathlib.Path, output_dir: pathlib.Path,
                          suite: str, name: str, benchmark_filter: str,
                          quick: bool, cpus: str | None,
                          env: dict[str, str]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "note": "诊断是独立的单次运行，不会混入计时样本。",
        "syscalls": None,
        "allocations": None,
    }
    diagnostic_dir = output_dir / "diagnostics" / suite
    diagnostic_dir.mkdir(parents=True, exist_ok=True)
    binary = str(executable(build_dir, name))
    arguments = [binary, f"--benchmark_filter={benchmark_filter}",
                 "--benchmark_min_time=0.01s", "--benchmark_repetitions=1"]
    if shutil.which("strace"):
        raw = diagnostic_dir / f"{name}-strace.txt"
        command = ["strace", "-f", "-c", "-o", str(raw), *arguments]
        if cpus is not None:
            command = ["taskset", "--cpu-list", cpus, *command]
        run(command, env=env)
        total_line = next((line for line in reversed(raw.read_text().splitlines())
                           if line.rstrip().endswith(" total")), "")
        numbers = re.findall(r"[0-9.]+", total_line)
        result["syscalls"] = {"total": int(numbers[-2]) if len(numbers) >= 2 else None,
                              "raw": str(raw)}
    else:
        result["syscalls"] = {"total": None, "reason": "未安装 strace"}

    if not quick and shutil.which("heaptrack") and shutil.which("heaptrack_print"):
        prefix = diagnostic_dir / f"{name}-heaptrack"
        command = ["heaptrack", "--record-only", "-o", str(prefix), *arguments]
        if cpus is not None:
            command = ["taskset", "--cpu-list", cpus, *command]
        run(command, env=env)
        traces = sorted((*diagnostic_dir.glob(f"{name}-heaptrack*.gz"),
                         *diagnostic_dir.glob(f"{name}-heaptrack*.zst")))
        if traces:
            report = command_output(["heaptrack_print", "-f", str(traces[-1])]) or ""
            raw = diagnostic_dir / f"{name}-heaptrack-summary.txt"
            raw.write_text(report, encoding="utf-8")
            match = re.search(r"calls to allocation functions:\s*([0-9]+)", report)
            result["allocations"] = {"total": int(match.group(1)) if match else None,
                                     "raw": str(raw)}
    if result["allocations"] is None:
        result["allocations"] = {"total": None,
                                 "reason": "heaptrack 诊断不可用，或当前为快速模式"}
    return result


def diagnostics(build_dir: pathlib.Path, output_dir: pathlib.Path,
                quick: bool, suites: tuple[str, ...],
                cpus: str | None, env: dict[str, str]) -> dict[str, Any]:
    targets = {
        "base": ("LogStream_bench", "BM_LogStream_Int$"),
        "net": ("Buffer_bench", "BM_Buffer_AppendAndRetrieve/1024$"),
    }
    return {
        suite: benchmark_diagnostics(build_dir, output_dir, suite, *targets[suite],
                                     quick, cpus, env)
        for suite in suites
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path,
                        default=ROOT / "build-benchmark",
                        help="构建目录，默认为 build-benchmark")
    parser.add_argument("--output-dir", type=pathlib.Path,
                        help="结果目录，默认按当前时间生成")
    parser.add_argument("--repetitions", type=int, default=5,
                        help="每个场景的重复次数，默认为 5")
    affinity = parser.add_mutually_exclusive_group()
    affinity.add_argument("--cpus", help="taskset CPU 列表，例如 0-3,8-11")
    affinity.add_argument("--cpu", type=int,
                          help="已弃用：指定单个 CPU，等价于 --cpus")
    parser.add_argument("--suite", choices=("all", "base", "net"), default="all",
                        help="要运行的基准套件，默认为 all")
    parser.add_argument("--quick", action="store_true", help="运行短时冒烟基线")
    parser.add_argument("--skip-build", action="store_true",
                        help="跳过配置和编译步骤")
    args = parser.parse_args()
    cpus = args.cpus if args.cpus is not None else (
        str(args.cpu) if args.cpu is not None else None)
    if cpus is not None:
        if os.name == "nt" or not shutil.which("taskset"):
            parser.error("--cpus 需要系统提供 taskset")
        try:
            run(["taskset", "--cpu-list", cpus, "true"], capture=True)
        except subprocess.CalledProcessError:
            parser.error(f"CPU 列表无效或不可用：{cpus}")
    suites = tuple(GOOGLE_BENCHMARKS) if args.suite == "all" else (args.suite,)
    build_dir = args.build_dir.resolve()
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output_dir = (args.output_dir or ROOT / "benchmark" / "results" / stamp).resolve()
    output_dir.mkdir(parents=True, exist_ok=False)

    if not args.skip_build:
        run(["cmake", "-S", str(ROOT), "-B", str(build_dir), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DCHAOXI_BUILD_TESTS=OFF",
             "-DCHAOXI_BUILD_EXAMPLES=ON", "-DCHAOXI_BUILD_BENCHMARKS=ON",
             "-DCHAOXI_ENABLE_STRICT_WARNINGS=ON", "-DCHAOXI_WARNINGS_AS_ERRORS=ON"])
        targets = [name for suite in suites for name in GOOGLE_BENCHMARKS[suite]]
        targets.extend(name for suite in suites
                       for name in STANDALONE_BENCHMARKS[suite]
                       if not (name == "LogFile_bench" and os.name == "nt"))
        if "net" in suites and os.name != "nt":
            targets.append("pingpong_bench")
        run(["cmake", "--build", str(build_dir), "--target", *targets])

    meta = metadata(build_dir)
    meta.update({"mode": "quick" if args.quick else "full",
                 "repetitions": args.repetitions, "cpu_affinity": cpus,
                 "suites": suites,
                 "storage": {
                     "tmpfs": command_output(
                         ["findmnt", "-T", "/dev/shm", "-o",
                          "TARGET,SOURCE,FSTYPE,OPTIONS"]),
                     "page_cache_and_fsync": command_output(
                         ["findmnt", "-T", str(output_dir), "-o",
                          "TARGET,SOURCE,FSTYPE,OPTIONS"]),
                 }})
    (output_dir / "metadata.json").write_text(
        json.dumps(meta, indent=2, ensure_ascii=False), encoding="utf-8")

    tmpfs_root = pathlib.Path("/dev/shm")
    if not tmpfs_root.is_dir():
        tmpfs_root = pathlib.Path(tempfile.gettempdir())
    with contextlib.ExitStack() as temporary_directories:
        log_dir = temporary_directories.enter_context(tempfile.TemporaryDirectory(
            prefix="chaoxi-async-", dir=tmpfs_root))
        tmpfs_dir = temporary_directories.enter_context(tempfile.TemporaryDirectory(
            prefix="chaoxi-log-tmpfs-", dir=tmpfs_root))
        page_cache_dir = temporary_directories.enter_context(
            tempfile.TemporaryDirectory(prefix="chaoxi-log-page-cache-",
                                        dir=output_dir))
        fsync_dir = temporary_directories.enter_context(tempfile.TemporaryDirectory(
            prefix="chaoxi-log-fsync-", dir=output_dir))
        benchmark_env = dict(os.environ)
        benchmark_env["CHAOXI_BENCHMARK_LOG_DIR"] = log_dir
        benchmark_env["CHAOXI_BENCHMARK_TMPFS_DIR"] = tmpfs_dir
        benchmark_env["CHAOXI_BENCHMARK_PAGE_CACHE_DIR"] = page_cache_dir
        benchmark_env["CHAOXI_BENCHMARK_FSYNC_DIR"] = fsync_dir
        resources = run_google_benchmarks(
            build_dir, output_dir, args.repetitions, args.quick, cpus,
            suites, benchmark_env)
        standalone_rows, standalone_resources = run_standalone_benchmarks(
            build_dir, output_dir, args.repetitions, cpus, suites,
            benchmark_env)
        resources.extend(standalone_resources)
        e2e = (run_e2e(build_dir, output_dir, args.repetitions, args.quick,
                       cpus, benchmark_env)
               if "net" in suites and os.name != "nt" else [])
        diagnostic_data = diagnostics(build_dir, output_dir, args.quick, suites,
                                      cpus, benchmark_env)

    e2e_summaries = summarize_e2e(e2e)
    standalone_summaries = summarize_standalone(standalone_rows)
    raw_rows, summaries = google_summary(output_dir)

    write_csv(output_dir / "google-benchmark-raw.csv", raw_rows)
    write_csv(output_dir / "google-benchmark-summary.csv", summaries)
    write_csv(output_dir / "resource-usage.csv", resources)
    write_csv(output_dir / "standalone-raw.csv", standalone_rows)
    write_csv(output_dir / "standalone-summary.csv", standalone_summaries)
    write_csv(output_dir / "e2e-raw.csv", e2e)
    write_csv(output_dir / "e2e-summary.csv", e2e_summaries)
    suite_summaries = {
        suite: {
            "google_benchmark": [row for row in summaries
                                 if row["suite"] == suite],
            "standalone": [row for row in standalone_summaries
                           if row["suite"] == suite],
            "resources": [row for row in resources if row["suite"] == suite],
            "e2e": e2e_summaries if suite == "net" else [],
            "diagnostics": diagnostic_data[suite],
        }
        for suite in suites
    }
    for suite, suite_summary in suite_summaries.items():
        write_csv(output_dir / f"{suite}-google-benchmark-summary.csv",
                  suite_summary["google_benchmark"])
        (output_dir / f"{suite}-summary.json").write_text(
            json.dumps(suite_summary, indent=2, ensure_ascii=False),
            encoding="utf-8")
    (output_dir / "summary.json").write_text(json.dumps({
        "google_benchmark": summaries,
        "standalone": standalone_summaries,
        "resources": resources,
        "e2e": e2e_summaries,
        "diagnostics": diagnostic_data,
        "suites": suite_summaries,
    }, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"Baseline written to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
