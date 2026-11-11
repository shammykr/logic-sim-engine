#!/usr/bin/env bash
# Build logicsim, generate the large netlist, and benchmark all three sizes.
#
#   benchmarks/run_benchmarks.sh [cycles] [repeats]
#
# Each netlist is simulated for `cycles` clock cycles (default 1000) of random input vectors,
# `repeats` times (default 3); the table reports the median run. The large netlist is also run
# with each optimisation disabled and with VCD output enabled. Raw output goes to
# benchmarks/results/.
set -euo pipefail

cd "$(dirname "$0")/.."
CYCLES=${1:-1000}
REPEATS=${2:-3}
BIN=build/logicsim
OUT=benchmarks/results
mkdir -p "$OUT" benchmarks/generated

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build -j > /dev/null

if [ ! -f benchmarks/generated/large_100k.v ]; then
  python3 benchmarks/gen_netlist.py --gates 100000 --out benchmarks/generated/large_100k.v
fi

field() { grep "^$1 " "$2" | awk '{print $2}'; }

# run <label> <netlist> [extra args...]: median-of-REPEATS run, prints one table row
run() {
  local label=$1 netlist=$2; shift 2
  local runs=()
  for i in $(seq 1 "$REPEATS"); do
    local f="$OUT/${label// /_}.$i.txt"
    "$BIN" "$netlist" --random "$CYCLES" --clock clk --reset rst --quiet "$@" > "$f"
    runs+=("$(field sim_seconds "$f") $f")
  done
  local median
  median=$(printf '%s\n' "${runs[@]}" | sort -n | awk -v n="$REPEATS" 'NR == int((n + 1) / 2) {print $2}')
  local gates
  gates=$(grep '^gates ' "$median" | awk '{print $2}')
  printf '| %-28s | %7s | %8s | %12s | %12s | %10s | %7s |\n' "$label" "$gates" \
    "$(field sim_seconds "$median")" "$(field events_processed "$median")" \
    "$(field events_suppressed "$median")" "$(field events_per_second "$median")" \
    "$(field peak_rss_mb "$median")"
}

echo "machine: $(uname -sm), $( (sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo | cut -d: -f2) | sed 's/^ *//'), compiler: $( (c++ --version | head -1) )"
echo "stimulus: $CYCLES cycles of random vectors (2 time steps per cycle), median of $REPEATS runs"
echo
printf '| %-28s | %7s | %8s | %12s | %12s | %10s | %7s |\n' netlist gates "sim (s)" "events" "suppressed" "events/s" "RSS MB"
printf '|%s|%s|%s|%s|%s|%s|%s|\n' "------------------------------" "---------" "----------" "--------------" "--------------" "------------" "---------"
run "small (100)" benchmarks/small_100.v
run "medium (10K)" benchmarks/medium_10k.v
run "large (100K)" benchmarks/generated/large_100k.v
run "large, --no-pool" benchmarks/generated/large_100k.v --no-pool
run "large, --no-dedup" benchmarks/generated/large_100k.v --no-dedup
run "large, --no-dedup --no-pool" benchmarks/generated/large_100k.v --no-dedup --no-pool
run "large + VCD" benchmarks/generated/large_100k.v --vcd "$OUT/large.vcd"
echo
grep '^vcd ' "$OUT"/large_+_VCD.1.txt || true
rm -f "$OUT/large.vcd"
