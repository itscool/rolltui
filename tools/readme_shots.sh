#!/bin/sh
# tools/readme_shots.sh — remakes the README's screenshots: the real dirktui, in a real Terminal window.
#
#   cmake --build build -j && tools/readme_shots.sh
#
# macOS only. Each picture is a Terminal.app window running `build/dirktui` on a staged demo project,
# captured with `screencapture`, then closed. Windows open and close on the screen while it runs (about
# five seconds each), and the first run asks for two permissions: to control Terminal, and to record the
# screen. The demo, its settings and its presets live in a temporary directory, removed afterwards; the
# person's own settings, presets and files are never read (ROLL_CONFIG_DIR points at the copy).
#
# WHAT IS KEPT OUT OF THE PICTURE:
#   - THE PATH. A breadcrumb drops whole parts from the left when the path is too long for its line, so
#     the demo sits under one very long directory name and what is left in view is the plain tail
#     `… › Users › ada › code › …`. The window's title is the folder's name and dirktui's short
#     relative argument, never the temporary directory.
#   - THE PERSON'S TERMINAL PROFILE. Every window uses Terminal's own "Basic" profile (SF Mono 11), opaque,
#     so nothing behind the window shows through; its title and background are set on the TAB, which
#     changes no profile. The background is the theme's own ground, so a dark theme is not framed in white.
#   - THE NETWORK. dirktui checks for an update at most once a day; the staged config says it just did.
#
#   - TERMINAL'S SCROLL BAR. With a mouse attached macOS draws a scroll bar's track whether or not there is
#     anything to scroll, and Terminal counts its main screen as scrollable while a program is on the alternate
#     one, so such a window has a track and half a thumb (with no mouse, macOS overlays them and there is nothing to cut). `trim` cuts the window just left of the track and
#     mirrors the left edge's border and rounded corners onto the new right edge. Nothing dirktui drew is
#     touched; a window with no track is left as it came.
set -eu
[ "$(uname)" = Darwin ] || { echo "readme_shots: macOS only (Terminal.app and screencapture)" >&2; exit 1; }
root=$(cd "$(dirname "$0")/.." && pwd)
bin="$root/build/dirktui"
selftest="$root/build/dirktui-selftest"
out="$root/docs/images"
for b in "$bin" "$selftest"; do [ -x "$b" ] || { echo "readme_shots: build first ($b is missing)" >&2; exit 1; }; done
tmp=$(mktemp -d)
window=""
cleanup() {
  [ -n "$window" ] && osascript -e "tell application \"Terminal\" to close (every window whose id is $window) saving no" >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT
mkdir -p "$out"

# ---- THE DEMO: a person's code folder, one project in several languages --------------------------------------
long=$(printf 'x%.0s' $(seq 1 200))
home="$tmp/$long/Users/ada"
code="$home/code"
wb="$code/hardware/sensors/weatherbox"
mkdir -p "$wb/src" "$wb/server" "$wb/docs" "$wb/tests" "$home/Documents" "$home/Music" "$code/notes" "$code/website" \
         "$code/hardware/keyboard" "$code/hardware/lamp" "$code/hardware/sensors/air-quality" \
         "$code/hardware/sensors/soil-probe/src" "$tmp/$long/Users/Shared"
cat > "$wb/README.md" <<'MD'
# weatherbox

A tiny station that reads its sensors and posts a reading every minute.

```mermaid
flowchart LR
  sensor[Sensor] --> filter{Valid?}
  filter -- yes --> store[(Store)]
  filter -- no --> log[Log it]
  store --> post[Post to feed]
```

## The loop

```c
while (sensor_read(&r) == 0) {
  if (r.celsius < -60 || r.celsius > 60) continue;  /* bad */
  printf("%.1f C, %d%% humidity\n", r.celsius, r.humidity);
}
```

| pin | sensor   | every |
|-----|----------|-------|
| 4   | BME280   | 60 s  |
| 17  | rain gauge | 10 s |
MD
printf 'weatherbox: src/main.c\n\tcc -O2 -o $@ src/main.c\n' > "$wb/Makefile"
printf '/* sensor.h: one reading from whatever is wired in */\nint sensor_read(void* reading);\n' > "$wb/src/sensor.h"
cat > "$wb/src/main.c" <<'C'
/* weatherbox: a reading every minute */
#include <stdio.h>
#include <unistd.h>
#include "sensor.h"

#define PERIOD_S 60

int main(void) {
  Reading r;
  while (sensor_read(&r) == 0) {
    printf("%.1f C\n", r.celsius);
    sleep(PERIOD_S);
  }
  return 1;
}
C
# The gallery's files. Short lines on purpose: its preview is about 46 columns wide, and a longer line ends in `…`.
cat > "$code/hardware/sensors/air-quality/upload.py" <<'PY'
"""Post each reading to the feed."""
import json
import time
from urllib import request

FEED = "https://feed.example/readings"
RETRIES = 3

def post(reading: dict) -> bool:
    body = json.dumps(reading).encode()
    req = request.Request(FEED, data=body)
    for attempt in range(RETRIES):
        try:
            request.urlopen(req, timeout=5)
            return True
        except OSError as err:
            print(f"retry {attempt}: {err}")
            time.sleep(2 ** attempt)
    return False
PY
cat > "$code/hardware/sensors/soil-probe/src/filter.rs" <<'RS'
//! Drop readings no probe could produce.
use std::ops::RangeInclusive;
const MOIST: RangeInclusive<u16> = 0..=1023;

#[derive(Debug, Clone, Copy)]
pub struct Reading {
    pub moisture: u16,
    pub celsius: f32,
}

impl Reading {
    pub fn plausible(&self) -> bool {
        MOIST.contains(&self.moisture)
            && self.celsius > -40.0
    }
}

pub fn keep(all: &[Reading]) -> Vec<Reading> {
    all.iter().copied()
        .filter(Reading::plausible)
        .collect()
}
RS
cat > "$wb/server/main.go" <<'GO'
// The station's small status server.
package main

import (
	"fmt"
	"log"
	"net/http"
	"time"
)

var started = time.Now()

func status(w http.ResponseWriter,
	r *http.Request) {
	up := time.Since(started)
	fmt.Fprintf(w, "up %s\n",
		up.Round(time.Second))
}

func main() {
	http.HandleFunc("/status", status)
	log.Fatal(http.ListenAndServe(":80", nil))
}
GO
cat > "$code/website/chart.ts" <<'TS'
// Draw the last day of readings.
interface Reading {
  at: Date;
  celsius: number;
}

const HOURS = 24;

export function hourly(rs: Reading[]) {
  const out = new Array<number>(HOURS);
  out.fill(0);
  for (const r of rs) {
    out[r.at.getHours()] = r.celsius;
  }
  return out;
}

export async function load(url: string) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(url);
  return (await res.json()) as Reading[];
}
TS
cat > "$wb/station.yaml" <<'YAML'
# weatherbox: what is wired to which pin
station:
  name: back garden
  feed: https://feed.example/readings
  every: 60s

