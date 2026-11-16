#!/bin/sh
# run.sh — rolltui and ratatui drawing the same screens, side by side: the time a frame takes and the bytes a terminal is sent.
#
#   tools/bench/run.sh [FRAMES]
#
# Builds rolltui in Release and tools/bench/rolltui_bench.c against it, and tools/bench/ratatui with cargo (ratatui pinned in its
# Cargo.toml), both in a temporary directory; then runs each screen five times at 200x50 and 80x24 and prints the median of each as
# a Markdown table. The screens are described in the two sources. cargo is looked for on PATH and in ~/.cargo/bin.
set -eu

HERE=$(cd "$(dirname "$0")/../.." && pwd)
FRAMES=${1:-3000}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/rolltui-bench.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

CARGO=$(command -v cargo || true)
[ -n "$CARGO" ] || CARGO="$HOME/.cargo/bin/cargo"
[ -x "$CARGO" ] || { echo "run.sh: cargo is needed for the ratatui side" >&2; exit 1; }

echo "building rolltui (Release)…" >&2
cmake -S "$HERE" -B "$WORK/rolltui" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$WORK/rolltui" --target rolltui -j8 >/dev/null
cc -O2 -std=c11 -I"$HERE/.." "$HERE/tools/bench/rolltui_bench.c" "$WORK/rolltui/librolltui.a" -lpthread -lm -o "$WORK/rolltui_bench"

echo "building the ratatui bench…" >&2
CARGO_TARGET_DIR="$WORK/cargo" "$CARGO" build --release --quiet --manifest-path "$HERE/tools/bench/ratatui/Cargo.toml"
RAT="$WORK/cargo/release/ratbench"

# The median of five runs: "us bytes".
median() {
  for _ in 1 2 3 4 5; do "$@"; done | sed -E 's/.*: ([0-9.]+) us\/frame, ([0-9]+) bytes\/frame/\1 \2/' | sort -n | sed -n 3p
}

printf '| Screen | Size | rolltui | ratatui, rows on screen | ratatui, every row |\n|---|---|---|---|---|\n'
for size in "200 50" "80 24"; do
  set -- $size
  for scenario in move scroll tick; do
    r=$(median "$WORK/rolltui_bench" "$1" "$2" "$scenario" "$FRAMES")
    if [ "$scenario" = tick ]; then
      s=$(median "$RAT" "$1" "$2" tick "$FRAMES")
      a="$s"
    else
      s=$(median "$RAT" "$1" "$2" "$scenario-sliced" "$FRAMES")
      a=$(median "$RAT" "$1" "$2" "$scenario" "$FRAMES")
    fi
    cell() { set -- $1; printf '%.2f ms, %s B' "$(echo "$1 / 1000" | bc -l)" "$2"; }
    printf '| %s | %sx%s | %s | %s | %s |\n' "$scenario" "$1" "$2" "$(cell "$r")" "$(cell "$s")" "$(cell "$a")"
  done
done
