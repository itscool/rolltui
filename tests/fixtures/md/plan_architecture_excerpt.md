### Architecture (the load-bearing part; requirements 1 and 3 fall out of it)

```
core ──owns──▶ SessionView (append-only entries + RunLog snapshot + input state)
                    │ snapshot()                          ▲ apply(Command)
                    ▼                                     │
            IFrontend (pure: render(view, size, theme) → Frame)   ◀── key/mouse events
              ├── PlainFrontend   (stdout byte stream; NOT a TTY → this, always)
              ├── TuiFrontend     (adapter: SessionView → rolltui::Document → tui/ library)
              └── (future) GUI / web — same SessionView, no core change
```

- **`SessionView` is core state, like `RunLog`.** Entries: user prompt, assistant text
  (streaming or final, with the raw markdown source kept), tool call, tool result,
  escalation notice (with its reason), warning (degeneration, truncation), approval
  request + decision, system note. Every `fprintf(stdout, ...)` in `main.cpp` that a
  user is meant to read becomes an entry; the plain frontend renders entries as the
  exact bytes it prints today. **Telemetry stays where it is** — the side panel reads the
  same `RunLog` snapshot `//status` and the exit dump read (one substrate, now four
  consumers).
- **A frontend is a pure function.** `Frame render(const SessionView&, Size, const Theme&)`.
  No frontend keeps a previous frame as *input*; it keeps one only to diff the bytes it
  emits. Scrollback-from-history and resize-correctness are therefore not features to
  test separately: they are what "pure function of state" *means*, and the test is
  `render(v, W1) == render(v, W1)` after an intervening `render(v, W2)`.
- **Performance is a memo, not a shortcut.** Re-wrapping a 10k-line transcript per frame
  is too slow; the fix is a cache keyed on `(entry id, entry version, width, theme
  version)` for finished entries, with only the streaming entry re-laid-out each frame.
  The cache is invalidated by its key, never by an event, so the output stays a function
  of state. Frame time is instrumented from day one (a frame over ~16 ms on a long
  history is the regression signal — CLAUDE.md: instrument as you write).
- **Scroll position is `(entry index, line within entry)`, not a line number.** That is
  what keeps a resize looking at the same message. "Follow" is on when the viewport is
  at the bottom; streaming output and typing never change the offset while it is off; a
  "▼ N new lines" marker on the transcript's bottom edge says what you are missing; End
  or PgDn-to-bottom re-engages follow.
- **The TUI is TTY-gated exactly as `StatusPane` is today, and additionally explicit:**
  `roll --frontend=plain|tui` selects; with no flag, a TTY gets `tui`, anything else gets
  `plain`. The flag exists so the probation harness can run under a pty and still get
  plain bytes — an explicit switch, not an inference (Explicit over implicit).
- **Model output is data, never terminal input.** ANSI/OSC escapes in a model's text are
  stripped before they reach any frontend. A model cannot set the title, move the
  cursor or write to the clipboard; if syntax colouring or hyperlinks are ever wanted,
  the *renderer* emits them from a parsed block, never by passing bytes through.

