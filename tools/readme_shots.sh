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
#     the demo sits under a directory named with 200 spaces: what is left in view is the plain tail
#     `… › Users › ada › code › …`, and where a popup covers the breadcrumb and the path is drawn as text,
#     its head is blank. The window's title is the folder's name and the program's short argument.
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
long=$(printf ' %.0s' $(seq 1 200))
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
# The rest of the demo tree. upload.py is the code picture's file; its lines fit a 46-column preview.
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

# The relay runs a program on a pty the size of this terminal and passes bytes both ways. Two seconds after start
# it types RELAY_KEYS, one step every quarter second, as the bytes a terminal sends for them, then creates
# RELAY_DONE a moment after the last. Keys: F1-F5, Up, Down, Left, Right, Enter, Tab, CtrlP, Type:text (an
# underscore is a space), Wait:ms. It runs under the program's own name with the program's own arguments, so the
# window's title names nothing else: RELAY_EXEC is the binary and RELAY_DIR the folder it starts in.
cat > "$tmp/relay.c" <<'C'
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

typedef struct { double delay; char bytes[256]; size_t n; } Step;

static double now(void) { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec + t.tv_usec / 1e6; }
static void write_all(int fd, const char* p, size_t n) {
  while (n) { ssize_t k = write(fd, p, n); if (k <= 0) { if (errno == EINTR) continue; return; } p += k; n -= (size_t)k; }
}
static size_t named(const char* t, char* out) {
  static const char* names[] = {"F1", "F2", "F3", "F4", "F5", "Up", "Down", "Right", "Left", "Enter", "Tab", "CtrlP"};
  static const char* bytes[] = {"\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~", "\033[A", "\033[B", "\033[C",
                                "\033[D", "\r", "\t", "\020"};
  for (size_t i = 0; i < sizeof names / sizeof *names; ++i)
    if (strcmp(t, names[i]) == 0) { strcpy(out, bytes[i]); return strlen(bytes[i]); }
  fprintf(stderr, "relay: unknown key %s\n", t);
  exit(2);
}

int main(int argc, char** argv) {
  (void)argc;
  const char* done = getenv("RELAY_DONE");
  const char* spec = getenv("RELAY_KEYS");
  Step steps[64];
  size_t count = 0;
  char buf[4096];
  snprintf(buf, sizeof buf, "%s", spec ? spec : "");
  for (char* t = strtok(buf, " "); t && count < 64; t = strtok(NULL, " ")) {
    Step* s = &steps[count++];
    s->delay = 0.25;
    s->n = 0;
    if (strncmp(t, "Wait:", 5) == 0) { s->delay = atoi(t + 5) / 1000.0; continue; }
    if (strncmp(t, "Type:", 5) == 0) {
      for (const char* c = t + 5; *c && s->n < sizeof s->bytes; ++c) s->bytes[s->n++] = *c == '_' ? ' ' : *c;
      continue;
    }
    s->n = named(t, s->bytes);
  }
  struct winsize ws;
  ioctl(0, TIOCGWINSZ, &ws);
  int master = -1;
  pid_t pid = forkpty(&master, NULL, NULL, &ws);
  if (pid < 0) return 1;
  if (pid == 0) {
    if (chdir(getenv("RELAY_DIR")) != 0) _exit(1);
    argv[0] = getenv("RELAY_EXEC");
    execv(argv[0], argv);
    _exit(127);
  }
  struct termios raw;
  tcgetattr(0, &raw);
  cfmakeraw(&raw);
  tcsetattr(0, TCSANOW, &raw);
  const double start = now();
  size_t i = 0;
  double due = start + 2.0 + (count ? steps[0].delay : 0), finished = -1;
  for (;;) {
    double t = now();
    if (i == count && finished < 0) finished = (t + 1.5 > start + 3.0) ? t + 1.5 : start + 3.0;
    double wake = i < count ? due : finished;
    struct timeval tv, *tvp = NULL;
    if (wake > 0) {
      double w = wake - t > 0 ? wake - t : 0;
      tv.tv_sec = (long)w;
      tv.tv_usec = (int)((w - (long)w) * 1e6);
      tvp = &tv;
    }
    fd_set r;
    FD_ZERO(&r);
    FD_SET(0, &r);
    FD_SET(master, &r);
    if (select(master + 1, &r, NULL, NULL, tvp) < 0 && errno != EINTR) break;
    if (FD_ISSET(master, &r)) {
      ssize_t k = read(master, buf, sizeof buf);
      if (k <= 0) break;
      write_all(1, buf, (size_t)k);
    }
    if (FD_ISSET(0, &r)) {
      ssize_t k = read(0, buf, sizeof buf);
      if (k > 0) write_all(master, buf, (size_t)k);
    }
    t = now();
    if (i < count && t >= due) {
      write_all(master, steps[i].bytes, steps[i].n);
      ++i;
      due = t + (i < count ? steps[i].delay : 0);
    }
    if (finished > 0 && t >= finished && done && access(done, F_OK) != 0) close(open(done, O_CREAT | O_WRONLY, 0644));
  }
  return 0;
}
C
cc -O2 -o "$tmp/relay" "$tmp/relay.c" -lutil 2>/dev/null || cc -O2 -o "$tmp/relay" "$tmp/relay.c"

