// rolltui/Terminal.cpp — see Terminal.hpp.
#include "rolltui/Terminal.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include "rolltui/Theme.hpp"  // parse_osc11_reply

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace rolltui {

struct Terminal::Saved {
  termios tio{};
  bool have_tio = false;
};

namespace {

// The active terminal's restore data, readable from a signal handler: raw pointers
// and a fixed buffer, set once when a Terminal enters and cleared when it leaves.
std::atomic<int> g_out_fd{-1};
std::atomic<int> g_wake_fd{-1};
char g_leave[256];
std::atomic<std::size_t> g_leave_len{0};
termios g_saved_tio;
std::atomic<bool> g_have_tio{false};
std::atomic<bool> g_handlers_installed{false};

void write_all_fd(int fd, const char* p, std::size_t n) {
  while (n > 0) {
    ssize_t k = ::write(fd, p, n);
    if (k < 0) {
      if (errno == EINTR) continue;
      return;
    }
    p += k;
    n -= static_cast<std::size_t>(k);
  }
}

void on_fatal_signal(int sig) {
  Terminal::restore_now();
  signal(sig, SIG_DFL);
  raise(sig);
}

void on_winch(int) {
  int fd = g_wake_fd.load();
  if (fd >= 0) {
    char c = 'w';
    ssize_t r = ::write(fd, &c, 1);
    (void)r;
  }
}

}  // namespace

void Terminal::restore_now() {
  int fd = g_out_fd.load();
  if (fd < 0) return;
  write_all_fd(fd, g_leave, g_leave_len.load());
  if (g_have_tio.load()) tcsetattr(fd, TCSANOW, &g_saved_tio);
  g_out_fd.store(-1);
}

Terminal::Terminal(int in_fd, int out_fd, const TerminalOptions& opts)
    : in_(in_fd), out_(out_fd), opts_(opts), saved_(std::make_unique<Saved>()) {
  tty_ = isatty(out_) != 0;
  if (::pipe(wake_) == 0) {
    fcntl(wake_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_[1], F_SETFL, O_NONBLOCK);
  }
  if (opts_.alt_screen) { enter_ += "\x1b[?1049h"; leave_.insert(0, "\x1b[?1049l"); }
  if (opts_.mouse) { enter_ += "\x1b[?1000h\x1b[?1002h\x1b[?1006h"; leave_.insert(0, "\x1b[?1006l\x1b[?1002l\x1b[?1000l"); }
  if (opts_.bracketed_paste) { enter_ += "\x1b[?2004h"; leave_.insert(0, "\x1b[?2004l"); }
  if (opts_.hide_cursor) { enter_ += "\x1b[?25l"; leave_.insert(0, "\x1b[?25h"); }
  leave_.insert(0, "\x1b[0m");  // attributes off before anything else
  enter();
  refresh_size();
}

Terminal::~Terminal() {
  leave();
  if (wake_[0] >= 0) ::close(wake_[0]);
  if (wake_[1] >= 0) ::close(wake_[1]);
  // saved_ is a unique_ptr since m2; ~Terminal is where it goes.
}

void Terminal::enter() {
  if (entered_) return;
  entered_ = true;
  if (tty_ && tcgetattr(out_, &saved_->tio) == 0) {
    saved_->have_tio = true;
    termios raw = saved_->tio;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(out_, TCSANOW, &raw);
    g_saved_tio = saved_->tio;
    g_have_tio.store(true);
  }
  std::size_t n = leave_.size() < sizeof g_leave ? leave_.size() : sizeof g_leave;
  std::memcpy(g_leave, leave_.data(), n);
  g_leave_len.store(n);
  g_out_fd.store(out_);
  g_wake_fd.store(wake_[1]);
  write(enter_);
  negotiate_keyboard();  // ask the terminal what it can deliver, before anything is typed
  if (opts_.handle_signals && !g_handlers_installed.exchange(true)) {
    for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT}) signal(sig, on_fatal_signal);
  }
  struct sigaction sa{};
  sa.sa_handler = on_winch;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGWINCH, &sa, nullptr);
}

void Terminal::leave() {
  if (!entered_) return;
  entered_ = false;
  write(leave_);
  if (saved_->have_tio) tcsetattr(out_, TCSANOW, &saved_->tio);
  g_out_fd.store(-1);
  g_wake_fd.store(-1);
  g_have_tio.store(false);
}

