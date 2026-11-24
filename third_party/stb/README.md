# stb_image (vendored)

The image decoder behind `rolltui_image_load` and `rolltui_image_decode`. `c/rolltui_image.c`
includes it with its functions made static, using the library's allocator, and with only PNG, JPEG,
GIF, BMP, TGA and PNM enabled. It's a single header with no dependencies (it has its own inflate).

- Upstream: https://github.com/nothings/stb
- Pinned commit: `9600522de53973336fa033a289858a4354cbb04c` (master, 2026-11-13), `stb_image.h` v2.32
- sha256: `deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91`
- Licence: MIT or public domain, whichever you prefer; the text is at the end of the header. rolltui
  uses it under the public-domain option, so programs built with it don't need a notice for it.
- Local modifications: none. To update, copy `stb_image.h` from the pinned commit or a newer one,
  update these lines, and run `rolltui-image-test` and `rolltui-paint-art-test`.
