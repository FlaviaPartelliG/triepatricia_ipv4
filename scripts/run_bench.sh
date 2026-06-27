#!/usr/bin/env bash
# Full experimental sweep for the report: builds, generates the table, runs the
# RCU-vs-rwlock throughput benchmarks and the structure comparison, then plots.
set -euo pipefail

cd "$(dirname "$0")/.."

SECONDS_PER_POINT="${SECONDS_PER_POINT:-3}"
THREADS="${THREADS:-1,2,4,8,16}"
GEN_N="${GEN_N:-100000}"

echo "==> build + generate table ($GEN_N prefixes)"
make all
make gen GEN_N="$GEN_N"

mkdir -p bench

echo "==> throughput: RCU vs rwlock, with concurrent writer"
bin/bench --table data/prefixes.txt --threads "$THREADS" \
    --seconds "$SECONDS_PER_POINT" --writer on \
    --out bench/throughput_writer.csv

echo "==> throughput: RCU vs rwlock, read-only"
bin/bench --table data/prefixes.txt --threads "$THREADS" \
    --seconds "$SECONDS_PER_POINT" --writer off \
    --out bench/throughput_nowriter.csv

echo "==> structure comparison: Patricia vs multibit"
bin/bench_struct --table data/prefixes.txt --packets 10000000 \
    --out bench/structcmp.csv

echo "==> plotting"
if command -v python3 >/dev/null && python3 -c 'import matplotlib' 2>/dev/null; then
    python3 scripts/plot.py
else
    echo "   (matplotlib not available; CSVs are in bench/)"
fi

echo "==> done. CSV + PNG outputs are in bench/"
