#!/usr/bin/env bash
#
# Repeat-measurement harness for the three workloads.
#
# Why this exists: docs/시행착오_기록.md #8 records that a single timing of
# io_bound gave 2.058s once and 0.686s the next time -- a 3x swing from
# nothing but cache warming. That entry ends with "benchmarks must be
# measured repeatedly", which was a note-to-self rather than a result.
# This script closes that loop: it runs each workload N times, reports the
# per-run values so the first-run outlier stays visible, and then hands the
# same workload to `perf stat -r N` for mean +- stddev on the counters.
#
# Usage:
#   bash scripts/bench.sh [runs]        # default: 5
#   PERF=/path/to/perf bash scripts/bench.sh 10
#
# Environment-specific: the default PERF path is what works on this Tegra
# kernel (6.8.12-1021-tegra has no matching linux-tools package -- see
# README). Override PERF anywhere else.
#
# Needs sudo: hardware PMU counters and kernel-mode cycles are not visible
# to an unprivileged process, which is exactly the ":u scope" trap
# documented in docs/log.md section 8.

set -uo pipefail

RUNS="${1:-5}"
PERF="${PERF:-/usr/lib/linux-tools/6.8.0-138-generic/perf}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$REPO_ROOT/src"

WORKLOADS="hot io_bound multithread"

if [ ! -x "$PERF" ]; then
  echo "error: no perf binary at $PERF" >&2
  echo "       available: $(ls /usr/lib/linux-tools/*/perf 2>/dev/null || echo none)" >&2
  echo "       retry as: PERF=/usr/lib/linux-tools/<ver>/perf bash scripts/bench.sh" >&2
  exit 1
fi

echo "== build =="
make -C "$SRC_DIR" all || exit 1
echo

echo "== environment =="
echo "kernel   : $(uname -r)"
echo "cores    : $(nproc)"
echo "perf     : $PERF ($("$PERF" --version))"
echo "runs     : $RUNS"
echo "governor : $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
echo "nvpmodel : $(nvpmodel -q 2>/dev/null | tr '\n' ' ' || echo n/a)"
echo "/tmp fs  : $(findmnt -no FSTYPE --target /tmp 2>/dev/null || echo unknown)"
echo "protected_regular : $(sysctl -n fs.protected_regular 2>/dev/null || echo n/a)"
echo

# ---------------------------------------------------------------------------
# Pass 1: wall-clock per run, so the first-run outlier is visible.
# ---------------------------------------------------------------------------
echo "== pass 1: wall clock, per run (ms) =="
for w in $WORKLOADS; do
  bin="$SRC_DIR/$w"
  times=""
  for _ in $(seq 1 "$RUNS"); do
    start=$(date +%s%N)
    "$bin" >/dev/null 2>&1
    end=$(date +%s%N)
    times="$times $(( (end - start) / 1000000 ))"
  done

  # mean / sample stddev / min / max, printed as a markdown table row
  echo "$times" | awk -v name="$w" '{
    n = NF; sum = 0; min = $1; max = $1
    for (i = 1; i <= n; i++) {
      sum += $i
      if ($i < min) min = $i
      if ($i > max) max = $i
    }
    mean = sum / n
    ss = 0
    for (i = 1; i <= n; i++) ss += ($i - mean) * ($i - mean)
    sd = (n > 1) ? sqrt(ss / (n - 1)) : 0
    printf "| %-12s |", name
    for (i = 1; i <= n; i++) printf " %d |", $i
    printf " %.1f | %.1f | %d-%d | %.1f%% |\n", mean, sd, min, max,
           (mean > 0 ? 100 * sd / mean : 0)
  }'
done
echo "(columns: workload | run1..runN | mean | stddev | range | rel.stddev)"
echo

# ---------------------------------------------------------------------------
# Pass 2: let perf aggregate the counters itself. `perf stat -r N` reports
# mean and relative stddev per event, which is the idiomatic way to do this
# and cross-checks pass 1.
# ---------------------------------------------------------------------------
echo "== pass 2: sudo perf stat -r $RUNS (counters, mean +- stddev) =="
for w in $WORKLOADS; do
  echo "--- $w ---"
  sudo "$PERF" stat -r "$RUNS" -- "$SRC_DIR/$w" 2>&1 >/dev/null | sed 's/^/  /'
  echo
done

echo "== cleanup =="
# io_bound writes ~128MB per run. Its path is per-uid, so both the user-mode
# runs (pass 1) and the root runs (pass 2) leave a file behind.
for f in /tmp/io_bound_test.*.bin; do
  [ -e "$f" ] || continue
  echo "  removing $f ($(du -h "$f" | cut -f1), owner $(stat -c %U "$f"))"
  rm -f "$f" 2>/dev/null || sudo rm -f "$f"
done
echo

echo "done. paste the whole output into docs/반복측정_결과.md"
