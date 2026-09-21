#ifndef ROLLTUI_STR_HPP
#define ROLLTUI_STR_HPP
/*
 * rolltui/str.hpp — the working set a C++ host reaches for beside `RolltuiStr`: a borrowed VIEW of text, and the few operations
 * that build and take apart strings. Header-only, over `p` and `n`: no C function is added, nothing allocates but `RolltuiStr`'s
 * own buffer, and nothing here names a `std::` container or view (the rule `rolltui.h` states for `RolltuiStr`), so a host that
 * uses it holds one kind of string, in one allocator, from the library's calls to its own logic.
 *
 *   StrView  a pointer and a length, BORROWED, never promised NUL-terminated. Cheap to pass by value. Made from a `RolltuiStr`,
 *            a literal or a `const char*`, so a function that only reads text takes one and accepts all three.
 *   `+`      builds one `RolltuiStr` and appends into it as it goes, so `dir + "/" + name + ".json"` is one buffer, not four.
 *   appendf  printf into a `RolltuiStr`, for numbers and padding.
 *   path_*   the last component of a path and everything before it, as views.
 *
 * A VIEW DOES NOT KEEP ITS TEXT ALIVE. `StrView v = a + b;` is a view of a temporary that is gone by the next line; hold the
 * `RolltuiStr`, take the view from that.
 */
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "rolltui/rolltui.h"

namespace rolltui {

class StrView {
 public:
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  constexpr StrView() = default;
  constexpr StrView(const char* s, std::size_t len) : p_(s), n_(len) {}
  StrView(const char* s) : p_(s), n_(s ? std::strlen(s) : 0) {}  // NOLINT(google-explicit-constructor)
  StrView(const RolltuiStr& s) : p_(s.p), n_(s.n) {}             // NOLINT(google-explicit-constructor)

  // NULL is the empty string everywhere in this library, so `data()` never returns NULL.
  const char* data() const { return p_ ? p_ : ""; }
  std::size_t size() const { return n_; }
  bool empty() const { return n_ == 0; }
  const char* begin() const { return data(); }
  const char* end() const { return data() + n_; }
  char operator[](std::size_t i) const { return p_[i]; }
  char front() const { return p_[0]; }
  char back() const { return p_[n_ - 1]; }

  // Clamped, never out of range: a `pos` past the end is the empty view at the end.
  StrView substr(std::size_t pos, std::size_t len = npos) const {
    if (pos > n_) pos = n_;
    if (len > n_ - pos) len = n_ - pos;
    return StrView(p_ + pos, len);
  }
  StrView first(std::size_t k) const { return substr(0, k); }
  StrView last(std::size_t k) const { return k >= n_ ? *this : StrView(p_ + n_ - k, k); }
  StrView drop_front(std::size_t k) const { return substr(k); }
  StrView drop_back(std::size_t k) const { return k >= n_ ? StrView() : StrView(p_, n_ - k); }

  std::size_t find(char c, std::size_t from = 0) const {
    for (std::size_t i = from; i < n_; ++i)
      if (p_[i] == c) return i;
    return npos;
  }
  std::size_t find(StrView s, std::size_t from = 0) const {
    if (from > n_) return npos;
    if (s.n_ == 0) return from;
    for (std::size_t i = from; i + s.n_ <= n_; ++i)
      if (std::memcmp(p_ + i, s.p_, s.n_) == 0) return i;
    return npos;
  }
  // The LAST match that starts at or before `from`.
  std::size_t rfind(char c, std::size_t from = npos) const {
    if (n_ == 0) return npos;
    for (std::size_t i = from < n_ ? from + 1 : n_; i > 0; --i)
      if (p_[i - 1] == c) return i - 1;
    return npos;
  }
  std::size_t rfind(StrView s, std::size_t from = npos) const {
    if (s.n_ > n_) return npos;
    std::size_t i = n_ - s.n_;
    if (from < i) i = from;
    for (;; --i) {
      if (s.n_ == 0 || std::memcmp(p_ + i, s.p_, s.n_) == 0) return i;
      if (i == 0) return npos;
    }
  }
  bool contains(StrView s) const { return find(s) != npos; }
  bool contains(char c) const { return find(c) != npos; }
  bool starts_with(StrView s) const { return s.n_ <= n_ && (s.n_ == 0 || std::memcmp(p_, s.p_, s.n_) == 0); }
  bool ends_with(StrView s) const { return s.n_ <= n_ && (s.n_ == 0 || std::memcmp(p_ + n_ - s.n_, s.p_, s.n_) == 0); }

