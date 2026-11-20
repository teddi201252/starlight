#include "image_decode.h"

#include "peos/alloc.h"
#include "peos/peos.h"

static void *sl_stbi_memmove(void *dst, const void *src, size_t n) __attribute__((unused));
static void *sl_stbi_memmove(void *dst, const void *src, size_t n) {
    if (!dst || !src || n == 0) return dst;
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

// stb config for freestanding PeOS userland.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF   // the first frame: thumbnails (system apps plan, phase 5)
#define STBI_ONLY_BMP
#define STBI_MALLOC(sz) malloc(sz)
#define STBI_REALLOC(p,sz) realloc((p),(sz))
#define STBI_FREE(p) free(p)
#define STBI_ASSERT(x) ((void)0)
#define STBI_MEMCPY peos_memcpy
#define STBI_MEMSET peos_memset
#define STBI_MEMMOVE sl_stbi_memmove
#include "../../third_party/stb/stb_image.h"

static inline uint8_t sl_mip_u8_from_float(float v) {
    if (v <= 0.0f) return 0u;
    if (v >= 255.0f) return 255u;
    return (uint8_t)(v + 0.5f);
}

static int sl_image_build_mips(sl_image_t *image) {
    uint32_t prev_w = 0u;
    uint32_t prev_h = 0u;
    uint32_t prev_stride = 0u;
    const uint8_t *prev_data = NULL;

    if (!image || !image->data || image->channels != 4u) return 0;

    prev_w = image->width;
    prev_h = image->height;
    prev_stride = image->stride;
    prev_data = image->data;

    while ((prev_w > 1u || prev_h > 1u) && image->mip_count < SL_IMAGE_MIP_MAX) {
        uint32_t level = image->mip_count;
        uint32_t next_w = (prev_w > 1u) ? (prev_w / 2u) : 1u;
        uint32_t next_h = (prev_h > 1u) ? (prev_h / 2u) : 1u;
        uint32_t next_stride = next_w * 4u;
        uint8_t *next_data = (uint8_t *)malloc((size_t)next_stride * (size_t)next_h);
        if (!next_data) return 0;

        for (uint32_t y = 0u; y < next_h; y++) {
            for (uint32_t x = 0u; x < next_w; x++) {
                uint32_t sx0 = x * 2u;
                uint32_t sy0 = y * 2u;
                uint32_t sx1 = (sx0 + 1u < prev_w) ? (sx0 + 1u) : sx0;
                uint32_t sy1 = (sy0 + 1u < prev_h) ? (sy0 + 1u) : sy0;
                const uint8_t *p00 = prev_data + ((size_t)sy0 * (size_t)prev_stride) + ((size_t)sx0 * 4u);
                const uint8_t *p10 = prev_data + ((size_t)sy0 * (size_t)prev_stride) + ((size_t)sx1 * 4u);
                const uint8_t *p01 = prev_data + ((size_t)sy1 * (size_t)prev_stride) + ((size_t)sx0 * 4u);
                const uint8_t *p11 = prev_data + ((size_t)sy1 * (size_t)prev_stride) + ((size_t)sx1 * 4u);
                uint8_t *dst = next_data + ((size_t)y * (size_t)next_stride) + ((size_t)x * 4u);
                float acc_r = 0.0f;
                float acc_g = 0.0f;
                float acc_b = 0.0f;
                float acc_a = 0.0f;

                {
                    float a = (float)p00[3] / 255.0f;
                    acc_r += (float)p00[0] * a;
                    acc_g += (float)p00[1] * a;
                    acc_b += (float)p00[2] * a;
                    acc_a += a;
                }
                {
                    float a = (float)p10[3] / 255.0f;
                    acc_r += (float)p10[0] * a;
                    acc_g += (float)p10[1] * a;
                    acc_b += (float)p10[2] * a;
                    acc_a += a;
                }
                {
                    float a = (float)p01[3] / 255.0f;
                    acc_r += (float)p01[0] * a;
                    acc_g += (float)p01[1] * a;
                    acc_b += (float)p01[2] * a;
                    acc_a += a;
                }
                {
                    float a = (float)p11[3] / 255.0f;
                    acc_r += (float)p11[0] * a;
                    acc_g += (float)p11[1] * a;
                    acc_b += (float)p11[2] * a;
                    acc_a += a;
                }

                acc_a *= 0.25f;
                if (acc_a <= 0.0f) {
                    dst[0] = 0u;
                    dst[1] = 0u;
                    dst[2] = 0u;
                    dst[3] = 0u;
                } else {
                    dst[3] = sl_mip_u8_from_float(acc_a * 255.0f);
                    dst[0] = sl_mip_u8_from_float((acc_r * 0.25f) / acc_a);
                    dst[1] = sl_mip_u8_from_float((acc_g * 0.25f) / acc_a);
                    dst[2] = sl_mip_u8_from_float((acc_b * 0.25f) / acc_a);
                }
            }
        }

        image->mip_width[level] = next_w;
        image->mip_height[level] = next_h;
        image->mip_stride[level] = next_stride;
        image->mip_data[level] = next_data;
        image->mip_count++;

        prev_w = next_w;
        prev_h = next_h;
        prev_stride = next_stride;
        prev_data = next_data;
    }

    return 1;
}

// A JPEG's EXIF orientation (1..8), 1 without one: the tag 0x0112 of IFD0
// in the APP1 "Exif" segment (as Orion reads it, userland/apps/orion/src/turn.c).
static uint32_t sl_exif_rd(const uint8_t *p, int n, int le) {
    uint32_t v = 0u;
    for (int i = 0; i < n; i++) v |= (uint32_t)p[le ? i : n - 1 - i] << (8 * i);
    return v;
}

static int sl_jpeg_orientation(const uint8_t *d, size_t n) {
    if (!d || n < 4u || d[0] != 0xFFu || d[1] != 0xD8u) return 1;
    size_t i = 2u;
    while (i + 4u <= n && d[i] == 0xFFu) {
        uint8_t m = d[i + 1];
        if (m == 0xD9u || m == 0xDAu) break;   // the image data: no EXIF before it
        size_t seg = sl_exif_rd(d + i + 2u, 2, 0);
        if (seg < 2u || i + 2u + seg > n) break;
        if (m == 0xE1u && seg >= 8u + 8u && memcmp(d + i + 4u, "Exif\0\0", 6u) == 0) {
            const uint8_t *t = d + i + 10u;
            size_t tn = seg - 8u;
            int le = t[0] == 'I' && t[1] == 'I';
            if (!le && !(t[0] == 'M' && t[1] == 'M')) return 1;
            size_t ifd = sl_exif_rd(t + 4u, 4, le);
            if (ifd + 2u > tn) return 1;
            uint32_t count = sl_exif_rd(t + ifd, 2, le);
            for (uint32_t k = 0u; k < count; k++) {
                size_t e = ifd + 2u + (size_t)k * 12u;
                if (e + 12u > tn) break;
                if (sl_exif_rd(t + e, 2, le) == 0x0112u) {
                    uint32_t o = sl_exif_rd(t + e + 8u, 2, le);
                    return (o >= 1u && o <= 8u) ? (int)o : 1;
                }
            }
            return 1;
        }
        i += 2u + seg;
    }
    return 1;
}

// The stored pixels turned upright for orientation o: mirrored left to right
// first, then quarters clockwise (Orion's table). The thumbnails of
// Curiosity and of "Scegli un file", the wallpaper (system apps plan, phase 5).
static uint8_t *sl_orient_rgba(uint8_t *px, int *w, int *h, int o) {
    static const int Q[9] = { 0, 0, 0, 2, 2, 3, 1, 1, 3 };
    static const int M[9] = { 0, 0, 1, 0, 1, 1, 0, 1, 0 };
    if (o < 2 || o > 8) return px;
    int q = Q[o], m = M[o], sw = *w, sh = *h, nw = (q & 1) ? sh : sw, nh = (q & 1) ? sw : sh;
    uint32_t *out = (uint32_t *)malloc((size_t)sw * (size_t)sh * 4u);
    if (!out) return px;   // shown as stored
    const uint32_t *in = (const uint32_t *)px;
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int sx = m ? sw - 1 - x : x, dx, dy;
            switch (q) {
            case 1: dx = sh - 1 - y; dy = sx; break;
            case 2: dx = sw - 1 - sx; dy = sh - 1 - y; break;
            case 3: dx = y; dy = sw - 1 - sx; break;
            default: dx = sx; dy = y; break;
            }
            out[(size_t)dy * (size_t)nw + (size_t)dx] = in[(size_t)y * (size_t)sw + (size_t)x];
        }
    }
    stbi_image_free(px);
    *w = nw;
    *h = nh;
    return (uint8_t *)out;
}

