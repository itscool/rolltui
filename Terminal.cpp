// rolltui/Terminal.cpp — see Terminal.hpp. Everything but construction and poll()'s event
// conversion is a one-line forward and lives inline in the header now (Phase 17 m1); the
// real implementation is rolltui/c/rolltui_terminal.c.
#include "rolltui/Terminal.hpp"

namespace rolltui {

Terminal::Terminal(int in_fd, int out_fd, const TerminalOptions& opts)
    : h_(rolltui_terminal_new(in_fd, out_fd, opts)) {}

namespace {

// The sink poll() emits through: one event appended per call, with the borrowed bytes
// copied HERE, inside the window the boundary states, and nowhere else — the same shape
// Keys.cpp's collect() uses one layer down, widened by the one kind only the terminal
// makes (rolltui_terminal.h rule 4).
void collect(void* ctx, const RolltuiTermEvent* e) {
  std::vector<Event>& out = *static_cast<std::vector<Event>*>(ctx);
  switch (e->kind) {
    case ROLLTUI_TERM_EVENT_MOUSE:
      out.emplace_back(e->mouse);
      return;
    case ROLLTUI_TERM_EVENT_PASTE:
      out.emplace_back(PasteEvent{std::string(e->text, e->text_len)});
      return;
    case ROLLTUI_TERM_EVENT_RESIZE:
      out.emplace_back(ResizeEvent{e->w, e->h});
      return;
    default: {
      KeyEvent k = key_event_of(e->key);
      if (e->text) k.raw.assign(e->text, e->text_len);
      out.emplace_back(std::move(k));
      return;
    }
  }
}

}  // namespace

std::vector<Event> Terminal::poll(int timeout_ms) {
  std::vector<Event> out;
  rolltui_terminal_poll(h_.get(), timeout_ms, collect, &out);
  return out;
}

}  // namespace rolltui
