#ifndef ROLLTUI_C_SYNTAX_H
#define ROLLTUI_C_SYNTAX_H
/*
 * rolltui/c/rolltui_syntax.h — INTERNAL: what a piece of source looks like, as classes of text.
 *
 * `rolltui_syntax_*` holds LANGUAGES and finds the one a file or a fence names; `rolltui_highlight_*` runs one over a
 * text and keeps, for each line, the runs of it that are a keyword, a string, a comment... The module names no colour
 * and no theme role of its own beyond one table (`rolltui_syntax_role`) that says which existing role a class takes,
 * so a caller draws the runs in its theme's words and a theme that one day names syntax roles changes that table.
 *
 * A LANGUAGE IS DATA, and that is what makes a custom one possible. It is a JSON file — the shipped ones are in
 * `rolltui/presets/syntax/`, and a person's own are read from the `.json` files in `<config>/rolltui/syntax/`, where one with the
 * same name replaces the shipped language. What is in it:
 *
 *   {
 *     "name": "Lua",                       the name a fence may use, and what a person sees
 *     "aliases": ["lua"],                  the other names a fence may use (```lua, ```luau)
 *     "extensions": ["lua"],               `.lua`; the longest matching suffix wins (`.d.ts` beats `.ts`)
 *     "filenames": ["CMakeLists.txt"],     whole names
 *     "first_line": "^#!.*\\blua\\b",      a first line that says so (a shebang)
 *     "ignore_case": false,
 *     "not_after": ["."],                  a word after one of these is a member, not a keyword
 *     "word_chars": "-",                   bytes besides letters, digits and _ that a word may hold (a shell's `set-url`)
 *     "contexts": { "main": [ RULE, ... ], "other": [ ... ],
 *                   "block": { "class": "comment", "rules": [ ... ] } }   text no rule takes is drawn in that class
 *   }
 *
 * A RULE is tried in order at each position; the first that matches there takes it. At the END of a line, a rule that
 * can match nothing and has an action is tried once more (`{"match": "$", "pop": true}` ends a value with its line):
 *   { "match": REGEX, "class": "keyword", "captures": {"1": "keyword", "2": "function"},
 *     "push": "ctx" | "pop": true | "set": "ctx" }        a token, and what it does to the stack of contexts
 *   { "words": ["if", "else", ...], "class": "keyword" }    whole words, from a set
 *   { "region": { "begin": REGEX, "end": REGEX, "class": "string", "escape": REGEX, "rules": [ ... ],
 *                 "single_line": true, "begin_class": "...", "end_class": "..." } }
 *                                                           a string, a comment, a block: begin to end. No `end`
 *                                                           means the end of the line. `\1`..`\9` in `end` are the
 *                                                           groups of `begin` (a here-document, a Lua long bracket).
 *   { "include": "ctx" | "lang:name" | "lang:name/ctx" }    another context's rules, here (HTML holds JavaScript)
 * The patterns are the small subset `rolltui_regex.h` takes. A file with an error in it is refused WHOLE, with the
 * reason and where; it never half-loads, and a bad rule can slow a line but never hang it.
 *
 * THE BOUNDARY'S RULES, as everywhere in `c/`: an opaque handle the caller frees and reuses, nothing returned by
 * value, text out is a BORROW until the handle is run again or freed.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the classes a run may be --------------------------------------------------------------------- */
#define ROLLTUI_SYN_PLAIN 0
#define ROLLTUI_SYN_KEYWORD 1
#define ROLLTUI_SYN_TYPE 2
#define ROLLTUI_SYN_FUNCTION 3
#define ROLLTUI_SYN_STRING 4
#define ROLLTUI_SYN_ESCAPE 5    /* an escape sequence inside a string */
#define ROLLTUI_SYN_NUMBER 6
#define ROLLTUI_SYN_CONSTANT 7  /* true, null, a named constant */
#define ROLLTUI_SYN_COMMENT 8
#define ROLLTUI_SYN_DOC 9       /* a documentation comment */
#define ROLLTUI_SYN_OPERATOR 10
#define ROLLTUI_SYN_PUNCT 11
#define ROLLTUI_SYN_PREPROC 12  /* a preprocessor line, an import, a directive */
#define ROLLTUI_SYN_VARIABLE 13
#define ROLLTUI_SYN_ATTRIBUTE 14 /* a decorator, an annotation, an attribute */
#define ROLLTUI_SYN_TAG 15      /* a markup tag's name */
#define ROLLTUI_SYN_PROPERTY 16 /* a key, or a markup attribute's name */
#define ROLLTUI_SYN_SECTION 17  /* an ini section, a document marker */
#define ROLLTUI_SYN_COUNT 18

/* What a class asks for on top of its role, as bits: bold (a keyword) and italic (a comment). A theme's role gives the colour and
 * whatever its own style says; these are added, so a theme with no attributes of its own still has weight and slant in its code. */
