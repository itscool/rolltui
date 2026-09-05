// rolltui/DocumentCpp.cpp — the C++ half of `rolltui/c/rolltui_document.h`: the special
// members and the four list operations a host writes, and NOTHING ELSE. Every one calls the
// C functions in `c/rolltui_document.c`, so "release this entry" has one implementation
// (Phase 15 m5e). The C++ edge of data the library holds in C.
#include "rolltui/c/rolltui_document.h"

#include <utility>

// ---- the entry --------------------------------------------------------------------------

RolltuiDocEntry::RolltuiDocEntry() = default;
RolltuiDocEntry::~RolltuiDocEntry() = default;

RolltuiDocEntry::RolltuiDocEntry(const RolltuiDocEntry& o) { rolltui_doc_entry_copy(this, &o); }

RolltuiDocEntry::RolltuiDocEntry(RolltuiDocEntry&& o) noexcept
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

RolltuiDocEntry& RolltuiDocEntry::operator=(const RolltuiDocEntry& o) {
  if (this != &o) rolltui_doc_entry_copy(this, &o);
  return *this;
}

RolltuiDocEntry& RolltuiDocEntry::operator=(RolltuiDocEntry&& o) noexcept {
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

RolltuiDocument::RolltuiDocument(const RolltuiDocument& o) { rolltui_document_copy(this, &o); }

RolltuiDocument::~RolltuiDocument() { rolltui_document_release(this); }

RolltuiDocument& RolltuiDocument::operator=(const RolltuiDocument& o) {
  if (this != &o) rolltui_document_copy(this, &o);
  return *this;
}

RolltuiDocument& RolltuiDocument::operator=(RolltuiDocument&& o) noexcept {
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

void RolltuiDocument::push_back(RolltuiDocEntry&& e) { *rolltui_document_add(this) = std::move(e); }

void RolltuiDocument::push_back(const RolltuiDocEntry& e) {
  rolltui_doc_entry_copy(rolltui_document_add(this), &e);
}

void RolltuiDocument::clear() { rolltui_document_clear(this); }

void RolltuiDocument::resize(std::size_t k) {
  while (n > k) rolltui_doc_entry_release(v[--n]);
  while (n < k) rolltui_document_add(this);
}
