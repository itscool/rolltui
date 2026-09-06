#ifndef ROLLTUI_C_DOCUMENT_H
#define ROLLTUI_C_DOCUMENT_H
/*
 * rolltui/c/rolltui_document.h — THE DOCUMENT MODEL, AS DATA (Phase 15 m5e).
 *
 * An ordered list of entries: text with a role, an optional prefix, markdown or verbatim,
 * foldable or not, possibly in a motion STATE. Every rule — what `version` means, why an
 * entry's state is not part of the layout cache's key, that text is what a renderer will
 * DRAW and never what a terminal will interpret — is stated in `rolltui/Document.hpp`; none
 * of it is repeated here.
 *
 * ---- WHY THIS IS IN BOTH CONFIGURATIONS ------------------------------------------------
 *
 * The same reason the two trees and the span store are: **it is DATA both implementations of
 * the transcript walk**, and it is filled by a HOST — roll's frontend, the studio, the tests
 * — so two shapes of it would be two things a host could disagree with. The flag chooses the
 * transcript's ALGORITHM, never what an entry is.
 *
 * ---- WHAT THE C++ LEFT IMPLICIT --------------------------------------------------------
 *
 * Four `std::string`s per entry and a `std::vector<DocEntry>` holding them. A host appends to
 * that vector every turn, so on each growth every existing entry is MOVED — four string
 * bookkeepings apiece — and every `const DocEntry*` anyone held is invalidated. Nobody
 * decided that; `push_back` is what you type. The list holds POINTERS here, so an entry's
 * address is stable and a growth moves `sizeof(void*)` per entry.
 */
#include <stddef.h>

#include "rolltui/c/rolltui_abi.h"
#include "rolltui/c/rolltui_str.h"
#include "rolltui/c/rolltui_style.h"

#ifdef __cplusplus
#include <cstddef>
#include <string>

namespace rolltui {
// Declared, not defined: the vocabularies live in `rolltui/Style.hpp` and
// `rolltui/Effects.hpp`, and this file names no role and no state (the m2 rule at
// `rolltui_diff.h`). An opaque enum declaration is a complete type because the underlying
// type is fixed, which is all a member needs.
enum class Role : unsigned char;
enum class EffectState : unsigned char;
}  // namespace rolltui
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiDocEntry {
  RolltuiStr id; /* stable identity across frames */
  unsigned long long version ROLLTUI_DEFAULT(0);
  RolltuiStr text;                           /* markdown source, or verbatim text */
  unsigned char markdown ROLLTUI_DEFAULT(1); /* 0: rendered as plain wrapped lines */
#ifdef __cplusplus
  rolltui::Role role = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_TEXT);
#else
  unsigned char role;
#endif
  RolltuiStr prefix; /* drawn before the first line, in `prefix_role` */
#ifdef __cplusplus
  rolltui::Role prefix_role = static_cast<rolltui::Role>(ROLLTUI_ROLE_DEFAULT_PROMPT);
#else
  unsigned char prefix_role;
#endif
  unsigned char foldable ROLLTUI_DEFAULT(0);
  RolltuiStr summary;                      /* the one-line summary a foldable entry shows */
  unsigned char folded ROLLTUI_DEFAULT(1); /* initial fold state of a foldable entry */
#ifdef __cplusplus
  rolltui::EffectState state = static_cast<rolltui::EffectState>(0); /* None */
#else
  unsigned char state;
#endif
  double progress ROLLTUI_DEFAULT(0); /* Progress: 0..1 */
  unsigned long long state_since_ms ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  RolltuiDocEntry();
  RolltuiDocEntry(const RolltuiDocEntry& o);
  RolltuiDocEntry(RolltuiDocEntry&& o) noexcept;
  RolltuiDocEntry& operator=(const RolltuiDocEntry& o);
  RolltuiDocEntry& operator=(RolltuiDocEntry&& o) noexcept;
  ~RolltuiDocEntry();
#endif
} RolltuiDocEntry;

void rolltui_doc_entry_init(RolltuiDocEntry* e);
void rolltui_doc_entry_release(RolltuiDocEntry* e);
void rolltui_doc_entry_copy(RolltuiDocEntry* to, const RolltuiDocEntry* from);

/* The entries, OWNED and individually allocated so an append never moves one. Laid out as
 * `RolltuiPtrVec` by construction, so the append mechanics are that one's. */
