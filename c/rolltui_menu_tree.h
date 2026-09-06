#ifndef ROLLTUI_C_MENU_TREE_H
#define ROLLTUI_C_MENU_TREE_H
/*
 * rolltui/c/rolltui_menu_tree.h — THE MENU TREE, AS DATA (Phase 15 m5).
 *
 * An item is an action, a submenu, a toggle, a choice or a typed input field. What each one
 * MEANS is stated in `rolltui/Menu.hpp` and asserted in `rolltui/tests/menu_test.cpp`; none
 * of it is repeated here. Like the split tree (`rolltui_layout_tree.h`) this file is in BOTH
 * configurations, because it is DATA both implementations of the widget walk.
 *
 * ---- WHAT THE C++ LEFT IMPLICIT ---------------------------------------------------------
 *
 * A `MenuItem` owns SIX `std::string`s (id, label, action_name, shortcut, value, and the
 * spec's validator and hint make eight) plus a `std::vector<MenuItem>` of children. Its
 * defaulted `operator==` is a deep tree compare. `set_options(id, std::vector<MenuItem>)`
 * takes a whole subtree BY VALUE. None of that was decided; it is what you type — and the
 * design editor rebuilds a menu tree on every keystroke, so it is a real per-keystroke cost
 * nobody had a reason to look at.
 *
 * ---- THE SECOND OWNED-POINTER LIST, AND WHY IT IS NOT A THIRD INVENTION ------------------
 *
 * `RolltuiMenuItemList` is the same shape as `RolltuiNodeList`: an owned array of pointers to
 * individually allocated items, so a child's address is stable. Both are laid out exactly as
 * `RolltuiPtrVec` (`rolltui_str.h`) and both delegate their mechanics to it — a C++ template
 * would have made them one type, and in C the honest answer is one MECHANISM with two typed
 * faces rather than two copies of the mechanism. That asymmetry is the same one m3 measured
 * on `PresetStore` and it belongs in m6's verdict, not in a workaround here.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_str.h"

#ifdef __cplusplus
#include <cstddef>
#include <cstring>
#endif

/* The typed-input vocabulary, outside `extern "C"` so C++ keeps the scoped enum every call
 * site already writes (`InputType::Color`). */
