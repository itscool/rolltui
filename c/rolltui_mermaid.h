#ifndef ROLLTUI_C_MERMAID_H
#define ROLLTUI_C_MERMAID_H
/*
 * rolltui/c/rolltui_mermaid.h — INTERNAL: a mermaid diagram, drawn as text.
 *
 * Mermaid is a way of WRITING a diagram; a terminal cannot run its renderer. This module reads the
 * text and draws what it describes with the characters a terminal has — box-drawing glyphs, arrows,
 * block elements — so a Markdown file that holds a diagram shows the diagram instead of its source.
 *
 * WHAT IT DRAWS
 *   flowchart / graph   nodes in their shapes (rectangle, rounded, stadium, subroutine, cylinder,
 *                       circle, diamond, hexagon, parallelogram, flag), edges in their styles
 *                       (arrow, line, dotted, thick, both ways, circle and cross ends) with their
 *                       labels, subgraphs as frames, in any of the four directions. Laid out in
 *                       layers, with the ordering that keeps edges from crossing where it can, and
 *                       edges routed in the channels between layers so they never cross a node.
 *   sequenceDiagram     participants across the top, lifelines, messages in all their arrow
 *                       styles, notes, activations, and the loop / alt / opt / par frames.
 *   stateDiagram        as a flowchart of rounded states, with the start and end markers and
 *                       composite states as frames.
 *   pie                 a bar for each slice, scaled to the width.
 *   classDiagram, erDiagram   records (a name over a rule over its members) joined by the relations
 *                       they name, with a mark at each end (a hollow triangle for a parent, a diamond
 *                       for a whole) and a cardinality by it.
 *   mindmap, timeline, journey, gantt   drawn as what they are: a tree, a list of periods on a
 *                       rail, tasks with a score in dots, bars on a dated axis.
 * A diagram that cannot be drawn — a kind not listed, a syntax error, or one wider than the room it
 * was given — is not drawn, and `reason` says which. Nothing is ever half-drawn.
 *
 * THE OUTPUT is lines of RUNS: text and the CLASS of the cells it covers (a node's label, a border, an
 * edge, an arrowhead, an edge's label, a title, something muted, or one of four accents), so the
 * caller colours the picture in its own theme's words. The module names no colour and no role.
 *
 * Where an ambiguous-width glyph is two cells (`ascii`), the box-drawing set would not line up, and
 * the picture is drawn in `+ - | > v` instead.
 *
 * THE BOUNDARY'S RULES, as everywhere in `c/`: an opaque handle the caller frees and reuses,
 * nothing returned by value, text out is a BORROW until the next render into the same handle.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RolltuiMermaid RolltuiMermaid;

#define ROLLTUI_MERMAID_CLASS_SPACE 0
#define ROLLTUI_MERMAID_CLASS_TEXT 1   /* a node's label, a participant's name */
#define ROLLTUI_MERMAID_CLASS_BOX 2    /* a border, a lifeline, a frame */
#define ROLLTUI_MERMAID_CLASS_EDGE 3   /* an edge's line */
#define ROLLTUI_MERMAID_CLASS_ARROW 4  /* an arrowhead, an end marker */
#define ROLLTUI_MERMAID_CLASS_LABEL 5  /* an edge's or a message's label */
#define ROLLTUI_MERMAID_CLASS_TITLE 6  /* a diagram's, a subgraph's, a frame's title */
#define ROLLTUI_MERMAID_CLASS_MUTED 7  /* a note, an axis, a dashed thing */
#define ROLLTUI_MERMAID_CLASS_ACCENT1 8 /* four colours to tell parts apart (pie slices, classes) */
#define ROLLTUI_MERMAID_CLASS_ACCENT2 9
#define ROLLTUI_MERMAID_CLASS_ACCENT3 10
#define ROLLTUI_MERMAID_CLASS_ACCENT4 11
#define ROLLTUI_MERMAID_CLASS_NODE 12  /* the border of a node or a participant: what the diagram is made of */

/* One run of cells that share a class. `text` is a BORROW (see above); `width` is its cells. */
typedef struct RolltuiMermaidRun {
  const char* text;
  size_t n;
  int width;
  unsigned char cls;
} RolltuiMermaidRun;

RolltuiMermaid* rolltui_mermaid_new(void);
void rolltui_mermaid_free(RolltuiMermaid* m); /* a no-op on NULL */

/* Draws `src` (`n` bytes: the text of the diagram, without its fence) in at most `max_width` cells, REPLACING
 * what `m` held. Returns 1 and leaves the picture in `m`, or returns 0 and leaves `m` empty, with the reason in
 * `*reason` (which may be NULL) in words a person can act on. */
int rolltui_mermaid_render(RolltuiMermaid* m, const char* src, size_t n, int max_width, int ascii, RolltuiStr* reason);

/* The picture. `line_count` lines, each `rolltui_mermaid_line`'s runs (its count returned, the runs a BORROW
 * valid until the next render), the widest line `width`, and the kind of diagram it was ("flowchart",
 * "sequence", "state", "pie", ...). */
size_t rolltui_mermaid_line_count(const RolltuiMermaid* m);
int rolltui_mermaid_width(const RolltuiMermaid* m);
size_t rolltui_mermaid_line(const RolltuiMermaid* m, size_t i, const RolltuiMermaidRun** runs);
const char* rolltui_mermaid_kind(const RolltuiMermaid* m);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_MERMAID_H */