void Terminal::refresh_size() {
  winsize ws{};
  if (ioctl(out_, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
    w_ = ws.ws_col;
    h_ = ws.ws_row;
  }
}

void Terminal::write(std::string_view bytes) {
  write_all_fd(out_, bytes.data(), bytes.size());
}

void Terminal::wake() {
  if (wake_[1] < 0) return;
  char c = 'k';
  ssize_t r = ::write(wake_[1], &c, 1);  // non-blocking; a full pipe already wakes
  (void)r;
}

namespace {

// One CSI sequence starting at `pos`, or 0 if the bytes there are not a complete one.
std::size_t csi_span(const std::string& s, std::size_t pos) {
  if (pos + 1 >= s.size() || s[pos] != '\x1b' || s[pos + 1] != '[') return 0;
  std::size_t i = pos + 2;
  while (i < s.size() && static_cast<unsigned char>(s[i]) >= 0x30 && static_cast<unsigned char>(s[i]) <= 0x3F) ++i;
  while (i < s.size() && static_cast<unsigned char>(s[i]) >= 0x20 && static_cast<unsigned char>(s[i]) <= 0x2F) ++i;
  if (i >= s.size()) return 0;
  return (static_cast<unsigned char>(s[i]) >= 0x40 && static_cast<unsigned char>(s[i]) <= 0x7E) ? i + 1 - pos : 0;
}

}  // namespace

KeyProtocol Terminal::negotiate_keyboard(int timeout_ms) {
  protocol_ = KeyProtocol::Legacy;  // the conservative answer, and the one every failure keeps
  if (const char* forced = std::getenv("ROLLTUI_KEY_PROTOCOL")) {
    if (const std::optional<KeyProtocol> p = parse_key_protocol(forced)) protocol_ = *p;
  } else if (tty_) {
    write("\x1b[?u"     // kitty: which enhancement flags are set?
          "\x1b[?4m"    // xterm XTQUERYMODIFIERS: what is modifyOtherKeys?
          "\x1b[c");    // Primary DA: the terminator every terminal answers
    std::string buf;
    bool saw_da = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!saw_da) {
      const auto left =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (left <= 0) break;
      pollfd one{in_, POLLIN, 0};
      const int n = ::poll(&one, 1, static_cast<int>(left));
      if (n < 0) { if (errno == EINTR) continue; break; }
      if (n == 0) break;
      char b[512];
      const ssize_t k = ::read(in_, b, sizeof b);
      if (k <= 0) break;
      buf.append(b, static_cast<std::size_t>(k));
      saw_da = false;
      for (std::size_t i = 0; i + 2 < buf.size(); ++i)
        if (const std::size_t len = csi_span(buf, i); len && buf[i + 2] == '?' && buf[i + len - 1] == 'c') saw_da = true;
    }
    // Split the replies we asked for from everything else, which is somebody typing.
    std::string rest;
    for (std::size_t i = 0; i < buf.size();) {
      const std::size_t len = csi_span(buf, i);
      if (!len) { rest.push_back(buf[i]); ++i; continue; }
      const std::string_view seq(buf.data() + i, len);
      const char final = seq.back();
      const char lead = len > 2 ? seq[2] : '\0';
      if (final == 'u' && lead == '?') protocol_ = KeyProtocol::Kitty;                  // CSI ? flags u
      else if (final == 'm' && lead == '>') protocol_ = KeyProtocol::ModifyOtherKeys;   // CSI > 4 ; value m
      else if (final != 'c' || lead != '?') rest.append(seq);                           // not a reply: input
      i += len;
    }
    if (!rest.empty()) {
      const std::vector<Event> ev = decoder_.feed(rest);
      queued_.insert(queued_.end(), ev.begin(), ev.end());
    }
  }
  // Turn on what was found, and make sure every exit path turns it back off. The pop is
  // APPENDED to leave_ rather than prepended so the bytes already copied into the
  // signal handler's fixed buffer keep their offsets: a fatal signal landing between the
  // memcpy and the length store then still writes a complete, valid, shorter sequence.
  // Only ever onto a real terminal: with ROLLTUI_KEY_PROTOCOL set there may be no tty at
  // all, and writing mode bytes down a pipe would land them in somebody's captured frame.
  if (tty_ && protocol_ == KeyProtocol::Kitty) {
    write("\x1b[>1u");        // push the disambiguate flag
    leave_ += "\x1b[<1u";     // pop it
  } else if (tty_ && protocol_ == KeyProtocol::ModifyOtherKeys) {
    // Mode 1, not 2: "encode only keys with modifiers that produce non-standard
    // results", which is exactly what encode_key models. Mode 2 also escapes keys that
    // would produce a printable character, and its shift handling is the part xterm's
    // own documentation declines to pin down.
    write("\x1b[>4;1m");
    leave_ += "\x1b[>4m";     // no value: back to the terminal's initial state
  }
  if (entered_) {
    const std::size_t n = leave_.size() < sizeof g_leave ? leave_.size() : sizeof g_leave;
    std::memcpy(g_leave, leave_.data(), n);
    g_leave_len.store(n);
  }
  set_active_key_protocol(protocol_);
  return protocol_;
}

