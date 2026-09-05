// rolltui/Input.cpp — the SHIM over `rolltui/c/rolltui_input.h`: the RAII, the styling
// vocabulary, the thirty action names, and the translation of one `std::function` into a
// function pointer. The two implementations live in `InputCpp.cpp` and `c/rolltui_input.c`,
// and this file is the C++ API over it (Phase 15 m5).
//
// Nothing here decides anything. It exists so the C boundary never has to know what a `Role`
// is called, what an action is called, or what a `std::function` is.
#include "rolltui/Input.hpp"

#include "rolltui/Scratch.hpp"  // rolltui::ThreadHandle

// THE OPTIONS' TWO C++ MEMBERS LIVE HERE, NOT IN EITHER IMPLEMENTATION. `InputOptions` is
// one struct shared by both sides of the flag, so its constructor and its `==` have to be in
// the file that is always linked — putting them in the deleted `InputCpp.cpp` made the C
// build fail to link, which is the boundary telling the truth about who owns what.
RolltuiInputOptions::RolltuiInputOptions() { rolltui_str_set(&prompt, "> ", 2); }

bool RolltuiInputOptions::operator==(const RolltuiInputOptions& o) const {
  return rolltui_input_options_equal(this, &o) != 0;
}

namespace rolltui {

namespace {

// THE THREE ROLES A DRAW NEEDS, handed in. `rolltui/Style.hpp` is the one place these names
// exist; the prompt's role is the OPTIONS' and travels with them (Phase 15 m2's rule, and
// the reason neither implementation names a role).
constexpr RolltuiInputRoles kRoles = {
    /*text=*/static_cast<unsigned char>(Role::input_text),
    /*selection=*/static_cast<unsigned char>(Role::selection),
    /*placeholder=*/static_cast<unsigned char>(Role::input_placeholder),
};


// What crosses instead of a `std::function`: the host's callable behind a `void*`.
void call_copy(void* ctx, const char* text, std::size_t len) {
  Input& in = *static_cast<Input*>(ctx);
  if (in.on_copy) in.on_copy(std::string(text, len));
}

// THE DRAW SCRATCH, owned per thread by the shim (rolltui/c/rolltui_frame_ops.h) — one
// owner, named, released at thread exit and at `release_thread()`.
RolltuiDrawScratch* draw_scratch() {
  static thread_local ThreadHandle<RolltuiDrawScratch, rolltui_draw_scratch_new, rolltui_draw_scratch_free> h;
  return h.get();
}

}  // namespace

// PHASE 17 m2a: the thirty names are `rolltui_library_actions.c`'s now — this file used to
// carry a second spelling of them, "in the command order `RolltuiInputActions` declares", and
// that ordering-by-hand is exactly what the C's designated expansion removes.
const RolltuiInputActions* input_actions() { return rolltui_input_default_actions(); }

Input::Input() { rolltui_input_set_copy(in_.get(), call_copy, this); }

Input::Input(Input&& o) noexcept : on_copy(std::move(o.on_copy)), in_(std::move(o.in_)) {
  rolltui_input_set_copy(in_.get(), call_copy, this);
}

Input& Input::operator=(Input&& o) noexcept {
  if (this != &o) {
    on_copy = std::move(o.on_copy);
    in_ = std::move(o.in_);
    rolltui_input_set_copy(in_.get(), call_copy, this);
  }
  return *this;
}

std::string_view Input::text() const {
  std::size_t n = 0;
  const char* p = rolltui_input_text(in_.get(), &n);
  return std::string_view(p, n);
}

void Input::set_text(std::string_view t) { rolltui_input_set_text(in_.get(), t.data(), t.size()); }

void Input::clear() { rolltui_input_clear(in_.get()); }

InputSelection Input::selection() const {
  InputSelection s;
  rolltui_input_selection(in_.get(), &s);
  return s;
}

std::string Input::selected_text() const {
  std::size_t n = 0;
  const char* p = rolltui_input_selected_text(in_.get(), &n);
  return std::string(p, n);
}

void Input::push_history(std::string_view entry) {
  rolltui_input_push_history(in_.get(), entry.data(), entry.size());
}

std::string_view Input::history_at(std::size_t i) const {
  std::size_t n = 0;
  const char* p = rolltui_input_history_at(in_.get(), i, &n);
  return std::string_view(p, n);
}

// The handle-taking form, for the same reason `menu_handle`/`transcript_handle` have one
// (Phase 17 m1c): what a host holds for an input the window table owns is a `RolltuiInput*`,
// and the thirty action names are this file's.
InputAction input_handle(RolltuiInput* in, const Event& e, const Bindings& bindings, std::uint64_t now_ms) {
  if (std::holds_alternative<ResizeEvent>(e)) return InputAction::Ignored;
  const RolltuiEvent ev = c_event_of(e);
  return static_cast<InputAction>(rolltui_input_handle(in, &ev, bindings.handle(), rolltui_input_default_actions(), now_ms));
}

InputAction Input::handle(const Event& e, const Bindings& bindings, std::uint64_t now_ms) {
  return input_handle(in_.get(), e, bindings, now_ms);
}

void Input::draw(Frame& f, const Theme& theme, bool focused) const {
  rolltui_input_draw(in_.get(), f.handle(), draw_scratch(), theme.styles.data(), &kRoles, focused);
}

Input::CellPos Input::cell_of(std::size_t offset) const {
  CellPos p;
  rolltui_input_cell_of(in_.get(), offset, &p.row, &p.col);
  return p;
}

std::optional<Input::Hit> Input::hit(int x, int y) const {
  Hit h;
  if (!rolltui_input_hit(in_.get(), x, y, &h.begin, &h.end)) return std::nullopt;
  return h;
}

Rect Input::area() const {
  Rect r;
  rolltui_input_area(in_.get(), &r);
  return r;
}

}  // namespace rolltui
