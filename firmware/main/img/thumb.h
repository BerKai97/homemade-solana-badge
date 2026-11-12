#pragma once

// Thumbnails of PNG and JPEG pictures for ui.image: side x side pixels of
// RGB565 in LVGL's binary image format (a 12-byte lv_image_header_t, then the
// rows), cropped square from the middle. The picture streams past a row (PNG)
// or a block (JPEG, decoded at 1/2 to 1/8 scale when it's big) at a time, so
// any size of picture costs ~50 KB of RAM while it's read and nothing after.
// Transparency goes onto black.
//
// PNG: any colour type and bit depth, not interlaced, rows up to 8 KB (2048 px
// of RGBA). JPEG: baseline, not progressive.

// Writes the thumbnail of src to dst. NULL when done, else why not.
const char *img_thumb(const char *src, const char *dst, int side);
