# md4c (vendored)

CommonMark + GitHub-flavoured Markdown parser, used by `rolltui/Markdown.hpp` behind
our own block-tree API so it stays an implementation detail (plan/phase-9.md,
"Build-vs-reuse": one `.c` + one `.h`, zero dependencies, current, and the alternative
is re-deriving 650+ spec examples).

- Upstream: https://github.com/mity/md4c
- Pinned commit: `61f5ce76647a93d60f652da8212e263ae2b4d8b7` (master, 2026-08-30)
- Files: `md4c.h`, `md4c.c` (from `src/`), `LICENSE.md` (MIT)
- Local modifications: none. Re-vendor by copying the two files from the pinned
  commit (or a newer one — update this line) and re-running `rolltui-markdown-test`.
