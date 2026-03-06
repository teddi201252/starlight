#pragma once

#include <stddef.h>
#include <stdint.h>

#define SL_IMAGE_MIP_MAX 12u

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t stride;
    uint8_t *data; // decoded pixel data (typically RGBA8)
    uint32_t mip_count;
    uint32_t mip_width[SL_IMAGE_MIP_MAX];
    uint32_t mip_height[SL_IMAGE_MIP_MAX];
    uint32_t mip_stride[SL_IMAGE_MIP_MAX];
    uint8_t *mip_data[SL_IMAGE_MIP_MAX];
} sl_image_t;

// Decode a PNG image from memory. Returns 1 on success, 0 on failure.
int sl_decode_png(const uint8_t *png_data, size_t png_size, sl_image_t *out_image);

// Decode a JPEG image from memory. Returns 1 on success, 0 on failure.
int sl_decode_jpeg(const uint8_t *jpeg_data, size_t jpeg_size, sl_image_t *out_image);

// Decode any stb-supported image from memory.
int sl_decode_image(const uint8_t *image_data, size_t image_size, sl_image_t *out_image);

// Decode an image directly from a filesystem path.
int sl_decode_image_file(const char *path, sl_image_t *out_image);

// Release memory held by a decoded image.
void sl_image_free(sl_image_t *image);
