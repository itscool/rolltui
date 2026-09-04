# Two blocks that look identical, and are not

The first is under a ```diff fence, so it colours through the diff roles. The second is
under a bare fence and holds the SAME SHAPE of text, so it must render plain — content is
never sniffed (rolltui/Diff.hpp).

```diff
--- a/rolltui/Widgets.cpp
+++ b/rolltui/Widgets.cpp
@@ -226,10 +226,12 @@ class TranscriptWidget
 class TranscriptWidget : public WidgetBase {
  public:
   using WidgetBase::WidgetBase;
   Transcript t;
-  void layout(const ResolvedNode& rn) {
+  void layout(const ResolvedNode& rn) override {
+    sync_highlighter();
     if (const Document* d = document(content.source)) t.layout(*d, rn.inner, options(rn));
   }
 };
```

A bare fence over the same shape of text:

```
--- a/notes/shopping.txt
+++ b/notes/shopping.txt
@@ -1,3 +1,3 @@
 bread
-milk
+oat milk
```

Short enough to stay open, and a pair whose changed words are the only difference:

```diff
-const int cap = 100;
+const int cap = 200;
```
