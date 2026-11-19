// A stick that comes in: see stick.h.
#include "stick.h"

#include <stdio.h>
#include <string.h>

#include "app_host.h"
#include "peos/disks.h"
#include "peos/peos.h"

#define SL_STICK_CURIOSITY "/system/apps/curiosity.app"
#define SL_STICK_CFG "/system/config/disks.cfg"

static struct {
    uint8_t started;
    int64_t watch;
    char seen[PEOS_DISKS_MAX][96];   // the /mediaN mounted the last time
    uint32_t n_seen;
} g_stick = {.watch = -1};

// disks.cfg open=1: Curiosity on a stick just mounted (off when not there).
static int sl_stick_open_wanted(void) {
    char text[256];
    size_t n = 0;
    if (peos_read_file(SL_STICK_CFG, sizeof(SL_STICK_CFG) - 1u, text, sizeof(text) - 1u, &n) != 0) return 0;
    text[n] = '\0';
    for (const char *line = text; *line;) {
        if (!strncmp(line, "open=1", 6)) return 1;
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    return 0;
}

static int sl_stick_was_seen(const char *mount) {
    for (uint32_t i = 0; i < g_stick.n_seen; i++)
        if (!strcmp(g_stick.seen[i], mount)) return 1;
    return 0;
}

static void sl_stick_look(int act) {
    static peos_disk_info_t disks[PEOS_DISKS_MAX];
    int64_t n = peos_disks(disks, PEOS_DISKS_MAX);
    if (n < 0) return;
    char now[PEOS_DISKS_MAX][96];
    uint32_t count = 0;
    int open = act ? -1 : 0;   // disks.cfg read only when a disk is new
    for (int64_t i = 0; i < n && count < PEOS_DISKS_MAX; i++) {
        const char *m = disks[i].mount;
        if (strncmp(m, "/media", 6) != 0) continue;
        if (act && !sl_stick_was_seen(m)) {
            if (open < 0) open = sl_stick_open_wanted();
            if (open) {
                printf("starlight: stick %s: Curiosity on it\n", m);
                sl_app_host_open_with(SL_STICK_CURIOSITY, m);
            }
        }
        snprintf(now[count++], sizeof(now[0]), "%s", m);
    }
    memcpy(g_stick.seen, now, sizeof(now[0]) * count);
    g_stick.n_seen = count;
}

void sl_stick_tick(sl_ctx_t *ctx) {
    (void)ctx;
    if (!g_stick.started) {
        g_stick.started = 1u;
        g_stick.watch = peos_disks_watch();   // -1 without "mount": nothing to do
        if (g_stick.watch >= 0) sl_stick_look(0);   // what is there at start opens nothing
        return;
    }
    if (g_stick.watch < 0) return;
    uint64_t gen = 0;
    if (peos_fread_nb((uint64_t)g_stick.watch, &gen, sizeof(gen)) != sizeof(gen)) return;
    sl_stick_look(1);
}
