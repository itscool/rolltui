// web_touch.js — TOUCH AS THE POINTER on the web page (tools/web/rolltui_web.js): a tap is a left press and release in its cell, a
// swipe turns the wheel a notch a cell, up and down and sideways. The page's pure part, run by JavaScriptCore's shell:
//   jsc tests/web_touch.js -- tools/web/rolltui_web.js
"use strict";
load(arguments[0]);
const out = print;
let failed = 0, checks = 0;
function check(ok, what) {
  ++checks;
  if (!ok) ++failed;
  out(`  [${ok ? "PASS" : "FAIL"}] ${what}`);
}
const ESC = "\x1b";
const cell = (x, y) => ({ col: Math.floor(x / 10) + 1, row: Math.floor(y / 20) + 1, w: 10, h: 20 });
let sent = [];
const t = globalThis.rolltuiWeb.touchPointer(cell, (s) => sent.push(s));
const shown = () => JSON.stringify(sent.map((s) => s.replace(ESC, "ESC")));

t.start(35, 45); t.end();
check(sent.length === 2 && sent[0] === ESC + "[<0;4;3M" && sent[1] === ESC + "[<0;4;3m", "a tap is a left press and its release, in its cell " + shown());
sent = [];
t.start(35, 45); t.move(39, 49); t.end();
check(sent.length === 2 && sent[0] === ESC + "[<0;4;3M", "a finger that wanders a few pixels is still a tap " + shown());
sent = [];
t.start(50, 200); t.move(50, 150);
check(sent.length === 2 && sent.every((s) => s === ESC + "[<65;6;8M"), "a swipe up two cells is the wheel down twice: what is below comes into view " + shown());
t.end();
check(sent.length === 2, "…and lifting the finger after a swipe is no tap");
sent = [];
t.start(50, 100); t.move(50, 125);
check(sent.length === 1 && sent[0] === ESC + "[<64;6;7M", "a swipe down a cell is the wheel up " + shown());
t.end();
sent = [];
t.start(100, 100); t.move(70, 100); t.end();
check(sent.length === 3 && sent.every((s) => s === ESC + "[<67;8;6M"), "a swipe left three cells is the wheel's sideways turn right, three times " + shown());
sent = [];
t.start(100, 100); t.cancel(); t.end();
check(sent.length === 0, "a touch cancelled (a second finger, the program not tracking) sends nothing");
sent = [];
t.end();
check(sent.length === 0, "a touch's end with no start sends nothing");
out(`web_touch: ${failed ? "FAILURES" : "ALL PASS"} (${checks} checks)`);
if (failed) throw new Error(`${failed} failed`);
