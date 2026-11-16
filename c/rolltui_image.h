#ifndef ROLLTUI_C_IMAGE_H
#define ROLLTUI_C_IMAGE_H
/* INTERNAL: the public declarations of this module live in `rolltui/rolltui.h` (a picture, and `rolltui_frame_image`). What is
 * below is the library's own: fitting and scaling, and the bytes each protocol sends, which the terminal writes at present. */
/*
 * rolltui/c/rolltui_image.h — PICTURES, fitted and sent.
 */
#include "rolltui/rolltui.h"

#ifdef __cplusplus
extern "C" {
#endif

unsigned int rolltui_image_id(const RolltuiImage* img);
/* The cells a picture takes in `cols` x `rows` with cells of `cell_w` x `cell_h` pixels (0: not known, about 1:2), its aspect kept
 * and never enlarged; and the pixel box it fills. */
void rolltui_image_fit(const RolltuiImage* img, int cols, int rows, int cell_w, int cell_h, int* used_cols, int* used_rows,
                       int* px_w, int* px_h);
/* The picture scaled to `dw` x `dh` into `out` (dw * dh * 4 bytes), each pixel the alpha-weighted average of those it covers. */
void rolltui_image_scale(const RolltuiImage* img, int dw, int dh, unsigned char* out);

/* ---- what each protocol sends, APPENDED to `out`; `tmux` wraps each sequence to pass through ---- */
void rolltui_image_kitty_upload(const RolltuiImage* img, int tmux, RolltuiStr* out);
void rolltui_image_kitty_place(unsigned int id, int cols, int rows, int tmux, RolltuiStr* out);
void rolltui_image_kitty_delete(unsigned int id, int tmux, RolltuiStr* out);
void rolltui_image_sixel(const RolltuiImage* img, int px_w, int px_h, RolltuiStr* out);
void rolltui_image_iterm2(const RolltuiImage* img, int cols, int rows, int tmux, RolltuiStr* out);

/* kitty's row and column diacritics, in order (a test checks them against the Unicode tables). */
size_t rolltui_image_diacritic_count(void);
unsigned int rolltui_image_diacritic(size_t i);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLTUI_C_IMAGE_H */
