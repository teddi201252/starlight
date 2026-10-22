#include "skin.h"

#include "peos/peos.h"

#include <stdlib.h>

// One open, one read: the whole skin lands in a single buffer that the
// sprites and atlases are then read from in place.
int sl_skin_load(sl_skin_t *skin, const char *path) {
    if (!skin || !path) return 0;
    uint64_t fd = peos_fopen(path, peos_strlen(path), 0);
    if ((int64_t)fd < 0) return 0;
    uint64_t sz = peos_fsize(fd);
    if ((int64_t)sz <= 0 || sz > 0x7fffffffu) {
        (void)peos_fclose(fd);
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);   // 16-byte aligned
    if (!buf) {
        (void)peos_fclose(fd);
        return 0;
    }
    uint64_t rd = peos_fread(fd, buf, (size_t)sz);
    (void)peos_fclose(fd);
    if (rd != sz || !sl_skin_parse(skin, buf, (size_t)sz)) {
        free(buf);
        return 0;
    }
    return 1;
}
