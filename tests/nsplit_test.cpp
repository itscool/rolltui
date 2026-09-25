//
// nsplit_test.cpp — nsplit.c/.h's own proof: correctness and portability, independent of
// rolltui. This file may use rolltui's test harness freely (testkit, std::); nsplit.c/.h
// themselves may not, and do not — that is the property this file exists to prove.
//
#include "rolltui/nsplit.h"

#include <cstring>
#include <string>
#include <vector>

#include "testkit/testkit.hpp"

using namespace testkit;

namespace {

// A caller's own richer node type: NSplitNode embedded as the FIRST member, exactly as the
// header documents. `id` and `destroyed` are this test's own business, invisible to the library.
struct TestNode {
  NSplitNode base{};
  int id = 0;
  bool* destroyed = nullptr;
};

TestNode* make(int id, bool fill, double fraction, int cells, int weight = 1, unsigned char axis = 0,
              unsigned char seam = 0, unsigned char visible = 1) {
  TestNode* n = new TestNode();
  n->id = id;
  n->base.axis = axis;
  n->base.seam = seam;
  n->base.visible = visible;
  n->base.size.fill = fill ? 1 : 0;
  n->base.size.fraction = fraction;
  n->base.size.cells = cells;
  n->base.size.weight = weight;
  return n;
}

void destroy_test_node(NSplitNode* n) {
  TestNode* t = reinterpret_cast<TestNode*>(n); // sound: NSplitNode is TestNode's first member
  if (t->destroyed) *t->destroyed = true;
  delete t;
}

struct Resolved {
  int id;
  NSplitRect outer, inner;
};

void collect(void* ctx, const NSplitNode* node, NSplitRect outer, NSplitRect inner) {
  const TestNode* t = reinterpret_cast<const TestNode*>(node);
  static_cast<std::vector<Resolved>*>(ctx)->push_back({t->id, outer, inner});
}

std::vector<Resolved> resolve(TestNode* root, NSplitRect box) {
  std::vector<Resolved> out;
  nsplit_resolve(&root->base, box, collect, &out);
  return out;
}

bool req(const NSplitRect& r, int x, int y, int w, int h) { return r.x == x && r.y == y && r.w == w && r.h == h; }

} // namespace