# shot OUT THEME SIDE COLS ROWS KEYS -- PROGRAM ARGS... — one window, one picture. PROGRAM runs in ~ada/code
# through the relay, which types KEYS once the first frame is up ("" for none).
shot() {
  png=$1 theme=$2 side=$3 cols=$4 rows=$5 keys=$6
  shift 7
  program=$1
  shift
  title=$(basename "$program")
  mkdir -p "$tmp/bin"
  cp "$tmp/relay" "$tmp/bin/$title"
  # Light or dark is the person's setting, kept in the theme store's working file beside the preset it follows.
  printf '{\n  "preset": "%s",\n  "follows_origin": true,\n  "mode": "%s"\n}\n' "$theme" "$side" > "$cfg/rolltui/theme.working.json"
  cp "$cfg/rolltui/theme.working.json" "$tmp/presets/theme.working.json"
  # The window's margin is the theme's ground, read from a headless frame in the same theme. Mono paints no
  # ground of its own: its margin is Basic's white.
  ground=$(cd "$code" && ROLL_CONFIG_DIR="$cfg" "$selftest" --presets "$tmp/presets" --frame-sgr "${cols}x${rows}" \
      hardware/sensors/weatherbox/README.md 2>/dev/null |
    python3 -c 'import re,sys,collections
frame=sys.stdin.read()
if "\x1b[" not in frame: sys.exit("readme_shots: the headless frame came back empty")
c=collections.Counter(re.findall(r"48;2;(\d+);(\d+);(\d+)m",frame))
print(",".join(str(int(v)*257) for v in c.most_common(1)[0][0]) if c else "65535,65535,65535")')
  args=""
  for arg in "$@"; do args="$args '$arg'"; done
  cat > "$tmp/launch.sh" <<EOF
#!/bin/sh
cd "$code" || exit 1
printf '\033]7;file://%s%s\007' "\$(hostname)" "\$PWD"
printf '\033[H\033[2J\033[3J'
echo \$\$ > "$tmp/pid"
cd "$tmp/bin" || exit 1
ROLL_CONFIG_DIR="$cfg" RELAY_DONE="$tmp/done" RELAY_KEYS="$keys" RELAY_EXEC="$program" RELAY_DIR="$code" \
  exec "./$title"$args
EOF
  chmod +x "$tmp/launch.sh"
  rm -f "$tmp/pid" "$tmp/done"
  window=$(osascript -e "tell application \"Terminal\"
    set t to do script \"\"
    set w to (first window whose selected tab is t)
    set current settings of t to settings set \"Basic\"
    set background color of t to {$ground}
    set custom title of t to \"$title\"
    set title displays custom title of t to true
    set title displays window size of t to false
    set number of columns of w to $cols
    set number of rows of w to $rows
    do script \"exec '$tmp/launch.sh'\" in t
    return id of w
  end tell")
  for _ in $(seq 1 120); do [ -e "$tmp/done" ] && break; sleep 0.5; done
  [ -e "$tmp/done" ] || { echo "readme_shots: $title did not reach its picture for $png" >&2; exit 1; }
  screencapture -l "$window" -o -x "$png"
  trim "$png"
  kill "$(cat "$tmp/pid")" 2>/dev/null || true
  osascript -e "tell application \"Terminal\" to close (every window whose id is $window) saving no" >/dev/null
  window=""
  echo "$png"
}

chat="$root/build/rolltui-chat"
studio="$root/build/rolltui-studio"
readme=hardware/sensors/weatherbox/README.md
six="Down Down Down Down Down Down"

# The hero: the default theme, both sides; the README shows whichever matches the reader's own.
shot "$out/dirktui-dark.png"  default dark  150 36 "" -- "$bin" "$readme"
shot "$out/dirktui-light.png" default light 150 36 "" -- "$bin" "$readme"

# The gallery: six themes, six parts of the library. Mono has no colours of its own, so its side is the
# terminal's: Basic's black on white.
shot "$out/chat.png"     catppuccin dark  100 30 \
  "Type:how_would_I_add_two_numbers_in_C? Enter Wait:4000 Type:show_me_the_code Enter Wait:4000 Type:thanks" -- "$chat"
shot "$out/settings.png" solarized  light 100 30 "F2 $six Enter" -- "$bin" "$readme"
shot "$out/help.png"     ember      dark  100 30 "F1" -- "$bin" "$readme"
shot "$out/studio.png"   orchard    dark  100 30 "F4 Enter" -- "$studio" "$readme"
shot "$out/code.png"     harbour    light 100 30 "" -- "$bin" hardware/sensors/air-quality/upload.py
shot "$out/palette.png"  mono       light 100 30 \
  "Type:show_me_the_code Enter Wait:4000 CtrlP Type:theme" -- "$chat"
# The gallery is shown at half the README's width.
for f in chat settings help studio code palette; do sips -Z 1100 "$out/$f.png" >/dev/null; done
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