#ifdef __cplusplus
namespace rolltui {
enum class InputType : unsigned char { Text, Int, Float, Color, Size, Dim, Name };
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ROLLTUI_INPUT_TYPE_TEXT 0
#define ROLLTUI_INPUT_TYPE_INT 1
#define ROLLTUI_INPUT_TYPE_FLOAT 2
#define ROLLTUI_INPUT_TYPE_COLOR 3
#define ROLLTUI_INPUT_TYPE_SIZE 4
#define ROLLTUI_INPUT_TYPE_DIM 5
#define ROLLTUI_INPUT_TYPE_NAME 6
#define ROLLTUI_INPUT_TYPE_COUNT 7

/* What an Input item accepts. `rolltui::InputSpec` IS this struct. */
typedef struct RolltuiInputSpec {
#ifdef __cplusplus
  rolltui::InputType type = rolltui::InputType::Text;
#else
  unsigned char type;
#endif
  double min ROLLTUI_DEFAULT(-1e15), max ROLLTUI_DEFAULT(1e15); /* Int / Float, inclusive */
  double step ROLLTUI_DEFAULT(1);                               /* Up / Down while editing a number */
  int precision ROLLTUI_DEFAULT(-1);   /* Float: most digits after the point; -1 = any */
  size_t max_len ROLLTUI_DEFAULT(0);   /* Text: 0 = no cap; Name: the type's own 64 */
  size_t min_len ROLLTUI_DEFAULT(0);   /* Text: checked at commit */
  unsigned char optional ROLLTUI_DEFAULT(0); /* empty commits as "" instead of being refused */
  RolltuiStr validator;                /* Text: a host-registered check by name, at commit */
  RolltuiStr hint;                     /* shown beside the field; input_hint(spec) when empty */

#ifdef __cplusplus
  bool operator==(const RolltuiInputSpec& o) const;
  RolltuiInputSpec clone() const;  /* `rolltui_input_spec_copy`, spelled (Phase 19 m2) */
#endif
} RolltuiInputSpec;

void rolltui_input_spec_init(RolltuiInputSpec* s);
void rolltui_input_spec_release(RolltuiInputSpec* s);
void rolltui_input_spec_copy(RolltuiInputSpec* to, const RolltuiInputSpec* from);
int rolltui_input_spec_equal(const RolltuiInputSpec* a, const RolltuiInputSpec* b);

/* ---- the item ------------------------------------------------------------------------------ */

struct RolltuiMenuItem;

/* An item's OWNED children (a Submenu's items, a Choice's options). Pointers, for the reason
 * `RolltuiNodeList` holds pointers: a child's address never moves. */
typedef struct RolltuiMenuItemList {
  struct RolltuiMenuItem** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  struct iterator {
    RolltuiMenuItem** p;
    RolltuiMenuItem& operator*() const { return **p; }
    RolltuiMenuItem* operator->() const { return *p; }
    iterator& operator++() {
      ++p;
      return *this;
    }
    iterator operator+(std::ptrdiff_t d) const { return {p + d}; }
    std::ptrdiff_t operator-(const iterator& o) const { return p - o.p; }
    bool operator==(const iterator& o) const { return p == o.p; }
  };
  struct const_iterator {
    RolltuiMenuItem* const* p;
    const_iterator() : p(nullptr) {}
    const_iterator(RolltuiMenuItem* const* q) : p(q) {}  // NOLINT(google-explicit-constructor)
    const_iterator(iterator i) : p(i.p) {}               // NOLINT(google-explicit-constructor)
    const RolltuiMenuItem& operator*() const { return **p; }
    const RolltuiMenuItem* operator->() const { return *p; }
    const_iterator& operator++() {
      ++p;
      return *this;
    }
    const_iterator operator+(std::ptrdiff_t d) const { return {p + d}; }
    std::ptrdiff_t operator-(const const_iterator& o) const { return p - o.p; }
    bool operator==(const const_iterator& o) const { return p == o.p; }
  };
  using value_type = RolltuiMenuItem;

  RolltuiMenuItemList() = default;
  RolltuiMenuItemList(const RolltuiMenuItemList&) = delete;  /* Phase 19 m2: `rolltui_menu_list_copy`, spelled */
  RolltuiMenuItemList(RolltuiMenuItemList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiMenuItemList& operator=(const RolltuiMenuItemList&) = delete;
  RolltuiMenuItemList& operator=(RolltuiMenuItemList&& o) noexcept;
  ~RolltuiMenuItemList();

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiMenuItem& operator[](std::size_t i) { return *v[i]; }
  const RolltuiMenuItem& operator[](std::size_t i) const { return *v[i]; }
  RolltuiMenuItem& front() { return *v[0]; }
  const RolltuiMenuItem& front() const { return *v[0]; }
  RolltuiMenuItem& back() { return *v[n - 1]; }
  const RolltuiMenuItem& back() const { return *v[n - 1]; }
  iterator begin() { return {v}; }
  iterator end() { return {v + n}; }
  const_iterator begin() const { return {v}; }
  const_iterator end() const { return {v + n}; }
  void push_back(RolltuiMenuItem&& c);
  void clear();
  bool operator==(const RolltuiMenuItemList& o) const;
#endif
} RolltuiMenuItemList;

#define ROLLTUI_MENU_ACTION 0
#define ROLLTUI_MENU_SUBMENU 1
#define ROLLTUI_MENU_TOGGLE 2
#define ROLLTUI_MENU_CHOICE 3
#define ROLLTUI_MENU_INPUT 4

typedef struct RolltuiMenuItem {
#ifdef __cplusplus
  enum class Kind : unsigned char { Action = 0, Submenu, Toggle, Choice, Input };
  Kind kind = Kind::Action;
#else
  unsigned char kind;
#endif
  RolltuiStr id;          /* the action id the host binds; an option's value id */
  RolltuiStr label;
  RolltuiStr action_name; /* the BINDINGS action this item does the same thing as */
  RolltuiStr shortcut;    /* display only; with `action_name` set it is the live chords */
  unsigned char enabled ROLLTUI_DEFAULT(1);
  unsigned char checked ROLLTUI_DEFAULT(0); /* Toggle */
  RolltuiStr value;       /* Choice: the current option id; Input: the COMMITTED text */
  RolltuiInputSpec spec;  /* Input: the type and its constraints */
  RolltuiMenuItemList children; /* Submenu: items; Choice: options */

#ifdef __cplusplus
  RolltuiMenuItem();
  bool operator==(const RolltuiMenuItem& o) const;
  RolltuiMenuItem clone() const;
  // The builders every host wrote items with — kept, in rolltui's own vocabulary (Phase 19 m2):
  // C strings in, `rolltui_menu_item_set` underneath. The std::string forms are gone; a host
  // with a std::string passes `.c_str()`.
  static RolltuiMenuItem action(const char* id, const char* label, const char* shortcut = "");
  static RolltuiMenuItem submenu(const char* id, const char* label);
  static RolltuiMenuItem toggle(const char* id, const char* label, bool checked);
  static RolltuiMenuItem choice(const char* id, const char* label, const char* value);  /* options: children.push_back */
  static RolltuiMenuItem input(const char* id, const char* label, const char* value = "");
  static RolltuiMenuItem input(const char* id, const char* label, RolltuiInputSpec spec, const char* value = "");
#endif
} RolltuiMenuItem;

/* Sets an item's kind, id and label in one call, releasing whatever they held — the C form of
 * the builders above, so a C host builds an item the way a C++ one does. `shortcut` may be
 * NULL with `shortcut_len` 0. */
void rolltui_menu_item_set(RolltuiMenuItem* it, unsigned char kind, const char* id, size_t id_len, const char* label,
                           size_t label_len, const char* shortcut, size_t shortcut_len);

void rolltui_menu_item_init(RolltuiMenuItem* it);
void rolltui_menu_item_release(RolltuiMenuItem* it); /* everything below and inside; leaves it clean */
void rolltui_menu_item_copy(RolltuiMenuItem* to, const RolltuiMenuItem* from);
int rolltui_menu_item_equal(const RolltuiMenuItem* a, const RolltuiMenuItem* b);
RolltuiMenuItem* rolltui_menu_item_new(void);
void rolltui_menu_item_free(RolltuiMenuItem* it); /* a no-op on NULL */

size_t rolltui_menu_list_count(const RolltuiMenuItemList* l);
RolltuiMenuItem* rolltui_menu_list_at(const RolltuiMenuItemList* l, size_t i);
RolltuiMenuItem* rolltui_menu_list_add(RolltuiMenuItemList* l); /* an EMPTY child, appended */
void rolltui_menu_list_clear(RolltuiMenuItemList* l);           /* frees every child */
void rolltui_menu_list_release(RolltuiMenuItemList* l);         /* …and the array */
void rolltui_menu_list_copy(RolltuiMenuItemList* to, const RolltuiMenuItemList* from);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
/* ---- the C++ special members of the structs above (Phase 17 m3) ---------------------------
 * Each one is a CALLER of a C function declared above it, so "release this subtree" has one
 * implementation and a destructor reaches it rather than being a second mechanism.
 *
 * They were out-of-line in `rolltui/MenuTree.cpp` until the C++ binding was deleted. They are not
 * part of that binding — they are what makes "the C++ type IS the C struct" true (Phase 14's
 * one-definition rule), so they had to keep a home; `inline`, beside the declarations they
 * implement, is that home and removes the last C++ translation unit from the library. */
inline bool RolltuiInputSpec::operator==(const RolltuiInputSpec& o) const {
  return rolltui_input_spec_equal(this, &o) != 0;
}

// ---- the child list ---------------------------------------------------------------------


inline RolltuiMenuItemList::~RolltuiMenuItemList() { rolltui_menu_list_release(this); }


inline RolltuiMenuItemList& RolltuiMenuItemList::operator=(RolltuiMenuItemList&& o) noexcept {
  if (this != &o) {
    rolltui_menu_list_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

inline void RolltuiMenuItemList::push_back(RolltuiMenuItem&& c) { *rolltui_menu_list_add(this) = std::move(c); }


inline void RolltuiMenuItemList::clear() { rolltui_menu_list_clear(this); }

inline bool RolltuiMenuItemList::operator==(const RolltuiMenuItemList& o) const {
  if (n != o.n) return false;
  for (std::size_t i = 0; i < n; ++i)
    if (!rolltui_menu_item_equal(v[i], o.v[i])) return false;
  return true;
}

// ---- the item ---------------------------------------------------------------------------

inline RolltuiMenuItem::RolltuiMenuItem() = default;

inline bool RolltuiMenuItem::operator==(const RolltuiMenuItem& o) const {
  return rolltui_menu_item_equal(this, &o) != 0;
}
inline RolltuiInputSpec RolltuiInputSpec::clone() const {
  RolltuiInputSpec out;
  rolltui_input_spec_copy(&out, this);
  return out;
}
inline RolltuiMenuItem RolltuiMenuItem::clone() const {
  RolltuiMenuItem out;
  rolltui_menu_item_copy(&out, this);
  return out;
}
inline RolltuiMenuItem RolltuiMenuItem::action(const char* id, const char* label, const char* shortcut) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Action), id, std::strlen(id), label, std::strlen(label), shortcut,
                        shortcut ? std::strlen(shortcut) : 0);
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::submenu(const char* id, const char* label) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Submenu), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::choice(const char* id, const char* label, const char* value) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Choice), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  it.value = value;
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::input(const char* id, const char* label, const char* value) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Input), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  it.value = value;
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::input(const char* id, const char* label, RolltuiInputSpec spec, const char* value) {
  RolltuiMenuItem it = input(id, label, value);
  it.spec = std::move(spec);
  return it;
}
inline RolltuiMenuItem RolltuiMenuItem::toggle(const char* id, const char* label, bool checked) {
  RolltuiMenuItem it;
  rolltui_menu_item_set(&it, static_cast<unsigned char>(Kind::Toggle), id, std::strlen(id), label, std::strlen(label), nullptr, 0);
  it.checked = static_cast<unsigned char>(checked);
  return it;
}






#endif /* __cplusplus */

#endif /* ROLLTUI_C_MENU_TREE_H */