sensors:
  - kind: BME280
    pin: 4
    reads: [celsius, humidity]
  - kind: rain gauge
    pin: 17
    every: 10s
    reads: [millimetres]

alerts:
  frost: { below: 0, notify: true }
  storm: { above_mm: 20, notify: true }
YAML
cat > "$wb/deploy.sh" <<'SH'
#!/bin/sh
# Build, copy to the station, restart it.
set -eu
host=${1:-weatherbox.local}
bin=build/weatherbox

make -j4
if ! ssh "$host" true 2>/dev/null; then
  echo "cannot reach $host" >&2
  exit 1
fi
scp "$bin" "$host:/usr/local/bin/"
ssh "$host" 'sudo systemctl restart weather'
echo "deployed to $host"
SH
# The info row shows when a file was last written: a fixed time, so a picture changes only when the program does.
find "$tmp/$long" -exec touch -t 202606011234 {} +

# ---- THE CONFIG: the preview on, no motion or sparkle (a still of an animated cursor catches its glow
# part-way), and an update check that has just happened -------------------------------------------------------
cfg="$tmp/cfg"
mkdir -p "$cfg/rolltui/dirktui"
echo '{ "preview": "right", "motion": false, "sparkle": false }' > "$cfg/rolltui/dirktui/settings.json"
echo '{ "latest": "", "checked_ms": 1000000000000000, "update_since_ms": 0 }' > "$cfg/rolltui/dirktui/update.json"
# The headless twin that reads each frame's ground needs the shipped presets themselves to point at.
cp -R "$root/examples/presets" "$tmp/presets"

