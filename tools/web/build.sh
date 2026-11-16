#!/bin/sh
# tools/web/build.sh — rolltui-tour as a web page: the library and examples/tour.c compiled for wasm32-wasi with zig, beside
# xterm.js. The library's sources are read from a configured native build (CMake writes rolltui_sources.txt), so the page is
# built from exactly what the native build compiles.
#
#   tools/web/build.sh [NATIVE_BUILD_DIR [OUT_DIR]]     NATIVE_BUILD_DIR: build; OUT_DIR: NATIVE_BUILD_DIR/web
#
# Serve OUT_DIR over HTTP (a page cannot load a .wasm from file://): `python3 -m http.server -d OUT_DIR`.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
NATIVE=$(cd "${1:-$ROOT/build}" && pwd)
OUT=${2:-$NATIVE/web}
command -v zig > /dev/null || { echo "build.sh: needs zig, which carries clang and wasi-libc for wasm32-wasi" >&2; exit 1; }
[ -f "$NATIVE/rolltui_sources.txt" ] || { echo "build.sh: build $NATIVE first: CMake writes the source list there" >&2; exit 1; }
mkdir -p "$OUT/obj"
FLAGS="-target wasm32-wasi -std=gnu11 -O2 -I $NATIVE/include"
objs=""
for src in $(grep '\.c$' "$NATIVE/rolltui_sources.txt") examples/tour.c; do
  case "$src" in /*) path=$src ;; *) path=$ROOT/$src ;; esac
  obj=$OUT/obj/$(echo "$src" | sed 's|^/||; s|/|_|g; s|\.c$|.o|')
  zig cc $FLAGS -c "$path" -o "$obj"
  objs="$objs $obj"
done
zig cc $FLAGS -mexec-model=reactor -o "$OUT/rolltui-tour.wasm" $objs
cp "$ROOT/tools/web/index.html" "$ROOT/tools/web/rolltui_web.js" "$OUT/"
echo "$OUT/index.html"
