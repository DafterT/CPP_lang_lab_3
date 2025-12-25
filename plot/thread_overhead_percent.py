import argparse
import json
import os

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns


BENCH_OVERHEAD = "BM_ThreadPoolOverhead"
BENCH_METHODS = {
    "BM_ProcessThreadPool": {
        "group": "ThreadPool",
        "suffix": "threadpool",
        "label": "ThreadPool",
    },
    "BM_ProcessThreadPoolFull": {
        "group": "ThreadPool Rows",
        "suffix": "threadpool_rows",
        "label": "ThreadPool Rows",
    },
}
CANDIDATE_FILES = ["../results_image.json", "../build/results_image.json"]
TIME_UNIT_FACTORS = {
    "ns": 1e-3,
    "us": 1.0,
    "ms": 1e3,
    "s": 1e6,
}
THREAD_COUNTS = [1, 4, 8, 16]


def convert_time(value, unit_from, unit_to):
    from_factor = TIME_UNIT_FACTORS.get(unit_from)
    to_factor = TIME_UNIT_FACTORS.get(unit_to)
    if from_factor is None or to_factor is None:
        return value
    return value * (from_factor / to_factor)


def has_benchmarks(payload):
    for bench in payload.get("benchmarks", []):
        name = bench.get("name", "")
        if name.startswith(BENCH_OVERHEAD):
            return True
        parts = name.split("/")
        if len(parts) > 1 and parts[1] in BENCH_METHODS:
            return True
    return False


def parse_overhead_threads(name):
    parts = name.split("/")
    for part in parts[1:]:
        if part.isdigit():
            return int(part)
    return None


def parse_method_benchmark(bench):
    name = bench.get("name", "")
    parts = name.split("/")
    if len(parts) < 3:
        return None
    method_raw = parts[1]
    if method_raw not in BENCH_METHODS:
        return None

    numeric_parts = [int(part) for part in parts[2:] if part.isdigit()]
    if len(numeric_parts) < 2:
        return None
    image_size = numeric_parts[0]
    kernel_size = numeric_parts[1]

    threads = None
    for part in parts:
        if part.startswith("threads:") and part[8:].isdigit():
            threads = int(part[8:])
            break
        if part.startswith("thread:") and part[7:].isdigit():
            threads = int(part[7:])
            break
    if threads is None and len(numeric_parts) > 2:
        threads = numeric_parts[2]

    bench_threads = bench.get("threads")
    if threads is None and isinstance(bench_threads, int) and bench_threads > 0:
        threads = bench_threads

    if threads is None:
        return None

    return method_raw, image_size, kernel_size, threads


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
        if has_benchmarks(candidate):
            data = candidate
            filename = path
            break
    return data, filename


def main():
    parser = argparse.ArgumentParser(
        description="Plot thread creation overhead share for image benchmarks."
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
        "--output-dir",
        default=".",
        help="Directory to store generated images.",
    )
    args = parser.parse_args()

    candidate_files = [args.input] if args.input else CANDIDATE_FILES
    data, filename = load_payload(candidate_files)
    if data is None or filename is None:
        print("Error: benchmark JSON file not found.")
        return 1

    overhead_records = []
    process_records = []
    for bench in data.get("benchmarks", []):
        aggregate = bench.get("aggregate_name")
        if aggregate not in (None, "", "mean"):
            continue

        name = bench.get("name", "")
        time_value = bench.get("real_time")
        if time_value is None:
            time_value = bench.get("cpu_time")
        if time_value is None:
            continue
        time_unit = bench.get("time_unit", args.unit)
        time_value = convert_time(time_value, time_unit, args.unit)

        if name.startswith(BENCH_OVERHEAD):
            threads = parse_overhead_threads(name)
            if threads is None:
                continue
            overhead_records.append({"Threads": threads, f"Time ({args.unit})": time_value})
            continue

        parsed = parse_method_benchmark(bench)
        if parsed is None:
            continue
        method_raw, image_size, kernel_size, threads = parsed
        method_info = BENCH_METHODS[method_raw]
        process_records.append(
            {
                "Method": method_info["label"],
                "Method Group": method_info["group"],
                "Method Suffix": method_info["suffix"],
                "Image Size": image_size,
                "Kernel Size": kernel_size,
                "Threads": threads,
                f"Time ({args.unit})": time_value,
            }
        )

    if not overhead_records:
        print("No thread pool overhead benchmarks found in the input file.")
        return 1

    if not process_records:
        print("No thread pool image benchmarks found in the input file.")
        return 1

    overhead_df = pd.DataFrame(overhead_records).drop_duplicates()
    time_col = f"Time ({args.unit})"
    overhead_map = overhead_df.groupby("Threads")[time_col].mean().to_dict()

    df = pd.DataFrame(process_records)
    df["Overhead Time"] = df["Threads"].map(overhead_map)
    df = df.dropna(subset=["Overhead Time"])
    df["Overhead (%)"] = df["Overhead Time"] / df[time_col] * 100.0
    df["Threads Label"] = df["Threads"].apply(lambda v: str(int(v)))
    df = df[df["Threads"].isin(THREAD_COUNTS)]

    if df.empty:
        print("No matching thread counts (1, 4, 8, 16) found in the input file.")
        return 1

    os.makedirs(args.output_dir, exist_ok=True)
    sns.set_theme(style="whitegrid")
    saved = []

    kernel_sizes = sorted(df["Kernel Size"].unique())
    for kernel_size in kernel_sizes:
        for method_group, method_suffix in {
            "ThreadPool": "threadpool",
            "ThreadPool Rows": "threadpool_rows",
        }.items():
            subset = df[
                (df["Kernel Size"] == kernel_size)
                & (df["Method Group"] == method_group)
            ]
            if subset.empty:
                continue
            subset = (
                subset.groupby(["Image Size", "Threads", "Threads Label"], as_index=False)[
                    "Overhead (%)"
                ]
                .mean()
                .sort_values(["Image Size", "Threads"])
            )
            image_sizes = sorted(subset["Image Size"].unique())
            label_order = [str(size) for size in image_sizes]
            subset = subset.copy()
            subset["Image Size Label"] = pd.Categorical(
                subset["Image Size"].astype(str),
                categories=label_order,
                ordered=True,
            )

            plt.figure(figsize=(10, 6))
            ax = sns.lineplot(
                data=subset,
                x="Image Size Label",
                y="Overhead (%)",
                hue="Threads Label",
                hue_order=[str(t) for t in THREAD_COUNTS],
                marker="o",
            )
            ax.set_title(
                f"Thread creation overhead share - {method_group} (Kernel {kernel_size}x{kernel_size})"
            )
            ax.set_xlabel("Image size (NxN)")
            ax.set_ylabel("Thread creation overhead (%)")
            ax.legend(title="Threads")
            plt.tight_layout()

            filename_out = os.path.join(
                args.output_dir,
                f"thread_overhead_percent_kernel_{kernel_size}_{method_suffix}.png",
            )
            plt.savefig(filename_out, dpi=150)
            plt.close()
            saved.append(filename_out)
            print(f"Saved: {filename_out}")

    if not saved:
        print("No plots were generated. Check input data filters.")
        return 1

    print(f"Using benchmark file: {filename}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