typedef struct RolltuiDocument {
  RolltuiDocEntry** v ROLLTUI_DEFAULT(nullptr);
  size_t n ROLLTUI_DEFAULT(0);
  size_t cap ROLLTUI_DEFAULT(0);

#ifdef __cplusplus
  struct iterator {
    RolltuiDocEntry** p;
    RolltuiDocEntry& operator*() const { return **p; }
    RolltuiDocEntry* operator->() const { return *p; }
    iterator& operator++() {
      ++p;
      return *this;
    }
    bool operator==(const iterator& o) const { return p == o.p; }
  };
  struct const_iterator {
    RolltuiDocEntry* const* p;
    const RolltuiDocEntry& operator*() const { return **p; }
    const RolltuiDocEntry* operator->() const { return *p; }
    const_iterator& operator++() {
      ++p;
      return *this;
    }
    bool operator==(const const_iterator& o) const { return p == o.p; }
  };

  RolltuiDocument() = default;
  RolltuiDocument(const RolltuiDocument& o);
  RolltuiDocument(RolltuiDocument&& o) noexcept : v(o.v), n(o.n), cap(o.cap) {
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  RolltuiDocument& operator=(const RolltuiDocument& o);
  RolltuiDocument& operator=(RolltuiDocument&& o) noexcept;
  ~RolltuiDocument();

  std::size_t size() const { return n; }
  bool empty() const { return n == 0; }
  RolltuiDocEntry& operator[](std::size_t i) { return *v[i]; }
  const RolltuiDocEntry& operator[](std::size_t i) const { return *v[i]; }
  RolltuiDocEntry& back() { return *v[n - 1]; }
  const RolltuiDocEntry& back() const { return *v[n - 1]; }
  iterator begin() { return {v}; }
  iterator end() { return {v + n}; }
  const_iterator begin() const { return {v}; }
  const_iterator end() const { return {v + n}; }
  void push_back(RolltuiDocEntry&& e);
  void push_back(const RolltuiDocEntry& e);
  void clear();
  /* Trims or grows, KEEPING the storage past the end — `std::vector<DocEntry>::resize`
   * destroys, and a transcript that trims and refills wants the entries back. */
  void resize(std::size_t k);
#endif
} RolltuiDocument;

size_t rolltui_document_count(const RolltuiDocument* d);
RolltuiDocEntry* rolltui_document_at(const RolltuiDocument* d, size_t i);
/* Appends an EMPTY entry and returns it — the C's `emplace_back`. */
RolltuiDocEntry* rolltui_document_add(RolltuiDocument* d);
void rolltui_document_clear(RolltuiDocument* d);   /* releases every entry, keeps the array */
void rolltui_document_release(RolltuiDocument* d); /* …and the array */
void rolltui_document_copy(RolltuiDocument* to, const RolltuiDocument* from);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
/* ---- the C++ special members of the structs above (Phase 17 m3) ---------------------------
 * Each one is a CALLER of a C function declared above it, so "release this subtree" has one
 * implementation and a destructor reaches it rather than being a second mechanism.
 *
 * They were out-of-line in `rolltui/DocumentCpp.cpp` until the C++ binding was deleted. They are not
 * part of that binding — they are what makes "the C++ type IS the C struct" true (Phase 14's
 * one-definition rule), so they had to keep a home; `inline`, beside the declarations they
 * implement, is that home and removes the last C++ translation unit from the library. */
inline RolltuiDocEntry::RolltuiDocEntry() = default;
inline RolltuiDocEntry::~RolltuiDocEntry() = default;

inline RolltuiDocEntry::RolltuiDocEntry(const RolltuiDocEntry& o) { rolltui_doc_entry_copy(this, &o); }

inline RolltuiDocEntry::RolltuiDocEntry(RolltuiDocEntry&& o) noexcept
    : id(std::move(o.id)),
      version(o.version),
      text(std::move(o.text)),
      markdown(o.markdown),
      role(o.role),
      prefix(std::move(o.prefix)),
      prefix_role(o.prefix_role),
      foldable(o.foldable),
      summary(std::move(o.summary)),
      folded(o.folded),
      state(o.state),
      progress(o.progress),
      state_since_ms(o.state_since_ms) {}

inline RolltuiDocEntry& RolltuiDocEntry::operator=(const RolltuiDocEntry& o) {
  if (this != &o) rolltui_doc_entry_copy(this, &o);
  return *this;
}

inline RolltuiDocEntry& RolltuiDocEntry::operator=(RolltuiDocEntry&& o) noexcept {
  if (this != &o) {
    id = std::move(o.id);
    version = o.version;
    text = std::move(o.text);
    markdown = o.markdown;
    role = o.role;
    prefix = std::move(o.prefix);
    prefix_role = o.prefix_role;
    foldable = o.foldable;
    summary = std::move(o.summary);
    folded = o.folded;
    state = o.state;
    progress = o.progress;
    state_since_ms = o.state_since_ms;
  }
  return *this;
}

// ---- the list ---------------------------------------------------------------------------

inline RolltuiDocument::RolltuiDocument(const RolltuiDocument& o) { rolltui_document_copy(this, &o); }

inline RolltuiDocument::~RolltuiDocument() { rolltui_document_release(this); }

inline RolltuiDocument& RolltuiDocument::operator=(const RolltuiDocument& o) {
  if (this != &o) rolltui_document_copy(this, &o);
  return *this;
}

inline RolltuiDocument& RolltuiDocument::operator=(RolltuiDocument&& o) noexcept {
  if (this != &o) {
    rolltui_document_release(this);
    v = o.v;
    n = o.n;
    cap = o.cap;
    o.v = nullptr;
    o.n = o.cap = 0;
  }
  return *this;
}

inline void RolltuiDocument::push_back(RolltuiDocEntry&& e) { *rolltui_document_add(this) = std::move(e); }

inline void RolltuiDocument::push_back(const RolltuiDocEntry& e) {
  rolltui_doc_entry_copy(rolltui_document_add(this), &e);
}

inline void RolltuiDocument::clear() { rolltui_document_clear(this); }

inline void RolltuiDocument::resize(std::size_t k) {
  while (n > k) rolltui_doc_entry_release(v[--n]);
  while (n < k) rolltui_document_add(this);
}
#endif /* __cplusplus */

#endif /* ROLLTUI_C_DOCUMENT_H */
