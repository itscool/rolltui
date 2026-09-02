<!-- user -->
read the manager header and tell me what stop_heartbeat does
<!-- tool: read_file include/ResilientModelManager.hpp (lines 40-52) -->
  void stop_heartbeat() {
    if (!hb_source_) return;
    dispatch_source_cancel(hb_source_);
    // Drain: a handler that is already running must finish before we return,
    // or a poll() blocked on the state lock can resume after release() and
    // respawn the server that was just killed.
    dispatch_sync(hb_queue_, ^{});
    dispatch_release(hb_source_);
    hb_source_ = nullptr;
  }
<!-- assistant -->
`stop_heartbeat` cancels the heartbeat timer **and drains its queue**: the
`dispatch_sync` after the cancel is what makes shutdown race-free, because
`dispatch_source_cancel` does not wait for a handler that is already running.

Without the drain a `poll()` blocked on the state lock could resume *after* `release()`
decided it was the last client, re-register itself and respawn the server release
just killed — a permanent orphan. The rule is recorded in [CLAUDE.md](https://example.com/roll/CLAUDE.md#hard-constraints)
under hard constraints.

1. cancel the timer
2. drain the queue
3. release the source
<!-- tool: run_tests build_only (134 ms) -->
[roll] build: 0 errors, 0 warnings
[roll] tests skipped (build_only)
<!-- user -->
thanks