  // Bytewise, like `strcmp`: negative, zero, positive.
  int compare(StrView o) const {
    const std::size_t m = n_ < o.n_ ? n_ : o.n_;
    const int c = m ? std::memcmp(p_, o.p_, m) : 0;
    if (c != 0) return c;
    return n_ < o.n_ ? -1 : n_ > o.n_ ? 1 : 0;
  }

 private:
  const char* p_ = nullptr;
  std::size_t n_ = 0;
};

inline bool operator==(StrView a, StrView b) { return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0); }
inline bool operator!=(StrView a, StrView b) { return !(a == b); }
inline bool operator<(StrView a, StrView b) { return a.compare(b) < 0; }

// A copy of a view, owned.
inline RolltuiStr own(StrView v) {
  RolltuiStr s;
  s.assign(v.data(), v.size());
  return s;
}

inline void append(RolltuiStr& s, StrView v) { s.append(v.data(), v.size()); }
inline RolltuiStr& operator+=(RolltuiStr& s, StrView v) {
  append(s, v);
  return s;
}

// `a + b` makes one string; `(a + b) + c` appends `c` into that same buffer, so a chain is one buffer that grows, never a copy
// per `+`. A left side that is already a `RolltuiStr` temporary is USED, not copied.
inline RolltuiStr operator+(StrView a, StrView b) {
  RolltuiStr s;
  s.append(a.data(), a.size());
  s.append(b.data(), b.size());
  return s;
}
template <class S, class = std::enable_if_t<std::is_same_v<S, RolltuiStr>>>
inline RolltuiStr operator+(S&& a, StrView b) {
  RolltuiStr s(static_cast<RolltuiStr&&>(a));
  s.append(b.data(), b.size());
  return s;
}

#if defined(__GNUC__) || defined(__clang__)
#define ROLLTUI_STR_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))
#else
#define ROLLTUI_STR_PRINTF(fmt, first)
#endif

// printf, appended. Nothing is truncated: a result longer than the stack scratch takes a heap one for the length of the call.
inline RolltuiStr& vappendf(RolltuiStr& s, const char* fmt, std::va_list args) {
  char stack[512];
  std::va_list again;
  va_copy(again, args);
  const int need = std::vsnprintf(stack, sizeof stack, fmt, args);
  if (need > 0) {
    if (static_cast<std::size_t>(need) < sizeof stack) {
      s.append(stack, static_cast<std::size_t>(need));
    } else {
      char* heap = static_cast<char*>(std::malloc(static_cast<std::size_t>(need) + 1));
      if (heap) {
        std::vsnprintf(heap, static_cast<std::size_t>(need) + 1, fmt, again);
        s.append(heap, static_cast<std::size_t>(need));
        std::free(heap);
      }
    }
  }
  va_end(again);
  return s;
}
inline RolltuiStr& appendf(RolltuiStr& s, const char* fmt, ...) ROLLTUI_STR_PRINTF(2, 3);
inline RolltuiStr& appendf(RolltuiStr& s, const char* fmt, ...) {
  std::va_list args;
  va_start(args, fmt);
  vappendf(s, fmt, args);
  va_end(args);
  return s;
}
inline RolltuiStr format(const char* fmt, ...) ROLLTUI_STR_PRINTF(1, 2);
inline RolltuiStr format(const char* fmt, ...) {
  RolltuiStr s;
  std::va_list args;
  va_start(args, fmt);
  vappendf(s, fmt, args);
  va_end(args);
  return s;
}
inline RolltuiStr to_str(long long v) { return format("%lld", v); }

// The last byte off, and every trailing `c`. A no-op on an empty string.
inline void pop_back(RolltuiStr& s) {
  if (s.n == 0) return;
  s.p[--s.n] = '\0';
}
inline void trim_trailing(RolltuiStr& s, char c) {
  while (s.n > 0 && s.p[s.n - 1] == c) pop_back(s);
}

// PATHS, as views: the last component ("a/b.txt" -> "b.txt"; "/" -> "/") and everything before it, without the slash
// ("a/b.txt" -> "a"; "b.txt" -> ""; "/x" -> "/"). A trailing slash is the caller's to trim first.
inline StrView path_base(StrView p) {
  if (p.size() == 1 && p[0] == '/') return p;
  const std::size_t slash = p.rfind('/');
  return slash == StrView::npos ? p : p.drop_front(slash + 1);
}
inline StrView path_dir(StrView p) {
  const std::size_t slash = p.rfind('/');
  if (slash == StrView::npos) return StrView();
  return slash == 0 ? p.first(1) : p.first(slash);
}

}  // namespace rolltui

#endif /* ROLLTUI_STR_HPP */
