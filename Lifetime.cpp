// rolltui/Lifetime.cpp — see Lifetime.hpp.
#include "rolltui/Lifetime.hpp"

#include <vector>

namespace rolltui {

namespace {

// The process-wide releasers. A plain function pointer and not a `std::function`: a releaser
// never needs to capture — it names a static the module already has — and a function pointer
// keeps this registry free of the allocation-per-registration a captured lambda would cost.
std::vector<void (*)()>& process_releasers() {
  static std::vector<void (*)()> v;
  return v;
}

// The calling thread's releasers, registered by `Scratch` (rolltui/Scratch.hpp) when it is
// first locked. Per-thread rather than process-wide because a `thread_local` in one thread
// cannot be freed from another, and pretending otherwise would be the kind of ambiguity this
// project spends its time removing.
std::vector<void (*)(void*)>& thread_releasers() {
  static thread_local std::vector<void (*)(void*)> v;
  return v;
}
std::vector<void*>& thread_release_targets() {
  static thread_local std::vector<void*> v;
  return v;
}

}  // namespace

void on_shutdown(void (*fn)()) { process_releasers().push_back(fn); }

namespace detail {

// Registers one per-thread buffer. Called by `Scratch`'s first lock; the pair is kept as
// (function, object) rather than a bound callable so that registration allocates at most a
// vector growth and never a closure.
void on_thread_release(void (*fn)(void*), void* target) {
  thread_releasers().push_back(fn);
  thread_release_targets().push_back(target);
}

}  // namespace detail

void release_thread() {
  std::vector<void (*)(void*)>& fns = thread_releasers();
  std::vector<void*>& targets = thread_release_targets();
  // Reverse order, and by INDEX: a releaser may not register anything new, but reading the
  // size each time round costs nothing and makes that a fact rather than an assumption.
  for (std::size_t i = fns.size(); i > 0; --i) fns[i - 1](targets[i - 1]);
  // The registry itself is storage this thread holds, so it goes too — and the buffers
  // re-register themselves on next use, which is what makes `release_thread()` safe to call
  // in the middle of a session rather than only at the end.
  fns.clear();
  fns.shrink_to_fit();
  targets.clear();
  targets.shrink_to_fit();
}

void shutdown() {
  std::vector<void (*)()>& v = process_releasers();
  for (std::size_t i = v.size(); i > 0; --i) v[i - 1]();
  v.clear();
  v.shrink_to_fit();
  release_thread();
}

}  // namespace rolltui
