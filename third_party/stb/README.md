# stb_image (vendored)

The PNG decoder behind `rolltui-paint`'s `--open`. One header, no dependencies (it carries its own
inflate), compiled with `STBI_ONLY_PNG` so nothing but PNG is built.

- Upstream: https://github.com/nothings/stb
- Pinned commit: `9600522de53973336fa033a289858a4354cbb04c` (master, 2026-11-13), `stb_image.h` v2.32
- sha256: `deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91`
- Licence: MIT or public domain, the reader's choice; the text is at the end of the header.
- Local modifications: none. Re-vendor by copying `stb_image.h` from the pinned commit (or a newer
  one — update these lines) and re-running `rolltui-paint-art-test`.
