#!/usr/bin/env python3
"""Render the report graphs from the benchmark CSVs in bench/.

Inputs (produced by `make bench`):
  bench/throughput_writer.csv    RCU vs rwlock, with a concurrent writer
  bench/throughput_nowriter.csv  RCU vs rwlock, read-only
  bench/structcmp.csv            Patricia vs multibit (speed/memory)

Outputs: matching PNGs in bench/. Requires matplotlib.
"""
import csv
import os
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("matplotlib is required: pip install matplotlib")

BENCH = os.path.join(os.path.dirname(__file__), "..", "bench")


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def plot_throughput(csv_name, png_name, title):
    path = os.path.join(BENCH, csv_name)
    if not os.path.exists(path):
        print("skip (missing):", path)
        return
    rows = read_csv(path)
    series = {}
    for r in rows:
        series.setdefault(r["backend"], ([], []))
        series[r["backend"]][0].append(int(r["threads"]))
        series[r["backend"]][1].append(float(r["mlookups_per_sec"]))

    plt.figure(figsize=(7, 4.5))
    for backend, (xs, ys) in sorted(series.items()):
        pts = sorted(zip(xs, ys))
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        plt.plot(xs, ys, marker="o", label=backend.upper())
    plt.xlabel("reader threads")
    plt.ylabel("throughput (Mlookups/s)")
    plt.title(title)
    plt.grid(True, alpha=0.3)
    plt.legend()
    out = os.path.join(BENCH, png_name)
    plt.tight_layout()
    plt.savefig(out, dpi=120)
    print("wrote", out)


def plot_structcmp():
    path = os.path.join(BENCH, "structcmp.csv")
    if not os.path.exists(path):
        print("skip (missing):", path)
        return
    rows = read_csv(path)
    labels, speed, mem = [], [], []
    for r in rows:
        name = r["structure"]
        if name == "multibit":
            name += " s" + r["stride"]
        labels.append(name)
        speed.append(float(r["mlookups_per_sec"]))
        mem.append(float(r["mem_kb"]) / 1024.0)  # MB

    fig, ax1 = plt.subplots(figsize=(7, 4.5))
    x = range(len(labels))
    ax1.bar([i - 0.2 for i in x], speed, width=0.4, label="speed (Mlps)", color="tab:blue")
    ax1.set_ylabel("throughput (Mlookups/s)", color="tab:blue")
    ax1.set_xticks(list(x))
    ax1.set_xticklabels(labels)
    ax2 = ax1.twinx()
    ax2.bar([i + 0.2 for i in x], mem, width=0.4, label="memory (MB)", color="tab:red")
    ax2.set_ylabel("memory (MB)", color="tab:red")
    plt.title("Patricia vs multibit: speed/memory trade-off")
    fig.tight_layout()
    out = os.path.join(BENCH, "structcmp.png")
    plt.savefig(out, dpi=120)
    print("wrote", out)


def main():
    plot_throughput("throughput_writer.csv", "throughput_writer.png",
                    "Lookup throughput with a concurrent writer (RCU vs rwlock)")
    plot_throughput("throughput_nowriter.csv", "throughput_nowriter.png",
                    "Lookup throughput, read-only (RCU vs rwlock)")
    plot_structcmp()


if __name__ == "__main__":
    main()