std::optional<Color> Terminal::query_background(int timeout_ms) {
  if (!tty_) return std::nullopt;
  write("\x1b]11;?\x1b\\");
  std::string buf;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  std::optional<Color> answer;
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) break;
    pollfd one{in_, POLLIN, 0};
    const int n = ::poll(&one, 1, static_cast<int>(left));
    if (n < 0) { if (errno == EINTR) continue; break; }
    if (n == 0) break;
    char b[512];
    const ssize_t k = ::read(in_, b, sizeof b);
    if (k <= 0) break;
    buf.append(b, static_cast<std::size_t>(k));
    const std::size_t at = buf.find("\x1b]11;");
    if (at == std::string::npos) continue;
    // A complete reply ends in ST (ESC \) or BEL; wait for it.
    std::size_t end = std::string::npos, end_len = 0;
    const std::size_t st = buf.find("\x1b\\", at + 5), bel = buf.find('\a', at + 5);
    if (st != std::string::npos && (bel == std::string::npos || st < bel)) { end = st; end_len = 2; }
    else if (bel != std::string::npos) { end = bel; end_len = 1; }
    if (end == std::string::npos) continue;
    answer = parse_osc11_reply(std::string_view(buf).substr(at, end + end_len - at));
    buf.erase(at, end + end_len - at);
    break;
  }
  // Whatever else arrived is input, not the reply: decode it for the next poll().
  if (!buf.empty()) {
    std::vector<Event> ev = decoder_.feed(buf);
    queued_.insert(queued_.end(), ev.begin(), ev.end());
  }
  return answer;
}

std::vector<Event> Terminal::poll(int timeout_ms) {
  std::vector<Event> out;
  if (!queued_.empty()) {
    out = std::move(queued_);
    queued_.clear();
    timeout_ms = 0;  // deliver now; pick up anything else already waiting
  }
  pollfd fds[2];
  fds[0] = {in_, POLLIN, 0};
  fds[1] = {wake_[0], POLLIN, 0};
  int n = ::poll(fds, wake_[0] >= 0 ? 2 : 1, timeout_ms);
  if (n < 0) {
    if (errno == EINTR) return out;
    return out;
  }
  if (n == 0) {  // timeout: a pending ESC is the Escape key
    if (decoder_.pending()) {
      std::vector<Event> rest = decoder_.flush();
      out.insert(out.end(), rest.begin(), rest.end());
    }
    return out;
  }
  if (fds[1].revents & POLLIN) {
    char buf[64];
    bool winch = false;
    ssize_t k;
    while ((k = ::read(wake_[0], buf, sizeof buf)) > 0)
      for (ssize_t i = 0; i < k; ++i) winch |= (buf[i] == 'w');  // 'k' is a plain wake()
    // Every SIGWINCH is reported, even when the size reads back the same: the frame
    // may still need a full repaint (some terminals clear on a font change).
    if (winch) {
      refresh_size();
      out.emplace_back(ResizeEvent{w_, h_});
    }
  }
  if (fds[0].revents & (POLLIN | POLLHUP)) {
    char buf[4096];
    ssize_t k = ::read(in_, buf, sizeof buf);
    if (k > 0) {
      std::vector<Event> ev = decoder_.feed(std::string_view(buf, static_cast<std::size_t>(k)));
      out.insert(out.end(), ev.begin(), ev.end());
      // A lone ESC (or a sequence cut by the read boundary) waits for the next read,
      // but not forever: give it 30 ms and then resolve it.
      if (decoder_.pending()) {
        pollfd one{in_, POLLIN, 0};
        if (::poll(&one, 1, 30) > 0 && (one.revents & POLLIN)) {
          ssize_t k2 = ::read(in_, buf, sizeof buf);
          if (k2 > 0) {
            std::vector<Event> more = decoder_.feed(std::string_view(buf, static_cast<std::size_t>(k2)));
            out.insert(out.end(), more.begin(), more.end());
          }
        }
        if (decoder_.pending()) {
          std::vector<Event> rest = decoder_.flush();
          out.insert(out.end(), rest.begin(), rest.end());
        }
      }
    }
  }
  return out;
}

}  // namespace rolltui
