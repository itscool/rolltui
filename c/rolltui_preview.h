#ifndef ROLLTUI_C_PREVIEW_H
#define ROLLTUI_C_PREVIEW_H
/*
 * rolltui/c/rolltui_preview.h — INTERNAL: what a file looks like, drawn into a rectangle.
 *
 * The column browser (`rolltui_widget_picker.c`) owns one of these and shows it in the slot to the
 * right of the cursor when the cursor is on a file. It is a module of its own because it has
 * nothing to do with folders: it is handed a PATH and a RECT and draws what is in the file.
 *
 * WHAT IT SHOWS, decided once when the path is set:
 *   text      the file's lines, tabs expanded, a line longer than the pane cut at an ellipsis.
 *             READ WHOLE up to `ROLLTUI_PREVIEW_TEXT_LIMIT` (256 KB); a bigger file shows its
 *             first 256 KB and says so. Nothing is lazy: the line count is known the moment the
 *             file is, so the scrollbar is honest and End means the end.
 *   markdown  rendered, not listed — headings, lists, tables, code blocks, and a mermaid block as
 *             the diagram it describes. Same limit.
 *   hex       a file with a NUL in it, or mostly bytes that are not text, as offset, byte pairs
 *             and an ASCII gutter. The size is known, so nothing is read but what is on screen
 *             (a seek per frame) and a file of any size scrolls at once.
 *   empty / unreadable / not a file
 *             one line saying so, in words.
 *
 * NOTHING THE FILE HOLDS REACHES THE TERMINAL AS ITSELF. A control character, an escape, an
 * invalid byte is drawn as a visible stand-in (`␛`, `�`): a text file is data, and data that
 * moves the cursor or sets the title is a way for a file to talk to your terminal.
 *
 * THE BOUNDARY'S RULES, as everywhere in `c/`: an opaque handle the caller frees, nothing returned
 * by value, text out is a caller's `RolltuiStr` or a BORROW.
 */
#include "rolltui/rolltui.h"
#include "rolltui/c/rolltui_syntax.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiPreview RolltuiPreview;

#define ROLLTUI_PREVIEW_TEXT_LIMIT (256 * 1024)

#define ROLLTUI_PREVIEW_NONE 0
#define ROLLTUI_PREVIEW_TEXT 1
#define ROLLTUI_PREVIEW_MARKDOWN 2
#define ROLLTUI_PREVIEW_HEX 3
#define ROLLTUI_PREVIEW_EMPTY 4
#define ROLLTUI_PREVIEW_UNREADABLE 5

RolltuiPreview* rolltui_preview_new(void);
void rolltui_preview_free(RolltuiPreview* pv); /* a no-op on NULL */

/* Points the preview at a file, and reads it. The same path again is a no-op; a different one
 * replaces everything and scrolls to the top; an empty path clears it (kind NONE). */
void rolltui_preview_set_path(RolltuiPreview* pv, const char* path, size_t len);
int rolltui_preview_kind(const RolltuiPreview* pv);
/* COLOUR. Lends the preview a set of languages (`rolltui_syntax_new_standard`), which the caller keeps and frees AFTER the
 * preview. A text file that the set knows — by its name, its extension or its first line — is drawn with its keywords,
 * strings, comments and so on in the theme's colours, and the head says which language; a Markdown document's fenced code
 * is coloured the same way. NULL (the state of a new preview) draws everything plain. A file already shown is drawn again. */
void rolltui_preview_set_syntax(RolltuiPreview* pv, RolltuiSyntax* syn);
/* The reason an UNREADABLE file could not be shown; empty otherwise. A BORROW. */
const char* rolltui_preview_message(const RolltuiPreview* pv, size_t* len);

/* A FILE AS IT WAS WHEN IT WAS LAST READ, so a later look can tell whether it changed: which file it is (a file
 * replaced by another of the same name has another inode), how big, and when it was last written and last touched, to
 * the nanosecond the file system keeps. */
typedef struct RolltuiFileSig {
  int ok;  /* the file could be described; 0: it could not, and `err` is why (an errno) */
  int err;
  unsigned long long dev, ino;
  long long size, mtime_ns, ctime_ns;
} RolltuiFileSig;

/* Describes `path`, following links. Returns `sig->ok`. */
int rolltui_filesig_read(const char* path, RolltuiFileSig* sig);
/* The same file, in the same state: both failed the same way, or both succeeded and nothing about it moved. */
int rolltui_filesig_same(const RolltuiFileSig* a, const RolltuiFileSig* b);
/* WRITTEN TOO RECENTLY TO TRUST. A file system that keeps whole seconds can carry one stamp through two writes, so a
 * description taken within two seconds of the write says nothing about a second write: it must be looked at again
 * until it has aged out. `read_at_secs` is the wall clock when the description was taken. */
int rolltui_filesig_racy(const RolltuiFileSig* sig, long long read_at_secs);

/* WATCHING. Looks at the file again; when it is not what was read — written, replaced, truncated, gone, back — reads
 * it again and keeps the reader's place: the same line, row or paragraph at the top, and at the very end still at the
 * end, so a log that grows under a reader who is at its foot is followed. Returns 1 when it read again, 0 when the file
 * was as it was. Cheap when nothing changed: one `stat`. */
int rolltui_preview_refresh(RolltuiPreview* pv);

/* A CAPTION UNDER THE HEAD, set by whoever hosts the preview: one dim row on the panel's ground between the head and the
 * body, for what the host knows about the file and the preview does not (the picker's mode and time). An empty text takes
 * the row away again. The text is the host's own, drawn as given. */
void rolltui_preview_set_info(RolltuiPreview* pv, const char* text, size_t len);

/* THE FACTS A LISTING HOLDS ABOUT AN ENTRY, said one way wherever they are shown. `size_text`: "12 B", "1.2K", "34M".
 * `mode_text`: the type then the three rwx triplets, "drwxr-xr-x" ('l' for a link, '-' for anything else), 10 characters and
 * a NUL. `when_text`: local time to the minute, "2026-09-20 19:52", `date_only` for "2026-09-20"; "-" when unknown (<= 0). */
void rolltui_fileinfo_size_text(long long bytes, char* out, size_t cap);
void rolltui_fileinfo_mode_text(unsigned int mode, char out[11]);
void rolltui_fileinfo_when_text(long long secs, int date_only, char* out, size_t cap);

/* Draws into `r`: a head row (the file's name, and what it is), under it the caption when one is set, and the body. `styles`
 * is the theme's table. `focused` marks the head so a person can tell the keys are theirs. */
void rolltui_preview_draw(RolltuiPreview* pv, RolltuiFrame* f, RolltuiRect r, const RolltuiStyle* styles,
                          int ambiguous_wide, int focused);

/* Scrolling, in the units the body draws: a text line, a markdown line, a hex row. Each returns 1
 * when the view moved. `page` is what the last draw showed, less one. */
int rolltui_preview_scroll_by(RolltuiPreview* pv, long long lines);
int rolltui_preview_scroll_page(RolltuiPreview* pv, int direction);
int rolltui_preview_scroll_edge(RolltuiPreview* pv, int to_end);
int rolltui_preview_scroll_extent(const RolltuiPreview* pv, RolltuiScrollExtent* out);
int rolltui_preview_scroll_to(RolltuiPreview* pv, size_t first);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_PREVIEW_H */