#define ROLLTUI_SYN_BOLD 1
#define ROLLTUI_SYN_ITALIC 2
unsigned char rolltui_syntax_flags(unsigned char cls);

/* The theme role a class is drawn in: a table of EXISTING roles (no theme has to change for a language to be coloured),
 * one place to edit if a theme ever names roles for syntax. A class that is drawn as the block's own text answers the
 * base role passed in. */
unsigned char rolltui_syntax_role(unsigned char cls, unsigned char base);

/* The name a class goes by in a language file; NULL out of range. */
const char* rolltui_syntax_class_name(unsigned char cls);

/* ---- the set of languages --------------------------------------------------------------------------- */
typedef struct RolltuiSyntax RolltuiSyntax;

/* An empty set. */
RolltuiSyntax* rolltui_syntax_new(void);
/* The set a host wants: the shipped languages, then the person's own from `<config>/rolltui/syntax/`. A file that does
 * not load is noted in `report` (which may be NULL) and skipped: one bad file never takes the others with it. */
RolltuiSyntax* rolltui_syntax_new_standard(RolltuiStr* report);
void rolltui_syntax_free(RolltuiSyntax* s); /* a no-op on NULL */

/* Adds one language from its JSON. 1 on success; 0 with the reason (and the language's name and the rule, when it can say
 * which) in `err`. A language with the name of one already there replaces it. */
int rolltui_syntax_add(RolltuiSyntax* s, const char* json, size_t n, RolltuiStr* err);
void rolltui_syntax_add_shipped(RolltuiSyntax* s, RolltuiStr* report);
/* Every `*.json` in `dir`, in name order; a missing directory is not an error. Returns how many loaded. */
size_t rolltui_syntax_add_dir(RolltuiSyntax* s, const char* dir, RolltuiStr* report);

size_t rolltui_syntax_language_count(const RolltuiSyntax* s);
const char* rolltui_syntax_language_name(const RolltuiSyntax* s, int lang); /* NULL for a replaced or unknown one */

/* The language a fence's tag names (```rust, ```c++, case ignored), or -1. */
int rolltui_syntax_find_name(RolltuiSyntax* s, const char* name, size_t n);
/* The language for a file: by its whole name, then its longest matching extension, then by what its first line says
 * (`first_line` may be NULL). -1 for none. */
int rolltui_syntax_find_file(RolltuiSyntax* s, const char* path, size_t n, const char* first_line, size_t first_n);

/* ---- one text, run through a language ------------------------------------------------------------------- */
typedef struct RolltuiHighlight RolltuiHighlight;

/* One run of a line that is not plain text: bytes [begin, end) OF THE LINE, of one class. Sorted, never overlapping. */
typedef struct RolltuiSyntaxRun {
  unsigned int begin, end;
  unsigned char cls;
} RolltuiSyntaxRun;

RolltuiHighlight* rolltui_highlight_new(void);
void rolltui_highlight_free(RolltuiHighlight* h); /* a no-op on NULL */

/* Runs `lang` over `text`, REPLACING what `h` held. Lines end at '\n'; a final '\n' does not start another line, and a
 * '\r' before one is not part of the line. Returns the number of lines, or -1 for a language that is not there. A line
 * longer than a few thousand bytes is coloured as far as that and plain after it. */
long rolltui_highlight_run(RolltuiHighlight* h, RolltuiSyntax* s, int lang, const char* text, size_t n);
size_t rolltui_highlight_line_count(const RolltuiHighlight* h);
/* The runs of line `i` (zero-based); their count, the array a BORROW valid until the next run. */
size_t rolltui_highlight_line(const RolltuiHighlight* h, size_t i, const RolltuiSyntaxRun** runs);

/* ---- the Markdown seam ------------------------------------------------------------------------------------ */
/* What `RolltuiMdRenderOptions.highlight_ctx` is when `rolltui_syntax_md_highlight` is the highlighter: a set to look
 * languages up in and the run cached across the lines of a block (the seam calls once per line). */
typedef struct RolltuiSyntaxMd RolltuiSyntaxMd;
RolltuiSyntaxMd* rolltui_syntax_md_new(RolltuiSyntax* s, unsigned char base_role); /* `s` is BORROWED and must outlive it */
void rolltui_syntax_md_free(RolltuiSyntaxMd* m);
void rolltui_syntax_md_highlight(void* ctx, const char* lang, size_t lang_n, const RolltuiMdCodeLine* lines, size_t line_count,
                                 size_t index, RolltuiMdSpanSink emit, void* sink);

/* The shipped languages, as the build embeds them (`presets/syntax/`). */
extern const RolltuiEmbeddedFile rolltui_kSyntaxPresets[];
extern const size_t rolltui_kSyntaxPresetCount;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_SYNTAX_H */
