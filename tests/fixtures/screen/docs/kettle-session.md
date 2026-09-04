<!-- state: waiting -->
· waiting for the kettle to boil

<!-- user -->
raise the target temperature

<!-- assistant -->
Here is the change to `kettle.c`. The fence says `diff`, so the renderer asks the
highlighter the host registered; nothing sniffed the content to decide that.

```diff
--- a/kettle.c
+++ b/kettle.c
@@ -12,7 +12,7 @@ static void heat(void) {
   element_on();
-  int target = 80;
+  int target = 100;
   while (probe() < target) {
     wait_ms(200);
   }
   element_off();
```

The line pair above is where the word-level roles apply: `80` and `100` are the
changed run inside a changed line, and the `+` and `-` are the non-colour signal
that answers for a reader who cannot see either.

<!-- note -->
this entry is here to push the document past the window, so the transcript reports a
scroll extent and the window draws a thumb in its right border column

<!-- assistant -->
A screen is a set of files. This document is one of them, and everything on it —
the marked span above, the coloured block, the thumb in the border and the find bar
— arrived because a file said so, not because a host was rebuilt to allow it.
