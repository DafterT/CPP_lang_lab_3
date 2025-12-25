import argparse
import json
import os

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns


BENCH_PREFIX = "BM_ThreadPoolOverhead"
CANDIDATE_FILES = ["../results_image.json", "../build/results_image.json"]
TIME_UNIT_FACTORS = {
    "ns": 1e-3,
    "us": 1.0,
    "ms": 1e3,
    "s": 1e6,
}


def convert_time(value, unit_from, unit_to):
    from_factor = TIME_UNIT_FACTORS.get(unit_from)
    to_factor = TIME_UNIT_FACTORS.get(unit_to)
    if from_factor is None or to_factor is None:
        return value
    return value * (from_factor / to_factor)


def has_overhead_bench(payload):
    for bench in payload.get("benchmarks", []):
        name = bench.get("name", "")
        if name.startswith(BENCH_PREFIX):
            return True
    return False


def parse_threads(name):
    parts = name.split("/")
    for part in parts[1:]:
        if part.isdigit():
            return int(part)
    return None


def load_payload(paths):
    data = None
    filename = None
    for path in paths:
        if not path or not os.path.exists(path):
            continue
        with open(path, "r") as handle:
            candidate = json.load(handle)
        if data is None:
            data = candidate
            filename = path
        if has_overhead_bench(candidate):
            data = candidate
            filename = path
            break
    return data, filename


def main():
    parser = argparse.ArgumentParser(
        description="Plot thread pool creation overhead from benchmark results."
    )
    parser.add_argument(
        "--input",
        "-i",
        help="Path to benchmark JSON file (defaults to common results files).",
        default=None,
    )
    parser.add_argument(
        "--unit",
        choices=sorted(TIME_UNIT_FACTORS.keys()),
        default="us",
        help="Time unit for plotting.",
    )
    parser.add_argument(
        "--output-prefix",
        default="thread_overhead",
        help="Output file prefix (no extension).",
    )
    parser.add_argument(
        "--no-per-thread",
        action="store_true",
        help="Skip the per-thread overhead plot.",
    )
    args = parser.parse_args()

    candidate_files = [args.input] if args.input else CANDIDATE_FILES
    data, filename = load_payload(candidate_files)
    if data is None or filename is None:
        print("Error: benchmark JSON file not found.")
        return 1

    records = []
    for bench in data.get("benchmarks", []):
        name = bench.get("name", "")
        if not name.startswith(BENCH_PREFIX):
            continue
        aggregate = bench.get("aggregate_name")
        if aggregate not in (None, "", "mean"):
            continue
        threads = parse_threads(name)
        if threads is None:
            continue
        time_value = bench.get("real_time")
        if time_value is None:
            time_value = bench.get("cpu_time")
        if time_value is None:
            continue
        time_unit = bench.get("time_unit", args.unit)
        time_value = convert_time(time_value, time_unit, args.unit)
        records.append({"Threads": threads, f"Time ({args.unit})": time_value})

    if not records:
        print("No thread pool overhead benchmarks found in the input file.")
        return 1

    df = pd.DataFrame(records).drop_duplicates()
    df = df.sort_values("Threads")

    sns.set_theme(style="whitegrid")
    time_col = f"Time ({args.unit})"

    plt.figure(figsize=(10, 6))
    ax = sns.lineplot(data=df, x="Threads", y=time_col, marker="o")
    ax.set_title("Thread pool creation overhead")
    ax.set_xlabel("Threads")
    ax.set_ylabel(time_col)
    ax.set_xticks(df["Threads"])
    plt.tight_layout()
    total_path = f"{args.output_prefix}_total.png"
    plt.savefig(total_path, dpi=150)
    plt.close()
    print(f"Saved: {total_path}")

    if not args.no_per_thread:
        df["Time per thread"] = df[time_col] / df["Threads"]
        plt.figure(figsize=(10, 6))
        ax = sns.lineplot(data=df, x="Threads", y="Time per thread", marker="o")
        ax.set_title("Thread pool creation overhead per thread")
        ax.set_xlabel("Threads")
        ax.set_ylabel(f"Time per thread ({args.unit})")
        ax.set_xticks(df["Threads"])
        plt.tight_layout()
        per_thread_path = f"{args.output_prefix}_per_thread.png"
        plt.savefig(per_thread_path, dpi=150)
        plt.close()
        print(f"Saved: {per_thread_path}")

    print(f"Using benchmark file: {filename}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
