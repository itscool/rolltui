### The default layout (three base-layer windows; the adapter fills their slots)
```
┌ transcript ────────────────────────────────┬ status ─────────────┐
│ > user prompt                              │ local  qwen3-next…  │
│                                            │ cloud  fable-5      │
│ ## heading                                 │ turns  17 (100%)    │
│ wrapped assistant text …                   │ tokens 30174/262    │
│ ┌ cpp ─────────────────────┐               │ spend  $0.0000      │
│ │ code                     │               │ saved  $0.2419      │
│ └──────────────────────────┘               │ tools  read_file=3  │
│ [tool] read_file README.md  (folded)       │ next   ~8,300 tok   │
│                                  ▼ 3 new   │        ~$0.0249     │
├────────────────────────────────────────────┴─────────────────────┤
│ > typing here, multi-line, cursor, history                      │
└──────────────────────────────────────────────────────────────────┘
```
- Side panel: fixed width (≈28-36 cells, theme/config), the same five facts the bottom
  pane shows today re-laid as label/value rows, plus the pre-flight estimate. Below a
  minimum terminal width the panel folds to a one-line strip above the input (the
  current behaviour, as the degrade rung). The DECSTBM `StatusPane` is retired once
  this lands; it was only ever TTY-side. **Its deviation note ("a side column must
  reflow every line of streamed output") was true of a write-through REPL and is
  dissolved, not overruled, by a frontend that owns the wrap.**
- Input window: the bottom rows, growing with the buffer up to a cap; multi-line via
  Alt+Enter; cursor movement, Home/End, history up/down (`LineEditor` grows into this,
  still pure, still unit-tested, and moves into the library); bracketed paste inserts
  literally. **Typing never touches the transcript's scroll offset** (requirement 7).
- Approval prompts (`GatedToolExecutor`'s approver) render the preview **in the
  transcript**, wrapped and scrollable, and the `[y/N]` in the input window as a
  modal — the human can scroll the diff while the question is open. This directly
  serves the 2026-09-01 lesson that an approver shown a truncated preview approves
  something that was not true.
- Keys: PgUp/PgDn/Home/End scroll; Ctrl-C cancels a turn, twice exits; Ctrl-D on
  empty input exits; Ctrl-L forces a full repaint (the escape hatch for a terminal that
  lost sync). **Enter is always submit** while the input window has focus; no widget
  may claim it. Mouse: see the next section.
- Terminal restore on every exit path including signals, extending `LineReader`'s
  existing guarantee to the alternate screen and mouse mode — a process that dies with
  the alternate screen on leaves the user staring at nothing.

