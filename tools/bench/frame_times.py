#!/usr/bin/env python3
# frame_times.py — A REAL PROGRAM'S FRAMES, TIMED: dirktui on a pseudo-terminal at a size, the cursor walked down a folder, each
# frame's draw and present times read from the library's own log (ROLLTUI_LOG_LEVEL=debug), and their medians printed.
#
#   tools/bench/frame_times.py [BUILD_DIR] [FOLDER] [COLS] [ROWS] [STEPS]
#
# Run against a COPY of examples/presets in a scratch config home, as any headless dirktui run is: the stores write working files
# beside their presets. Everything it makes is removed when it ends.
import os
import pty
import select
import shutil
import struct
import sys
import tempfile
import time

try:
    import fcntl
    import termios
except ImportError:
    sys.exit("frame_times.py: needs a POSIX pseudo-terminal")

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "build")
folder = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "c")
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 200
rows = int(sys.argv[4]) if len(sys.argv) > 4 else 50
steps = int(sys.argv[5]) if len(sys.argv) > 5 else 60

work = tempfile.mkdtemp(prefix="rolltui-frames.")
try:
    shutil.copytree(os.path.join(root, "examples", "presets"), os.path.join(work, "presets"))
    log = os.path.join(work, "frames.log")
    env = dict(os.environ, ROLL_CONFIG_DIR=os.path.join(work, "cfg"), ROLLTUI_LOG=log, ROLLTUI_LOG_LEVEL="debug",
               TERM="xterm-256color", COLORTERM="truecolor")
    pid, fd = pty.fork()
    if pid == 0:
        os.execve(os.path.join(build, "dirktui"), ["dirktui", folder], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def drain(seconds):
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.02)
            if ready:
                try:
                    os.read(fd, 65536)
                except OSError:
                    return

    drain(1.5)
    for _ in range(steps):
        os.write(fd, b"\x1b[B")  # Down
        drain(0.05)
    drain(0.5)
    os.write(fd, b"q")
    drain(0.5)
    try:
        os.kill(pid, 9)
    except OSError:
        pass

    render, present, sent = [], [], []
    with open(log) as f:
        for line in f:
            if "frame: render" not in line:
                continue
            w = line.split()
            render.append(float(w[w.index("render") + 1]))
            present.append(float(w[w.index("present") + 1]))
            sent.append(int(w[w.index("bytes") - 1]))

    def median(v):
        v = sorted(v)
        return v[len(v) // 2] if v else 0

    def p90(v):
        v = sorted(v)
        return v[int(len(v) * 0.9)] if v else 0

    print(f"dirktui {cols}x{rows}, {len(render)} frames: render median {median(render):.2f} ms (p90 {p90(render):.2f}), "
          f"present median {median(present):.2f} ms (p90 {p90(present):.2f}), {median(sent)} bytes a frame")
finally:
    shutil.rmtree(work, ignore_errors=True)
