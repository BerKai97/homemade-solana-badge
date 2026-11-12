// img_thumb (thumb.h): PNG through miniz's tinfl, JPEG through TJpgDec.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "img/thumb.h"
#include "vendor/miniz_tinfl.h"
#include "vendor/tjpgd.h"

#define PNG_ROW_MAX     8192            // bytes of one decoded row
#define JPEG_POOL       4096            // TJpgDec's work area (JD_FASTDECODE 1 needs ~3.5 KB)
#define IO_CHUNK        1024

// ---------------------------------------------------------------------------
// The picture as it streams past: the middle square [x0, x0 + c) x [y0, y0 + c)
// of a w x h picture, into side x side.

typedef struct {
    int w, h, x0, y0, c, side;
    uint16_t *out;                      // side * side RGB565
    uint32_t *acc;                      // PNG: side x {r, g, b, n} of output row acc_row
    int acc_row;
} sink_t;

static uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

static void sink_crop(sink_t *k, int w, int h)
{
    k->w = w;
    k->h = h;
    k->c = w < h ? w : h;
    k->x0 = (w - k->c) / 2;
    k->y0 = (h - k->c) / 2;
}

static void acc_flush(sink_t *k)
{
    if (k->acc_row < 0) {
        return;
    }
    for (int ox = 0; ox < k->side; ox++) {
        uint32_t *a = &k->acc[ox * 4];
        uint32_t n = a[3] ? a[3] : 1;
        k->out[k->acc_row * k->side + ox] = rgb565(a[0] / n, a[1] / n, a[2] / n);
    }
    memset(k->acc, 0, k->side * 4 * sizeof(uint32_t));
    k->acc_row = -1;
}

// Box filter: every pixel of the square counts toward the output pixel it falls in.
static void acc_pixel(sink_t *k, int x, int y, uint32_t r, uint32_t g, uint32_t b)
{
    int oy = (int)((int64_t)(y - k->y0) * k->side / k->c);
    if (oy != k->acc_row) {
        acc_flush(k);
        k->acc_row = oy;
    }
    uint32_t *a = &k->acc[(int)((int64_t)(x - k->x0) * k->side / k->c) * 4];
    a[0] += r;
    a[1] += g;
    a[2] += b;
    a[3]++;
}