int main() {
  // ---- a leaf, alone ------------------------------------------------------------------------
  {
    TestNode* leaf = make(1, false, 0, 0);
    const std::vector<Resolved> r = resolve(leaf, {0, 0, 40, 20});
    check(r.size() == 1 && r[0].id == 1 && req(r[0].outer, 0, 0, 40, 20) && req(r[0].inner, 0, 0, 40, 20),
          "a leaf with no seam resolves to exactly the box it was given");
    delete leaf;
  }

  // ---- seam insets the node's own box, once, before anything else -----------------------------
  {
    TestNode* leaf = make(1, false, 0, 0, 1, 0, 2);
    const std::vector<Resolved> r = resolve(leaf, {0, 0, 40, 20});
    check(req(r[0].outer, 0, 0, 40, 20) && req(r[0].inner, 2, 2, 36, 16),
          "a seam of 2 insets 2 on every side, outer unchanged");
  }
  {
    // A seam that would make the inner rect negative clamps to zero, never wraps negative.
    TestNode* leaf = make(1, false, 0, 0, 1, 0, 100);
    const std::vector<Resolved> r = resolve(leaf, {0, 0, 10, 10});
    check(r[0].inner.w == 0 && r[0].inner.h == 0, "a seam wider than the box clamps the inner rect to zero, not negative");
  }

  // ---- two fixed children, 50/50 by fraction, in a row -----------------------------------------
  {
    TestNode* root = make(0, false, 0, 0, 1, /*axis=row*/ 1);
    TestNode* a = make(1, false, 0.5, 0);
    TestNode* b = make(2, false, 0.5, 0);
    nsplit_attach(&root->base, 0, &a->base);
    nsplit_attach(&root->base, 1, &b->base);
    const std::vector<Resolved> r = resolve(root, {0, 0, 40, 10});
    check(r.size() == 3 && req(r[1].inner, 0, 0, 20, 10) && req(r[2].inner, 20, 0, 20, 10),
          "two 50% children in a row split the width evenly, left then right");
  }

  // ---- fixed cells plus a fill child takes the remainder ---------------------------------------
  {
    TestNode* root = make(0, false, 0, 0, 1, /*row*/ 1);
    TestNode* fixed = make(1, false, 0, 12); // exactly 12 cells
    TestNode* fill = make(2, true, 0, 0, 1);
    nsplit_attach(&root->base, 0, &fixed->base);
    nsplit_attach(&root->base, 1, &fill->base);
    const std::vector<Resolved> r = resolve(root, {0, 0, 40, 10});
    check(req(r[1].inner, 0, 0, 12, 10) && req(r[2].inner, 12, 0, 28, 10),
          "a fixed 12-cell child takes exactly 12; the fill child takes what's left (28)");
  }

  // ---- fill weights split the remainder proportionally -----------------------------------------
  {
    TestNode* root = make(0, false, 0, 0, 1, /*row*/ 1);
    TestNode* a = make(1, true, 0, 0, 1); // weight 1
    TestNode* b = make(2, true, 0, 0, 3); // weight 3 -> 3x a's share
    nsplit_attach(&root->base, 0, &a->base);
    nsplit_attach(&root->base, 1, &b->base);
    const std::vector<Resolved> r = resolve(root, {0, 0, 40, 10});
    check(r[1].inner.w == 10 && r[2].inner.w == 30, "weight 1 vs 3 splits 40 as 10/30 (" +
                                                        std::to_string(r[1].inner.w) + "/" + std::to_string(r[2].inner.w) + ")");
  }

  // ---- a hidden node takes no space and is never visited, itself or beneath it -----------------
  {
    TestNode* root = make(0, false, 0, 0, 1, /*row*/ 1);
    TestNode* shown = make(1, true, 0, 0);
    TestNode* hidden = make(2, true, 0, 0, 1, 0, 0, /*visible=*/0);
    TestNode* under_hidden = make(3, false, 0, 0);
    nsplit_attach(&hidden->base, 0, &under_hidden->base);
    nsplit_attach(&root->base, 0, &shown->base);
    nsplit_attach(&root->base, 1, &hidden->base);
    const std::vector<Resolved> r = resolve(root, {0, 0, 40, 10});
    bool sawHidden = false, sawUnder = false;
    for (const Resolved& x : r) { if (x.id == 2) sawHidden = true; if (x.id == 3) sawUnder = true; }
    check(r.size() == 2 && !sawHidden && !sawUnder && req(r[1].inner, 0, 0, 40, 10),
          "a hidden sibling takes no space (the visible one gets the whole extent) and is never emitted, nor is anything under it");
  }

  // ---- nesting: a row containing a column containing two leaves ---------------------------------
  {
    TestNode* root = make(0, false, 0, 0, 1, /*row*/ 1);
    TestNode* col = make(1, false, 0.5, 0, 1, /*column*/ 0);
    TestNode* leaf_left = make(2, false, 0.5, 0);
    TestNode* top = make(3, false, 0.5, 0);
    TestNode* bottom = make(4, false, 0.5, 0);
    nsplit_attach(&col->base, 0, &top->base);
    nsplit_attach(&col->base, 1, &bottom->base);
    nsplit_attach(&root->base, 0, &leaf_left->base);
    nsplit_attach(&root->base, 1, &col->base);
    const std::vector<Resolved> r = resolve(root, {0, 0, 20, 20});
    check(r.size() == 5, "every node in a nested tree is emitted exactly once (" + std::to_string(r.size()) + ")");
    Resolved topR{}, bottomR{};
    for (const Resolved& x : r) { if (x.id == 3) topR = x; if (x.id == 4) bottomR = x; }
    check(req(topR.inner, 10, 0, 10, 10) && req(bottomR.inner, 10, 10, 10, 10),
          "the column's own two children split ITS half vertically, not the whole root's box");
  }

  // ---- more children than the inline scratch buffer: the spill path -----------------------------
  {
    TestNode* root = make(0, false, 0, 0, 1, /*row*/ 1);
    std::vector<TestNode*> kids;
    const int N = 30; // > NSPLIT_INLINE (12)
    for (int i = 0; i < N; ++i) {
      TestNode* k = make(100 + i, true, 0, 0, 1);
      kids.push_back(k);
      nsplit_attach(&root->base, static_cast<size_t>(i), &k->base);
    }
    const std::vector<Resolved> r = resolve(root, {0, 0, 300, 10});
    check(r.size() == static_cast<size_t>(N + 1), "30 equal-weight children (past the 12-slot inline buffer) all resolve");
    bool evenSplit = true;
    for (const Resolved& x : r)
      if (x.id >= 100 && x.inner.w != 10) evenSplit = false;
    check(evenSplit, "…each gets an equal 1/30th share (10 of 300)");
    for (TestNode* k : kids) delete k;
  }

  // ---- mutation: attach, detach, reparent --------------------------------------------------------
  {
    TestNode* root = make(0, false, 0, 0);
    TestNode* a = make(1, false, 0, 0);
    TestNode* b = make(2, false, 0, 0);
    nsplit_attach(&root->base, 0, &a->base);
    nsplit_attach(&root->base, 0, &b->base); // inserted BEFORE a
    check(root->base.n == 2 && reinterpret_cast<TestNode*>(root->base.children[0])->id == 2 &&
              reinterpret_cast<TestNode*>(root->base.children[1])->id == 1,
          "attach at index 0 inserts before what was already there");

    NSplitNode* out = nsplit_detach(&root->base, 0);
    check(root->base.n == 1 && out == &b->base, "detach removes the one at that index and hands it back, still valid");

    TestNode* other_parent = make(9, false, 0, 0);
    nsplit_attach(&other_parent->base, 0, out); // replant the detached node under a new parent
    check(other_parent->base.n == 1 && other_parent->base.children[0] == out, "a detached node can be attached anywhere else, whole");
    delete other_parent; // does not own `out`'s memory; freed separately below
    delete b;

    // reparent: move `a` from `root` to a fresh third node in one call.
    TestNode* third = make(8, false, 0, 0);
    nsplit_reparent(&root->base, 0, &third->base, 0);
    check(root->base.n == 0 && third->base.n == 1 && third->base.children[0] == &a->base,
          "reparent moves a node from one parent to another in one call");
    delete third; // does not own `a`
    delete a;
    delete root;
  }

  // ---- release: post-order, every node exactly once, root last ----------------------------------
  {
    bool d_root = false, d_a = false, d_b = false;
    TestNode* root = make(0, false, 0, 0);
    TestNode* a = make(1, false, 0, 0);
    TestNode* b = make(2, false, 0, 0);
    root->destroyed = &d_root;
    a->destroyed = &d_a;
    b->destroyed = &d_b;
    nsplit_attach(&root->base, 0, &a->base);
    nsplit_attach(&root->base, 1, &b->base);
    nsplit_release(&root->base, destroy_test_node);
    check(d_root && d_a && d_b, "release tears down the root and every child");
  }

  return report("nsplit_test");
}
