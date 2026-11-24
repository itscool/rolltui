# stb_image (vendored)

The library's picture decoder: `rolltui_image_load` and `rolltui_image_decode` in
`c/rolltui_image.c`, where it is compiled with its functions static and the library's allocator, for
PNG, JPEG, GIF, BMP, TGA and PNM only. One header, no dependencies (it carries its own inflate).

- Upstream: https://github.com/nothings/stb
- Pinned commit: `9600522de53973336fa033a289858a4354cbb04c` (master, 2026-11-13), `stb_image.h` v2.32
- sha256: `deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91`
- Licence: MIT or public domain, the reader's choice; the text is at the end of the header. rolltui
  takes it as public domain, so a program built with it owes no notice for it.
- Local modifications: none. Re-vendor by copying `stb_image.h` from the pinned commit (or a newer
  one — update these lines) and re-running `rolltui-image-test` and `rolltui-paint-art-test`.