# trim PNG — Terminal's scroll bar off the right edge, as described at the top. `sips` hands the pixels over as a
# 32-bit BMP (it has no raw format), and the result is written back as a PNG by hand: no image library needed.
cat > "$tmp/trim.py" <<'PY'
import struct, sys, zlib
png, bmp = sys.argv[1], sys.argv[2]
b = open(bmp, "rb").read()
off = struct.unpack_from("<I", b, 10)[0]
w, h = struct.unpack_from("<ii", b, 18)
H = abs(h)
assert struct.unpack_from("<H", b, 28)[0] == 32, "expected 32-bit pixels"
rows = []
for y in range(H):
    yy = y if h < 0 else H - 1 - y
    raw = b[off + yy * w * 4: off + (yy + 1) * w * 4]
    rows.append([list(raw[i + 2::-1][:3]) + [raw[i + 3]] for i in range(0, len(raw), 4)])  # BGRA -> RGBA
margin = rows[H // 2][10]                     # the left padding: the tab's background
top, bottom = H // 8, H - H // 8               # below the title bar, above the bottom corners
sample = range(top, bottom, 7)
def foreign(x):
    return all(rows[y][x] != margin for y in sample)
x = w - 1
while x > w - 12 and not foreign(x):           # the window's own edge, a few pixels
    x -= 1
if not foreign(x):
    sys.exit(0)                                # no track: the window is left as it came
while x > w - 120 and foreign(x):
    x -= 1
if foreign(x):
    sys.exit(0)                                # not a narrow strip: not a scroll bar, leave it
W = x + 1 - 8                                  # the margin, less the few pixels the scroll area's top button and corner start early
for row in rows:
    del row[W:]
    for i in range(2):                         # the window's edge, from the left one
        row[W - 1 - i] = list(row[i])
R = 32                                         # the corner radius and a little more, in pixels
for y in list(range(R)) + list(range(H - R, H)):
    for i in range(R):
        if rows[y][i][3] < 255:                # transparent or antialiased: the corner's own shape
            rows[y][W - 1 - i] = list(rows[y][i])
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
data = bytearray()
for row in rows:
    line = bytes(v for px in row for v in px)
    sub = bytes((line[i] - (line[i - 4] if i >= 4 else 0)) & 255 for i in range(len(line)))
    data += b"\x01" + sub
with open(png, "wb") as f:
    f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(data), 9)) + chunk(b"IEND", b""))
PY
trim() {
  sips -s format bmp "$1" --out "$tmp/shot.bmp" >/dev/null
  python3 "$tmp/trim.py" "$1" "$tmp/shot.bmp"
}

