#pragma once
//
// rolltui/Scratch.hpp — CALLEE-OWNED SCRATCH, LENT FOR A BOUNDED WINDOW (Phase 13 m5b).
//
// The third answer to "a per-frame API must not return an owning container by value"
// (CLAUDE.md). The first two are *fill a buffer the caller owns* and *return a borrow*;
// this is the one that keeps the storage where it belongs — with the callee, which knows
// how big it needs to be and reuses it forever — and lends it out for a stated window.
//
// It is the shape every graphics and audio API converged on: `Map`/`Unmap`,
// `LockTexture`/`UnlockTexture`, `GetBuffer`/`ReleaseBuffer`, the Python buffer protocol.
// The reason to name it that way rather than leave it as "a thread_local I promise not to
// nest" is the second half:
//
//   **THE WINDOW IS ENFORCED, NOT PROMISED.**
//   - A second lock while one is outstanding ABORTS, naming the buffer. Phase 13 m3 and m5
//     introduced reused buffers whose safety rested on a COMMENT saying "not nested" —
//     a claim nobody could check, on exactly the kind of aliasing that produces a
//     plausible wrong frame rather than a crash. This checks it, in every build, because a
//     check that only runs under NDEBUG is a check that never runs in `ctest`.
//   - On release the storage is CLEARED (capacity kept), so a caller that held the
//     reference past its window reads an obviously empty buffer instead of last frame's
//     plausible contents. Deterministic garbage beats stale truth.
//
// **Page protection is deliberately NOT here, and it is worth saying why**, because it is
// the natural next thought: `mprotect`ing the region read-only between windows would turn
// a stale WRITE into a SIGSEGV at the offending instruction, which is stronger than
// anything above. It needs page-aligned, page-sized allocations — so a 200-byte scratch
// would cost 4 KB, and this phase exists to reduce bytes. The clear-on-release gets most
// of the signal for nothing. A single large buffer that wants the stronger guarantee can
// have it later without changing this interface.
//
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace rolltui {

namespace detail {
// Phase 14 m6a: registers one per-thread buffer with `release_thread()`. Declared here rather
// than included from Lifetime.hpp so this header stays what it has always been — a template
// and nothing else.
void on_thread_release(void (*fn)(void*), void* target);
}  // namespace detail

// One reusable T, lent through `lock()`. Declare one per buffer, `static thread_local` at
// the function that owns it, and name it — the name is what an abort prints.
template <typename T>
class Scratch {
 public:
  explicit Scratch(const char* name) : name_(name) {}
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;

  // The borrow. Holds the window open for its lifetime; move-only, so the window cannot
  // be accidentally duplicated.
  class Lock {
   public:
    explicit Lock(Scratch& s) : s_(&s) { s_->acquire(); }
    ~Lock() {
      if (s_) s_->release();
    }
    Lock(Lock&& o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    Lock& operator=(Lock&& o) noexcept {
      if (this != &o) {
        if (s_) s_->release();
        s_ = o.s_;
        o.s_ = nullptr;
      }
      return *this;
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

    T& operator*() const { return s_->value_; }
    T* operator->() const { return &s_->value_; }
    T& get() const { return s_->value_; }

   private:
    Scratch* s_ = nullptr;
  };

  Lock lock() { return Lock(*this); }

  // RELEASES THE STORAGE, not just its contents — `clear()` keeps the capacity, which is the
  // whole point of this class, and this is the one place that gives it back. Deliberately NOT
  // called `release()`: that is the private half of the lock/unlock pair below, and one of
  // this project's standing rules is that two readings of a name is a place to be wrong.
  // Called by
  // `rolltui::release_thread()`; a buffer that is released re-registers itself the next time
  // it is locked, so the lend keeps working afterwards.
  void release_storage() {
    T empty;
    value_ = std::move(empty);
    registered_ = false;
  }

 private:
  friend class Lock;
  void acquire() {
    if (held_) {
      // Not an exception: this is a programming error in the LIBRARY, the window is
      // already corrupt by the time anyone could catch it, and a stack trace at the
      // second lock names both callers.
      std::fprintf(stderr, "rolltui: scratch '%s' locked twice — a reused buffer cannot be nested\n", name_);
      std::abort();
    }
    held_ = true;
    // Registered on first use rather than at construction: these are `static thread_local`, so
    // construction happens on the thread that first draws, and registering there would be the
    // same moment — but first-use also covers a buffer whose thread was created before this
    // mechanism existed, and it costs one branch on a path that is already doing work.
    if (!registered_) {
      registered_ = true;
      detail::on_thread_release([](void* p) { static_cast<Scratch*>(p)->release_storage(); }, this);
    }
    value_.clear();
  }
  void release() {
    held_ = false;
    value_.clear();  // capacity kept; a stale reader sees empty, not last frame's contents
  }

  T value_;
  const char* name_ = "";
  bool held_ = false;
  bool registered_ = false;
};

}  // namespace rolltui
