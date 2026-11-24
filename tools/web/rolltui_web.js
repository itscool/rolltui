// rolltui_web.js — a rolltui program built for wasm32-wasi (tools/web/build.sh), run in a page on an xterm.js terminal.
//
// The program's descriptor 1 is the terminal: what it writes goes to xterm.js. What xterm.js sends (keys, the mouse, a paste, its
// replies) is handed in with `rolltui_web_input`, and a new size with `rolltui_web_resize`. A page may never block, so the program
// is stepped (`tour_step`): when input arrives, and when the step before asked to be woken.
//
//   const run = await rolltuiWeb.start(term, "rolltui-tour.wasm");   // term: an opened xterm.js Terminal
//   run.resize(cols, rows);                                          // after the page changes the terminal's size
"use strict";

(function (global) {
  const ERRNO = { SUCCESS: 0, BADF: 8, NOSYS: 52, SPIPE: 70 };

  // What a page has of WASI: the terminal on descriptors 0 to 2, a clock, random bytes and the environment; no files.
  function wasi(memory, term, env) {
    const enc = new TextEncoder();
    const view = () => new DataView(memory().buffer);
    const bytes = () => new Uint8Array(memory().buffer);
    const envs = Object.entries(env).map(([k, v]) => enc.encode(`${k}=${v}\0`));
    const t0 = performance.now();
    return {
      environ_sizes_get(count, size) {
        view().setUint32(count, envs.length, true);
        view().setUint32(size, envs.reduce((n, e) => n + e.length, 0), true);
        return ERRNO.SUCCESS;
      },
      environ_get(ptrs, buf) {
        for (const e of envs) {
          view().setUint32(ptrs, buf, true);
          bytes().set(e, buf);
          ptrs += 4;
          buf += e.length;
        }
        return ERRNO.SUCCESS;
      },
      args_sizes_get(count, size) {
        view().setUint32(count, 0, true);
        view().setUint32(size, 0, true);
        return ERRNO.SUCCESS;
      },
      args_get() { return ERRNO.SUCCESS; },
      clock_time_get(id, precision, out) {
        const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round((performance.now() - t0) * 1e6));
        view().setBigUint64(out, ns, true);
        return ERRNO.SUCCESS;
      },
      clock_res_get(id, out) {
        view().setBigUint64(out, 1000n, true);
        return ERRNO.SUCCESS;
      },
      fd_write(fd, iovs, n, written) {
        let total = 0;
        for (let i = 0; i < n; ++i) {
          const p = view().getUint32(iovs + 8 * i, true), len = view().getUint32(iovs + 8 * i + 4, true);
          const chunk = bytes().slice(p, p + len);
          if (fd === 1) term.write(chunk);
          else if (fd === 2) console.warn(new TextDecoder().decode(chunk));
          total += len;
        }
        view().setUint32(written, total, true);
        return fd <= 2 ? ERRNO.SUCCESS : ERRNO.BADF;
      },
      fd_read(fd, iovs, n, read) {
        view().setUint32(read, 0, true);
        return ERRNO.SUCCESS;
      },
      fd_close() { return ERRNO.SUCCESS; },
      fd_seek() { return ERRNO.SPIPE; },
      fd_fdstat_get(fd, out) {
        if (fd > 2) return ERRNO.BADF;
        bytes().fill(0, out, out + 24);
        view().setUint8(out, 2); // a character device
        return ERRNO.SUCCESS;
      },
      fd_prestat_get() { return ERRNO.BADF; }, // no directory is opened to a page
      random_get(buf, len) {
        crypto.getRandomValues(bytes().subarray(buf, buf + len));
        return ERRNO.SUCCESS;
      },
      sched_yield() { return ERRNO.SUCCESS; },
      proc_exit(code) { throw new Error(`the program exited (${code})`); },
    };
  }

  // TOUCH AS THE POINTER: a phone has no mouse, and xterm.js reports only a mouse, so a touch is made the report a mouse would
  // send (SGR, 1006, which is what a rolltui program asks for), straight to the program, while it asks for the mouse at all:
  //   a tap         a left press and its release in that cell (two quick taps are a double-click, as two clicks are)
  //   a swipe       the wheel, a notch for each cell the finger travels: up and down, and sideways for a sideways swipe;
  //                 the content follows the finger, as a page does under a touch
  // `cell(x, y)` gives the 1-based cell under a point and the size of a cell in pixels; `send(text)` hands the report in.
  // Pure, so it is tested without a page (tests/web_touch.js).
  function touchPointer(cell, send) {
    const SLOP = 8; // pixels a finger may wander and still be a tap
    let down = null;
    const report = (code, c, final) => send(`\x1b[<${code};${c.col};${c.row}${final}`);
    return {
      start(x, y) {
        down = { x0: x, y0: y, ax: x, ay: y, moved: false, at: cell(x, y) };
      },
      move(x, y) {
        if (!down) return;
        if (!down.moved && Math.hypot(x - down.x0, y - down.y0) < SLOP) return;
        down.moved = true;
        const c = cell(x, y);
        while (Math.abs(y - down.ay) >= c.h) { // the finger up: what is below comes into view, as the wheel down shows it
          const up = y < down.ay;
          report(up ? 65 : 64, c, "M");
          down.ay += up ? -c.h : c.h;
        }
        while (Math.abs(x - down.ax) >= c.w) {
          const left = x < down.ax;
          report(left ? 67 : 66, c, "M");
          down.ax += left ? -c.w : c.w;
        }
      },
      end() {
        if (down && !down.moved) {
          report(0, down.at, "M");
          report(0, down.at, "m");
        }
        down = null;
      },
      cancel() { down = null; },
    };
  }

  // The touches on the terminal, while the program asks for the mouse: the page neither scrolls nor makes mouse events of its own
  // under them. Two fingers are the page's (a pinch zooms it).
  function wireTouch(term, input) {
    const el = term.element;
    if (!el) return;
    const tracking = () => !term.modes || term.modes.mouseTrackingMode !== "none";
    const cell = (x, y) => {
      const screen = el.querySelector(".xterm-screen") || el;
      const r = screen.getBoundingClientRect();
      const w = r.width / Math.max(term.cols, 1), h = r.height / Math.max(term.rows, 1);
      return {
        col: Math.min(Math.max(Math.floor((x - r.left) / w) + 1, 1), term.cols),
        row: Math.min(Math.max(Math.floor((y - r.top) / h) + 1, 1), term.rows),
        w, h,
      };
    };
    const touch = touchPointer(cell, input);
    const one = (e) => e.touches.length === 1 || (e.type === "touchend" && e.touches.length === 0);
    el.addEventListener("touchstart", (e) => {
      if (!tracking() || e.touches.length !== 1) return touch.cancel();
      touch.start(e.touches[0].clientX, e.touches[0].clientY);
      e.preventDefault();
    }, { passive: false });
    el.addEventListener("touchmove", (e) => {
      if (!tracking() || !one(e)) return touch.cancel();
      touch.move(e.touches[0].clientX, e.touches[0].clientY);
      e.preventDefault();
    }, { passive: false });
    el.addEventListener("touchend", (e) => {
      if (!tracking()) return touch.cancel();
      touch.end();
      e.preventDefault();
    }, { passive: false });
    el.addEventListener("touchcancel", () => touch.cancel());
  }

  async function start(term, url, options = {}) {
    const response = await fetch(url);
    const module = await WebAssembly.compile(await response.arrayBuffer());
    let instance;
    const memory = () => instance.exports.memory;
    const dark = !(global.matchMedia && global.matchMedia("(prefers-color-scheme: light)").matches);
    const env = Object.assign({ TERM: "xterm-256color", COLORTERM: "truecolor", COLORFGBG: dark ? "15;0" : "0;15" }, options.env || {});
    const provided = wasi(memory, term, env);
    const imports = {};
    for (const imp of WebAssembly.Module.imports(module)) { // anything else a page has not: "not here"
      if (imp.kind !== "function") continue;
      imports[imp.module] = imports[imp.module] || {};
      imports[imp.module][imp.name] = imp.module === "wasi_snapshot_preview1" && provided[imp.name] ? provided[imp.name] : () => ERRNO.NOSYS;
    }
    instance = await WebAssembly.instantiate(module, imports);
    const ex = instance.exports;
    if (ex._initialize) ex._initialize();

    const input = (data) => {
      const b = typeof data === "string" ? new TextEncoder().encode(data) : data;
      if (!b.length) return;
      const p = ex.rolltui_web_input_area(b.length);
      new Uint8Array(memory().buffer, p, b.length).set(b);
      ex.rolltui_web_input(b.length);
      kick();
    };
    let timer = 0, queued = false, stopped = false;
    const step = () => {
      queued = false;
      if (stopped) return;
      clearTimeout(timer);
      const ms = ex.tour_step();
      if (ms === -2) {
        stopped = true;
        if (options.onStop) options.onStop();
        return;
      }
      if (ms >= 0) timer = setTimeout(step, Math.max(ms, 1));
    };
    const kick = () => {
      if (queued || stopped) return;
      queued = true;
      queueMicrotask(step);
    };

    // The terminal's size in pixels too, as TIOCGWINSZ gives it: a picture is fitted to the cells' real size.
    const pixels = () => {
      const screen = term.element && term.element.querySelector(".xterm-screen");
      return screen ? [Math.round(screen.clientWidth), Math.round(screen.clientHeight)] : [0, 0];
    };
    ex.rolltui_web_resize(term.cols, term.rows, ...pixels());
    // WHAT XTERM.JS WOULD ANSWER, said before it is asked: a page cannot wait while the program enters, so the reply to the one
    // question entering asks (DA1) is already there.
    input("\x1b[?1;2c");
    if (!ex.tour_start()) throw new Error("the program did not start");
    term.onData(input);
    term.onBinary((s) => input(Uint8Array.from(s, (c) => c.charCodeAt(0))));
    wireTouch(term, input);
    kick();
    return {
      resize(cols, rows) {
        ex.rolltui_web_resize(cols, rows, ...pixels());
        kick();
      },
      get stopped() { return stopped; },
    };
  }

  global.rolltuiWeb = { start, touchPointer };
})(typeof window !== "undefined" ? window : globalThis);