static const char *write_bin(const char *dst, const sink_t *k)
{
    // lv_image_header_t, little-endian: magic, cf (RGB565), flags, w, h, stride, reserved
    uint8_t head[12] = {0x19, 0x12, 0, 0};
    uint16_t v[4] = {(uint16_t)k->side, (uint16_t)k->side, (uint16_t)(k->side * 2), 0};
    for (int i = 0; i < 4; i++) {
        head[4 + 2 * i] = v[i] & 0xff;
        head[5 + 2 * i] = v[i] >> 8;
    }
    char tmp[200];
    snprintf(tmp, sizeof(tmp), "%s.part", dst);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return "can't write the thumbnail";
    }
    size_t px = (size_t)k->side * k->side;
    bool ok = fwrite(head, 1, sizeof(head), f) == sizeof(head);
    for (size_t i = 0; ok && i < px; i++) {
        uint8_t le[2] = {k->out[i] & 0xff, k->out[i] >> 8};
        ok = fwrite(le, 1, 2, f) == 2;
    }
    ok = fclose(f) == 0 && ok;
    remove(dst);                        // FatFs rename() won't replace a file
    if (!ok || rename(tmp, dst) != 0) {
        remove(tmp);
        return "can't write the thumbnail";
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// PNG

typedef struct {
    FILE *f;
    sink_t *k;
    int depth, type, chans, bpp;        // bpp: bytes per pixel for the filters, at least 1
    size_t rowbytes, got;               // got: bytes of the current row (with its filter byte)
    int y;
    uint8_t *cur, *prev;                // [0] is the filter byte
    uint8_t pal[256][4];
    tinfl_decompressor *inf;
    uint8_t *dict;
    size_t dict_ofs;
    bool done;
} png_t;

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Pixel x of an unfiltered row as RGB, its alpha applied (onto black).
static void png_px(const png_t *p, const uint8_t *r, int x, uint32_t out[3])
{
    uint32_t s[4] = {0, 0, 0, 255};
    int step = p->depth == 16 ? 2 : 1;          // 16-bit samples: the high byte
    if (p->depth < 8) {
        int bit = x * p->depth, max = (1 << p->depth) - 1;
        int v = r[bit >> 3] >> (8 - p->depth - (bit & 7)) & max;
        if (p->type == 3) {
            for (int i = 0; i < 4; i++) {
                s[i] = p->pal[v][i];
            }
        } else {
            s[0] = s[1] = s[2] = (uint32_t)v * 255 / max;
        }
    } else {
        const uint8_t *q = r + (size_t)x * p->chans * step;
        switch (p->type) {
        case 0: s[0] = s[1] = s[2] = q[0]; break;
        case 2: s[0] = q[0]; s[1] = q[step]; s[2] = q[2 * step]; break;
        case 3: for (int i = 0; i < 4; i++) { s[i] = p->pal[q[0]][i]; } break;
        case 4: s[0] = s[1] = s[2] = q[0]; s[3] = q[step]; break;
        default: s[0] = q[0]; s[1] = q[step]; s[2] = q[2 * step]; s[3] = q[3 * step]; break;
        }
    }
    for (int i = 0; i < 3; i++) {
        out[i] = s[i] * s[3] / 255;
    }
}

static const char *png_row(png_t *p)
{
    uint8_t *r = p->cur + 1, *q = p->prev + 1;
    size_t n = p->rowbytes;
    int bpp = p->bpp;
    switch (p->cur[0]) {
    case 0:
        break;
    case 1:
        for (size_t i = bpp; i < n; i++) {
            r[i] += r[i - bpp];
        }
        break;
    case 2:
        for (size_t i = 0; i < n; i++) {
            r[i] += q[i];
        }
        break;
    case 3:
        for (size_t i = 0; i < n; i++) {
            r[i] += ((i >= (size_t)bpp ? r[i - bpp] : 0) + q[i]) >> 1;
        }
        break;
    case 4:
        for (size_t i = 0; i < n; i++) {
            r[i] += paeth(i >= (size_t)bpp ? r[i - bpp] : 0, q[i], i >= (size_t)bpp ? q[i - bpp] : 0);
        }
        break;
    default:
        return "broken PNG (row filter)";
    }
    sink_t *k = p->k;
    int y = p->y++;
    if (y >= k->y0 && y < k->y0 + k->c) {
        uint32_t c[3];
        if (k->c >= k->side) {
            for (int x = k->x0; x < k->x0 + k->c; x++) {
                png_px(p, r, x, c);
                acc_pixel(k, x, y, c[0], c[1], c[2]);
            }
        } else {
            // Fewer pixels than the thumbnail: each output pixel takes its nearest.
            for (int oy = 0; oy < k->side; oy++) {
                if (k->y0 + oy * k->c / k->side != y) {
                    continue;
                }
                for (int ox = 0; ox < k->side; ox++) {
                    png_px(p, r, k->x0 + ox * k->c / k->side, c);
                    k->out[oy * k->side + ox] = rgb565(c[0], c[1], c[2]);
                }
            }
        }
    }
    uint8_t *t = p->prev;
    p->prev = p->cur;
    p->cur = t;
    return NULL;
}

static const char *png_bytes(png_t *p, const uint8_t *b, size_t n)
{
    while (n && p->y < p->k->h) {
        size_t take = p->rowbytes + 1 - p->got;
        if (take > n) {
            take = n;
        }
        memcpy(p->cur + p->got, b, take);
        p->got += take;
        b += take;
        n -= take;
        if (p->got == p->rowbytes + 1) {
            p->got = 0;
            const char *why = png_row(p);
            if (why) {
                return why;
            }
        }
    }
    return NULL;
}

static const char *png_inflate(png_t *p, const uint8_t *in, size_t n)
{
    while (!p->done) {
        size_t in_bytes = n, out_bytes = TINFL_LZ_DICT_SIZE - p->dict_ofs;
        tinfl_status st = tinfl_decompress(p->inf, in, &in_bytes, p->dict, p->dict + p->dict_ofs, &out_bytes,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
        const char *why = out_bytes ? png_bytes(p, p->dict + p->dict_ofs, out_bytes) : NULL;
        if (why) {
            return why;
        }
        p->dict_ofs = (p->dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
        in += in_bytes;
        n -= in_bytes;
        if (st == TINFL_STATUS_DONE) {
            p->done = true;
        } else if (st < 0) {
            return "broken PNG (compressed data)";
        } else if (st == TINFL_STATUS_NEEDS_MORE_INPUT && !n) {
            break;
        } else if (!in_bytes && !out_bytes && st != TINFL_STATUS_HAS_MORE_OUTPUT) {
            return "broken PNG (compressed data)";
        }
    }
    return NULL;
}

static const char *png_decode(FILE *f, sink_t *k)
{
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    png_t *p = calloc(1, sizeof(*p));
    uint8_t *buf = malloc(IO_CHUNK);
    const char *why = NULL;
    if (!p || !buf) {
        why = "not enough memory";
        goto out;
    }
    p->f = f;
    p->k = k;
    memset(p->pal, 255, sizeof(p->pal));
    if (fread(buf, 1, 8, f) != 8 || memcmp(buf, SIG, 8)) {
        why = "not a PNG";
        goto out;
    }
    bool header = false;
    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) {
            why = header ? "the PNG ends early" : "not a PNG";
            break;
        }
        uint32_t len = be32(ch);
        const uint8_t *type = ch + 4;
        if (!memcmp(type, "IHDR", 4)) {
            if (len != 13 || fread(buf, 1, 13, f) != 13) {
                why = "broken PNG (header)";
                break;
            }
            int w = (int)be32(buf), h = (int)be32(buf + 4);
            p->depth = buf[8];
            p->type = buf[9];
            static const int CHANS[7] = {1, 0, 3, 1, 2, 0, 4};
            p->chans = p->type <= 6 ? CHANS[p->type] : 0;
            if (w <= 0 || h <= 0 || !p->chans || (p->depth != 1 && p->depth != 2 && p->depth != 4 &&
                                                  p->depth != 8 && p->depth != 16)) {
                why = "broken PNG (header)";
                break;
            }
            if (buf[12]) {
                why = "interlaced PNG: save it again without interlacing";
                break;
            }
            int bits = p->chans * p->depth;
            p->rowbytes = ((size_t)w * bits + 7) / 8;
            p->bpp = bits >= 8 ? bits / 8 : 1;
            if (p->rowbytes > PNG_ROW_MAX) {
                why = "PNG too wide (up to 2048 px): resize it";
                break;
            }
            sink_crop(k, w, h);
            p->cur = calloc(1, p->rowbytes + 1);
            p->prev = calloc(1, p->rowbytes + 1);
            p->inf = malloc(sizeof(tinfl_decompressor));
            p->dict = malloc(TINFL_LZ_DICT_SIZE);
            if (!p->cur || !p->prev || !p->inf || !p->dict) {
                why = "not enough memory";
                break;
            }
            tinfl_init(p->inf);
            header = true;
            len = 0;
        } else if (!header) {
            why = "broken PNG (no header)";
            break;
        } else if (!memcmp(type, "PLTE", 4) && len <= 768 && len % 3 == 0) {
            if (fread(buf, 1, len, f) != len) {
                why = "the PNG ends early";
                break;
            }
            for (uint32_t i = 0; i < len / 3; i++) {
                memcpy(p->pal[i], buf + 3 * i, 3);
            }
            len = 0;
        } else if (!memcmp(type, "tRNS", 4) && p->type == 3 && len <= 256) {
            if (fread(buf, 1, len, f) != len) {
                why = "the PNG ends early";
                break;
            }
            for (uint32_t i = 0; i < len; i++) {
                p->pal[i][3] = buf[i];
            }
            len = 0;
        } else if (!memcmp(type, "IDAT", 4)) {
            while (len && !why) {
                size_t n = fread(buf, 1, len < IO_CHUNK ? len : IO_CHUNK, f);
                if (!n) {
                    why = "the PNG ends early";
                    break;
                }
                len -= n;
                why = p->done ? NULL : png_inflate(p, buf, n);
            }
            if (why) {
                break;
            }
        } else if (!memcmp(type, "IEND", 4)) {
            if (p->y < k->h) {
                why = "the PNG ends early";
            }
            break;
        }
        if (fseek(f, (long)len + 4, SEEK_CUR) != 0) {     // the rest of the chunk and its CRC
            why = "the PNG ends early";
            break;
        }
    }
    acc_flush(k);
out:
    if (p) {
        free(p->cur);
        free(p->prev);
        free(p->inf);
        free(p->dict);
    }
    free(p);
    free(buf);
    return why;
}

// ---------------------------------------------------------------------------
// JPEG: decoded at the smallest 1/2^n scale that keeps the square at least
// side wide, then sampled at each output pixel's middle.

typedef struct {
    FILE *f;
    sink_t *k;
    int scale;
    uint16_t sx[128], sy[128];          // the source pixel each output column / row samples
} jpeg_t;

static size_t jpeg_in(JDEC *jd, uint8_t *buf, size_t n)
{
    jpeg_t *j = jd->device;
    if (!buf) {
        return fseek(j->f, (long)n, SEEK_CUR) == 0 ? n : 0;
    }
    return fread(buf, 1, n, j->f);
}

static int jpeg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_t *j = jd->device;
    sink_t *k = j->k;
    const uint8_t *rgb = bitmap;
    int bw = rect->right - rect->left + 1;
    for (int oy = 0; oy < k->side; oy++) {
        int y = j->sy[oy];
        if (y < rect->top || y > rect->bottom) {
            continue;
        }
        for (int ox = 0; ox < k->side; ox++) {
            int x = j->sx[ox];
            if (x >= rect->left && x <= rect->right) {
                const uint8_t *px = rgb + ((y - rect->top) * bw + (x - rect->left)) * 3;
                k->out[oy * k->side + ox] = rgb565(px[0], px[1], px[2]);
            }
        }
    }
    return 1;
}

