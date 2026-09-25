#ifndef NSPLIT_H
#define NSPLIT_H
/*
 * nsplit.h — an n-ary space-partitioning tree: drop these two files into any project that needs
 * one. No dependency beyond <stddef.h>; nothing here names rolltui.
 *
 * THE SHAPE: a tree where every node with children divides its own box along ONE axis (a column
 * stacks children top to bottom; a row places them left to right) among any number of them, each
 * sized as either a FIXED share (a fraction of the axis extent, plus or minus a fixed number of
 * units) or a FILL share (a weighted portion of whatever is left after every fixed sibling is
 * placed). A node with no children is a leaf; nothing about it needs an explicit tag for that —
 * `children.n == 0` already says so.
 *
 * WHAT THIS LIBRARY KNOWS: geometry. Every node carries a `seam` — the inset it reserves for
 * itself before dividing its own box among its children, the same width on every side. A caller
 * building bordered panels sets `seam` to the border's width; a caller building a docking system
 * with draggable splitters sets it to the gutter a splitter needs room to exist in; a caller that
 * wants siblings flush against each other, sharing an edge, sets it to 0. Which of those is right
 * is a caller decision — the library carries no default opinion and does no drawing of any kind.
 *
 * THE INTRUSIVE-STRUCT PATTERN, deliberately: a caller's own richer node type (with its own id,
 * content, style, whatever else it needs) embeds `NSplitNode` AS ITS FIRST MEMBER, the same shape
 * intrusive C data structures always use. A pointer to the caller's type and a pointer to its
 * embedded `NSplitNode` are the same address, so this library operates directly on a caller's own
 * tree — no copying, no shim layer, no callback indirection for the geometry itself. This library
 * never allocates or frees a node: a caller's node is bigger than `sizeof(NSplitNode)`, so only
 * the caller can allocate one correctly, and only the caller's own type knows how to free whatever
 * else it owns — the one place this library asks the caller for a callback is exactly there,
 * `nsplit_release`'s `destroy`.
 *
 * WHAT IS DELIBERATELY NOT HERE: tabs (a tab group is an ordinary leaf to this library — nothing
 * about "several things share one rect, one visible at a time" is a spatial question), and any
 * docking POLICY (deciding that a drag gesture means "undock this," deciding where a dropped pane
 * should land). Those consume this library's mutation primitives once a caller has already decided
 * what to do; deciding is never this library's job.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NSplitRect {
  int x, y, w, h;
} NSplitRect;

/* How one node is sized within its PARENT's axis. `fill` shares are resolved only after every
 * fixed sibling has taken its share of the extent; among fill siblings, each gets
 * `weight / (sum of every fill sibling's weight)` of what is left. A `fixed` size is
 * `fraction * extent + cells` (either alone is enough — `cells` alone is a plain fixed count;
 * `fraction` alone is a percentage; together, a percentage plus or minus a margin). */
typedef struct NSplitSize {
  unsigned char fill; /* 0: fixed (fraction * extent + cells); 1: a share of the remainder, by weight */
  int weight;         /* fill only; treated as at least 1 */
  double fraction;    /* fixed only */
  int cells;          /* fixed only */
} NSplitSize;

typedef struct NSplitNode {
  unsigned char axis;    /* 0: children stack in a column (top to bottom); 1: a row (left to right).
                          * Meaningless on a leaf. */
  unsigned char seam;    /* THIS node's own inset, each side, before its children (or its resolved
                          * inner rect, if it has none) are computed. */
  unsigned char visible; /* 0: resolves nothing for this node or anything under it, and takes no
                          * space in its parent's division. */
  NSplitSize size;       /* how this node is sized within ITS PARENT's axis; irrelevant for a root,
                          * which is sized by the caller's own `box` argument instead. */
  struct NSplitNode** children; /* OWNED array of BORROWED pointers — see nsplit_attach/_detach:
                                 * this library never allocates or frees what they point to. */
  size_t n, cap;
} NSplitNode;

/* Called once per node, in tree order (a container before its children), with both the box IT
 * OWNS before its own seam is subtracted (`outer`) and the box left for its children, or its own
 * content if it has none (`inner`). A hidden node (and everything under it) is skipped entirely —
 * not called with an empty rect, not called at all. */
typedef void (*NSplitSink)(void* ctx, const NSplitNode* node, NSplitRect outer, NSplitRect inner);

/* Resolves the whole tree into `box`. Pure: reads the tree, allocates nothing, touches nothing
 * outside `box`'s own coordinate space — a caller wanting the result clipped to some larger bound
 * (a screen, say) does that itself in `emit`, once per call, rather than this library carrying a
 * second box to intersect against. */
void nsplit_resolve(const NSplitNode* root, NSplitRect box, NSplitSink emit, void* ctx);

/* ---- mutation: the library manages the children array; the caller manages node memory -------- */

/* Inserts `node` as `parent`'s child at `index` (clamped to `parent`'s current count, so
 * `SIZE_MAX` — or any index at or past the end — appends). Does not allocate `node`, does not
 * touch anything already under it: attaching a subtree attaches all of it in one call. */
void nsplit_attach(NSplitNode* parent, size_t index, NSplitNode* node);

/* Unlinks `parent`'s child at `index` and returns it, OWNED by the caller from this point on —
 * free it (via `nsplit_release`, if it has children of its own), replant it as a new root, hand
 * it to `nsplit_attach` somewhere else. Its own children stay attached to it; this never recurses
 * into what it returns. Returns NULL if `index` is out of range — a caller checks, not a crash. */
NSplitNode* nsplit_detach(NSplitNode* parent, size_t index);

/* `nsplit_detach(old_parent, old_index)` followed by `nsplit_attach(new_parent, new_index, ...)`,
 * as one call — what a docking framework's undock-and-redock (or a plain drag-to-reorder) does
 * once it has already found where a node currently sits (the caller's own tree walk already knows
 * `old_parent`/`old_index`; this library keeps no parent back-pointer to rediscover it from). A
 * no-op if `old_parent` has nothing at `old_index`. */
void nsplit_reparent(NSplitNode* old_parent, size_t old_index, NSplitNode* new_parent, size_t new_index);

/* Post-order: every child of `root` is fully torn down (recursively, the same way) before `root`
 * itself is. For each node, in order: recurse into every child; free this node's own children
 * array and zero `children`/`n`/`cap` (so `destroy` below never sees a dangling pointer even if
 * it looks); call `destroy(node)` — the one callback this library needs, because only a caller's
 * own type knows how to free its own extra fields, and how the node itself was allocated. */
void nsplit_release(NSplitNode* root, void (*destroy)(NSplitNode* n));

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NSPLIT_H */
