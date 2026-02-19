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
#define STBI_MALLOC(sz) malloc(sz)
#define STBI_REALLOC(p,sz) realloc((p),(sz))
#define STBI_FREE(p) free(p)
#define STBI_ASSERT(x) ((void)0)
#define STBI_MEMCPY peos_memcpy
#define STBI_MEMSET peos_memset
#define STBI_MEMMOVE sl_stbi_memmove
#include "../../third_party/stb/stb_image.h"

static int sl_decode_internal(const uint8_t *data, size_t len, sl_image_t *out_image) {
    if (!data || !out_image || len == 0) return 0;
    if (len > 0x7fffffffU) return 0;

    peos_memset(out_image, 0, sizeof(*out_image));

    int w = 0, h = 0, channels = 0;
    // Force RGBA output so blending code is always deterministic.
    uint8_t *decoded = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    if (!decoded || w <= 0 || h <= 0) return 0;

    out_image->width = (uint32_t)w;
    out_image->height = (uint32_t)h;
    out_image->channels = 4u;
    out_image->stride = (uint32_t)w * 4u;
    out_image->data = decoded;
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
    if (image->data) stbi_image_free(image->data);
    peos_memset(image, 0, sizeof(*image));
}
