// rolltui/Terminal.cpp — see Terminal.hpp.
#include "rolltui/Terminal.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>

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
    : in_(in_fd), out_(out_fd), opts_(opts), saved_(new Saved) {
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
  delete saved_;
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

std::vector<Event> Terminal::poll(int timeout_ms) {
  std::vector<Event> out;
  pollfd fds[2];
  fds[0] = {in_, POLLIN, 0};
  fds[1] = {wake_[0], POLLIN, 0};
  int n = ::poll(fds, wake_[0] >= 0 ? 2 : 1, timeout_ms);
  if (n < 0) {
    if (errno == EINTR) return out;
    return out;
  }
  if (n == 0) {  // timeout: a pending ESC is the Escape key
    if (decoder_.pending()) out = decoder_.flush();
    return out;
  }
  if (fds[1].revents & POLLIN) {
    char buf[64];
    while (::read(wake_[0], buf, sizeof buf) > 0) {}
    // Every SIGWINCH is reported, even when the size reads back the same: the frame
    // may still need a full repaint (some terminals clear on a font change).
    refresh_size();
    out.emplace_back(ResizeEvent{w_, h_});
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
