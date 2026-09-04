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
#include <string>
#include <vector>
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
  RolltuiMenuItemList(const RolltuiMenuItemList& o);
  RolltuiMenuItemList(RolltuiMenuItemList&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiMenuItemList& operator=(const RolltuiMenuItemList& o);
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
  void push_back(const RolltuiMenuItem& c);
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

  static RolltuiMenuItem action(std::string id, std::string label, std::string shortcut = {});
  static RolltuiMenuItem submenu(std::string id, std::string label, std::vector<RolltuiMenuItem> children);
  static RolltuiMenuItem toggle(std::string id, std::string label, bool checked);
  static RolltuiMenuItem choice(std::string id, std::string label, std::vector<RolltuiMenuItem> options,
                                std::string value);
  static RolltuiMenuItem input(std::string id, std::string label, std::string value = {});
  static RolltuiMenuItem input(std::string id, std::string label, RolltuiInputSpec spec, std::string value = {});
#endif
} RolltuiMenuItem;

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

#endif /* ROLLTUI_C_MENU_TREE_H */