# shot OUT THEME SIDE FILE COLS ROWS — one window, one picture. FILE is relative to ~ada/code.
shot() {
  png=$1 theme=$2 side=$3 file=$4 cols=$5 rows=$6
  # Light or dark is the person's setting, kept in the theme store's working file beside the preset it follows.
  printf '{\n  "preset": "%s",\n  "follows_origin": true,\n  "mode": "%s"\n}\n' "$theme" "$side" > "$cfg/rolltui/theme.working.json"
  cp "$cfg/rolltui/theme.working.json" "$tmp/presets/theme.working.json"
  # The ground the frame is painted on, read from the same frame drawn headless, for the window's margin.
  # Mono paints nothing: its ground is the terminal's own, Basic's white.
  ground=$(cd "$code" && ROLL_CONFIG_DIR="$cfg" "$selftest" --presets "$tmp/presets" --frame-sgr "${cols}x${rows}" "$file" 2>/dev/null |
    python3 -c 'import re,sys,collections
frame=sys.stdin.read()
if "\x1b[" not in frame: sys.exit("readme_shots: the headless frame came back empty")
c=collections.Counter(re.findall(r"48;2;(\d+);(\d+);(\d+)m",frame))
print(",".join(str(int(v)*257) for v in c.most_common(1)[0][0]) if c else "65535,65535,65535")')
  cat > "$tmp/launch.sh" <<EOF
#!/bin/sh
cd "$code" || exit 1
printf '\033]7;file://%s%s\007' "\$(hostname)" "\$PWD"
printf '\033[H\033[2J\033[3J'
echo \$\$ > "$tmp/pid"
ROLL_CONFIG_DIR="$cfg" exec "$bin" "$file"
EOF
  chmod +x "$tmp/launch.sh"
  rm -f "$tmp/pid"
  window=$(osascript -e "tell application \"Terminal\"
    set t to do script \"\"
    set w to (first window whose selected tab is t)
    set current settings of t to settings set \"Basic\"
    set background color of t to {$ground}
    set custom title of t to \"dirktui\"
    set title displays custom title of t to true
    set title displays window size of t to false
    set number of columns of w to $cols
    set number of rows of w to $rows
    do script \"exec '$tmp/launch.sh'\" in t
    return id of w
  end tell")
  for _ in 1 2 3 4 5 6 7 8 9 10; do [ -s "$tmp/pid" ] && break; sleep 0.5; done
  [ -s "$tmp/pid" ] || { echo "readme_shots: dirktui did not start for $png" >&2; exit 1; }
  sleep 2  # the terminal's answers, then the first frame
  screencapture -l "$window" -o -x "$png"
  trim "$png"
  kill "$(cat "$tmp/pid")" 2>/dev/null || true
  osascript -e "tell application \"Terminal\" to close (every window whose id is $window) saving no" >/dev/null
  window=""
  echo "$png"
}

# THE HERO: the default theme, both sides; the README shows whichever matches the reader's own.
shot "$out/dirktui-dark.png"  default dark  hardware/sensors/weatherbox/README.md 150 36
shot "$out/dirktui-light.png" default light hardware/sensors/weatherbox/README.md 150 36

# THE GALLERY: six themes, six languages. Mono has no colour of its own, only bold, dim, italic, underline and
# reverse, so its side is the terminal's: Basic's black on white.
shot "$out/theme-catppuccin.png" catppuccin dark  hardware/sensors/air-quality/upload.py     100 30
shot "$out/theme-solarized.png"  solarized  light hardware/sensors/soil-probe/src/filter.rs 100 30
shot "$out/theme-ember.png"      ember      dark  hardware/sensors/weatherbox/server/main.go 100 30
shot "$out/theme-orchard.png"    orchard    light website/chart.ts                          100 30
shot "$out/theme-harbour.png"    harbour    dark  hardware/sensors/weatherbox/station.yaml  100 30
shot "$out/theme-mono.png"       mono       light hardware/sensors/weatherbox/deploy.sh     100 30
# The gallery is shown at half the README's width: no need to carry Retina pixels nobody sees.
for f in "$out"/theme-*.png; do sips -Z 1100 "$f" >/dev/null; done
# The pixels and nothing else: screencapture and sips add EXIF, XMP and colour chunks (a capture's size, the
# word "Screenshot"), and a picture published in a repository has no use for any of it.
for f in "$out"/*.png; do python3 - "$f" <<'PY'
import struct, sys
p = sys.argv[1]
b = open(p, "rb").read()
out, i = bytearray(b[:8]), 8
while i < len(b):
    n = struct.unpack(">I", b[i:i + 4])[0]
    if b[i + 4:i + 8] in (b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND"):
        out += b[i:i + 12 + n]
    i += 12 + n
open(p, "wb").write(bytes(out))
PY
done