static const char *jpeg_decode(FILE *f, sink_t *k)
{
    jpeg_t *j = calloc(1, sizeof(*j));
    void *pool = malloc(JPEG_POOL);
    JDEC jd;
    const char *why = NULL;
    if (!j || !pool) {
        why = "not enough memory";
        goto out;
    }
    j->f = f;
    j->k = k;
    JRESULT r = jd_prepare(&jd, jpeg_in, pool, JPEG_POOL, j);
    if (r != JDR_OK) {
        why = r == JDR_FMT3 ? "progressive JPEG: save it as a baseline JPEG or a PNG"
            : r == JDR_MEM1 ? "JPEG too complex for the badge" : "not a JPEG it can read";
        goto out;
    }
    int full = jd.width < jd.height ? jd.width : jd.height;
    while (j->scale < 3 && (full >> (j->scale + 1)) >= k->side) {
        j->scale++;
    }
    int div = 1 << j->scale;
    sink_crop(k, (jd.width + div - 1) / div, (jd.height + div - 1) / div);
    for (int i = 0; i < k->side; i++) {
        j->sx[i] = (uint16_t)(k->x0 + ((int64_t)i * k->c + k->c / 2) / k->side);
        j->sy[i] = (uint16_t)(k->y0 + ((int64_t)i * k->c + k->c / 2) / k->side);
    }
    r = jd_decomp(&jd, jpeg_out, (uint8_t)j->scale);
    if (r != JDR_OK) {
        why = "broken JPEG";
    }
out:
    free(j);
    free(pool);
    return why;
}

// ---------------------------------------------------------------------------

const char *img_thumb(const char *src, const char *dst, int side)
{
    if (side < 1 || side > 128) {
        return "bad thumbnail size";
    }
    FILE *f = fopen(src, "rb");
    if (!f) {
        return "not found";
    }
    uint8_t magic[3] = {0};
    size_t n = fread(magic, 1, 3, f);
    fseek(f, 0, SEEK_SET);
    sink_t k = { .side = side, .acc_row = -1 };
    k.out = calloc((size_t)side * side, sizeof(uint16_t));
    k.acc = calloc((size_t)side * 4, sizeof(uint32_t));
    const char *why;
    if (!k.out || !k.acc) {
        why = "not enough memory";
    } else if (n == 3 && magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N') {
        why = png_decode(f, &k);
    } else if (n == 3 && magic[0] == 0xff && magic[1] == 0xd8) {
        why = jpeg_decode(f, &k);
    } else {
        why = "not a PNG or JPEG";
    }
    fclose(f);
    if (!why) {
        why = write_bin(dst, &k);
    }
    free(k.out);
    free(k.acc);
    return why;
}