static int sl_decode_internal(const uint8_t *data, size_t len, sl_image_t *out_image) {
    if (!data || !out_image || len == 0) return 0;
    if (len > 0x7fffffffU) return 0;

    peos_memset(out_image, 0, sizeof(*out_image));

    int w = 0, h = 0, channels = 0;
    // Force RGBA output so blending code is always deterministic.
    uint8_t *decoded = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    if (!decoded || w <= 0 || h <= 0) return 0;
    decoded = sl_orient_rgba(decoded, &w, &h, sl_jpeg_orientation(data, len));

    out_image->width = (uint32_t)w;
    out_image->height = (uint32_t)h;
    out_image->channels = 4u;
    out_image->stride = (uint32_t)w * 4u;
    out_image->data = decoded;
    if (!sl_image_build_mips(out_image)) {
        sl_image_free(out_image);
        return 0;
    }
    return 1;
}

int sl_decode_image(const uint8_t *image_data, size_t image_size, sl_image_t *out_image) {
    return sl_decode_internal(image_data, image_size, out_image);
}

int sl_decode_image_file(const char *path, sl_image_t *out_image) {
    if (!path || !out_image) return 0;

    uint64_t fd = peos_fopen(path, peos_strlen(path), 0);
    if ((int64_t)fd < 0) return 0;

    uint64_t sz = peos_fsize(fd);
    if ((int64_t)sz <= 0 || sz > 0x7fffffffU) {
        (void)peos_fclose(fd);
        return 0;
    }

    uint8_t *encoded = (uint8_t *)malloc((size_t)sz);
    if (!encoded) {
        (void)peos_fclose(fd);
        return 0;
    }

    uint64_t rd = peos_fread(fd, encoded, (size_t)sz);
    (void)peos_fclose(fd);
    if (rd != sz) {
        free(encoded);
        return 0;
    }

    int ok = sl_decode_internal(encoded, (size_t)sz, out_image);
    free(encoded);
    return ok;
}

int sl_decode_png(const uint8_t *png_data, size_t png_size, sl_image_t *out_image) {
    static const uint8_t png_sig[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    if (!png_data || png_size < sizeof(png_sig)) return 0;
    for (size_t i = 0; i < sizeof(png_sig); i++) {
        if (png_data[i] != png_sig[i]) return 0;
    }
    return sl_decode_internal(png_data, png_size, out_image);
}

int sl_decode_jpeg(const uint8_t *jpeg_data, size_t jpeg_size, sl_image_t *out_image) {
    if (!jpeg_data || jpeg_size < 4) return 0;
    if (jpeg_data[0] != 0xFFu || jpeg_data[1] != 0xD8u) return 0;
    return sl_decode_internal(jpeg_data, jpeg_size, out_image);
}

void sl_image_free(sl_image_t *image) {
    if (!image) return;
    for (uint32_t i = 0u; i < image->mip_count; i++) {
        if (image->mip_data[i]) free(image->mip_data[i]);
    }
    if (image->data) stbi_image_free(image->data);
    peos_memset(image, 0, sizeof(*image));
}
