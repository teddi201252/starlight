// The system menu: Informazioni su PeOS, Riavvia, Spegni (Technical Alpha
// plan, phase 1). The menu is a raised card under the logo, like the desk's
// context menu of the design; Riavvia and Spegni ask in a dialog over a night
// veil on the whole screen, and so does the about panel.

#include "sysmenu.h"

#include "desktop.h"
#include "render/blit.h"
#include "render/pixels.h"
#include "render/surface.h"
#include "ui_sdk.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

#define SM_TOP 56u
#define SM_W 280u
#define SM_PAD 10u
#define SM_ROW 44u
#define SM_SEP 14u
#define SM_SHADOW 24u
#define SM_LOGO_W 178u          // the logo and STARLIGHT
#define SM_VEIL 87u             // as the apps' dialogs (SLUI VEIL)
#define SM_BTN_H 44u

enum { SM_ITEM_ABOUT = 0, SM_ITEM_REBOOT, SM_ITEM_OFF, SM_ITEMS };
enum { SM_NONE = 0, SM_ASK_REBOOT, SM_ASK_OFF, SM_ABOUT };

static const char *const k_items[SM_ITEMS] = { "menu.about", "menu.restart", "menu.shutdown" };

static struct {
    uint8_t open;               // the menu
    uint8_t dialog;             // SM_ASK_*, SM_ABOUT
    int8_t hover;               // the row under the mouse
    int8_t btn_hover;           // the dialog's button under the mouse (0 cancel, 1 confirm)
    uint8_t live;               // the root is in RAM: what is written is lost
    uint8_t live_known;
    char version[32], starlight[32], commit[24], built[16];
    const slsk_font_t *row_f, *title_f, *text_f, *mono_f, *big_f;
} g_sm = { .hover = -1, .btn_hover = -1 };

// ----------------------------------------------------------------- places

static uint32_t sm_logo_x(const sl_ctx_t *ctx) {
    return ctx->fb.height < 800u ? 172u : 40u;   // as the top bar draws it
}

static uint32_t sm_menu_x(const sl_ctx_t *ctx) {
    uint32_t x = sm_logo_x(ctx);
    return x > 12u ? x - 12u : 0u;
}

static uint32_t sm_menu_h(void) { return 2u * SM_PAD + SM_ITEMS * SM_ROW + SM_SEP; }

static uint32_t sm_row_y(int i) {
    return SM_TOP + SM_PAD + (uint32_t)i * SM_ROW + (i >= SM_ITEM_REBOOT ? SM_SEP : 0u);
}

static void sm_dialog_size(uint32_t *w, uint32_t *h) {
    if (g_sm.dialog == SM_ABOUT) {
        *w = 520u;
        *h = 382u;
    } else {
        *w = 480u;
        *h = g_sm.live ? 230u : 206u;
    }
}

static void sm_dialog_rect(const sl_ctx_t *ctx, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    sm_dialog_size(w, h);
    *x = ctx->fb.width > *w ? (ctx->fb.width - *w) / 2u : 0u;
    *y = ctx->fb.height > *h ? (ctx->fb.height - *h) / 2u : 0u;
}

// The dialog's buttons: confirm at the right, cancel left of it (the about
// panel has only "Chiudi", in the confirm place).
static void sm_button_rect(const sl_ctx_t *ctx, int which, uint32_t *bx, uint32_t *by, uint32_t *bw) {
    uint32_t x, y, w, h;
    sm_dialog_rect(ctx, &x, &y, &w, &h);
    uint32_t cw = 120u, kw = 110u;
    *by = y + h - 28u - SM_BTN_H;
    *bw = which ? cw : kw;
    *bx = which ? x + w - 30u - cw : x + w - 30u - cw - 14u - kw;
}

static int sm_in(uint32_t px, uint32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static int sm_button_at(const sl_ctx_t *ctx, uint32_t px, uint32_t py) {
    for (int b = 0; b < 2; b++) {
        if (b == 0 && g_sm.dialog == SM_ABOUT) continue;
        uint32_t bx, by, bw;
        sm_button_rect(ctx, b, &bx, &by, &bw);
        if (sm_in(px, py, bx, by, bw, SM_BTN_H)) return b;
    }
    return -1;
}

static int sm_row_at(const sl_ctx_t *ctx, uint32_t px, uint32_t py) {
    uint32_t mx = sm_menu_x(ctx);
    for (int i = 0; i < SM_ITEMS; i++) {
        if (sm_in(px, py, mx + SM_PAD, sm_row_y(i), SM_W - 2u * SM_PAD, SM_ROW)) return i;
    }
    return -1;
}

static void sm_dirty_menu(sl_ctx_t *ctx) {
    uint32_t x = sm_menu_x(ctx);
    x = x > SM_SHADOW ? x - SM_SHADOW : 0u;
    sl_dirty_add(ctx, x, SM_TOP - SM_SHADOW, SM_W + 2u * SM_SHADOW, sm_menu_h() + 2u * SM_SHADOW);
}

static void sm_dirty_all(sl_ctx_t *ctx) { sl_dirty_add(ctx, 0u, 0u, ctx->fb.width, ctx->fb.height); }

// ------------------------------------------------------------------- facts

static void sm_cfg_value(const char *text, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    out[0] = '\0';
    for (const char *p = text; p && *p;) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            p += kl + 1u;
            size_t n = 0u;
            while (p[n] && p[n] != '\n' && p[n] != '\r' && n + 1u < cap) n++;
            memcpy(out, p, n);
            out[n] = '\0';
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

static void sm_learn(void) {
    if (g_sm.live_known) return;
    g_sm.live_known = 1u;
    char buf[1024];
    uint64_t n = peos_mounts(buf, sizeof(buf) - 1u);
    if (n != (uint64_t)-1 && n < sizeof(buf)) {
        buf[n] = '\0';
        g_sm.live = strstr(buf, "ramfs") != NULL;
    }
    snprintf(g_sm.version, sizeof(g_sm.version), "?");
    uint64_t fd = peos_fopen("/system/version.cfg", 19u, PEOS_O_READ);
    if (fd == (uint64_t)-1) return;
    char cfg[512];
    uint64_t got = peos_fread(fd, cfg, sizeof(cfg) - 1u);
    peos_fclose(fd);
    if (got == (uint64_t)-1) return;
    cfg[got] = '\0';
    sm_cfg_value(cfg, "version", g_sm.version, sizeof(g_sm.version));
    sm_cfg_value(cfg, "starlight", g_sm.starlight, sizeof(g_sm.starlight));
    sm_cfg_value(cfg, "commit", g_sm.commit, sizeof(g_sm.commit));
    sm_cfg_value(cfg, "built", g_sm.built, sizeof(g_sm.built));
}

static int sm_fonts(sl_ctx_t *ctx) {
    if (g_sm.row_f) return 1;
    if (!ctx->skin_loaded) return 0;
    const char *names[5] = { "ui-15", "ui-22-mid", "ui-15", "mono-14", "ui-34-light" };
    const slsk_font_t *f[5];
    for (int i = 0; i < 5; i++) {
        const slsk_entry_t *e = sl_skin_find(&ctx->skin, names[i]);
        if (!e) return 0;
        f[i] = sl_skin_font(&ctx->skin, e);
    }
    g_sm.row_f = f[0];
    g_sm.title_f = f[1];
    g_sm.text_f = f[2];
    g_sm.mono_f = f[3];
    g_sm.big_f = f[4];
    return 1;
}

// ----------------------------------------------------------------- actions

static void sm_set_dialog(sl_ctx_t *ctx, uint8_t dialog) {
    sm_learn();
    g_sm.dialog = dialog;
    g_sm.btn_hover = -1;
    if (g_sm.open) {
        sm_dirty_menu(ctx);
        g_sm.open = 0u;
    }
    sm_dirty_all(ctx);   // the veil comes or goes
}

// Every app is asked to close (as with its close button), then the machine.
static void sm_power(sl_ctx_t *ctx, int reboot) {
    (void)ctx;
    printf("[STARLIGHT] %s requested\n", reboot ? "reboot" : "power off");
    sl_windows_destroy_all();
    if (reboot) (void)peos_reboot();
    else (void)peos_poweroff();
    printf("[STARLIGHT] the firmware did not %s\n", reboot ? "reboot" : "power off");
}

void sl_sysmenu_show_about(sl_ctx_t *ctx) {
    if (ctx) sm_set_dialog(ctx, SM_ABOUT);
}

int sl_sysmenu_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    if (g_sm.dialog) {   // modal: nothing else gets the click
        int b = sm_button_at(ctx, x, y);
        if (b == 0) sm_set_dialog(ctx, SM_NONE);
        else if (b == 1) {
            uint8_t d = g_sm.dialog;
            sm_set_dialog(ctx, SM_NONE);
            if (d == SM_ASK_REBOOT || d == SM_ASK_OFF) sm_power(ctx, d == SM_ASK_REBOOT);
        }
        return 1;
    }
    if (y < SL_DESK_BAR_HEIGHT && x >= sm_logo_x(ctx) && x < sm_logo_x(ctx) + SM_LOGO_W) {
        g_sm.open = (uint8_t)!g_sm.open;
        g_sm.hover = -1;
        sm_dirty_menu(ctx);
        return 1;
    }
    if (!g_sm.open) return 0;
    int row = sm_row_at(ctx, x, y);
    if (row == SM_ITEM_ABOUT) sm_set_dialog(ctx, SM_ABOUT);
    else if (row == SM_ITEM_REBOOT) sm_set_dialog(ctx, SM_ASK_REBOOT);
    else if (row == SM_ITEM_OFF) sm_set_dialog(ctx, SM_ASK_OFF);
    else {   // a click elsewhere closes it, and does nothing else
        sm_dirty_menu(ctx);
        g_sm.open = 0u;
    }
    return 1;
}

int sl_sysmenu_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return -1;
    if (g_sm.dialog) {
        int b = sm_button_at(ctx, x, y);
        if (b != g_sm.btn_hover) {
            g_sm.btn_hover = (int8_t)b;
            uint32_t dx, dy, dw, dh;
            sm_dialog_rect(ctx, &dx, &dy, &dw, &dh);
            sl_dirty_add(ctx, dx, dy, dw, dh);
        }
        return b >= 0 ? SL_CUR_HAND : SL_CUR_ARROW;
    }
    int over_logo = y < SL_DESK_BAR_HEIGHT && x >= sm_logo_x(ctx) && x < sm_logo_x(ctx) + SM_LOGO_W;
    if (!g_sm.open) return over_logo ? SL_CUR_HAND : -1;
    int row = sm_row_at(ctx, x, y);
    if (row != g_sm.hover) {
        g_sm.hover = (int8_t)row;
        sm_dirty_menu(ctx);
    }
    if (row >= 0 || over_logo) return SL_CUR_HAND;
    return sm_in(x, y, sm_menu_x(ctx), SM_TOP, SM_W, sm_menu_h()) ? SL_CUR_ARROW : -1;
}

int sl_sysmenu_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev) {
    if (!ctx || !ev || (!g_sm.open && !g_sm.dialog)) return 0;
    if (!ev->pressed) return g_sm.dialog ? 1 : 0;
    if (ev->code == PEOS_KEY_ESC) {
        if (g_sm.dialog) sm_set_dialog(ctx, SM_NONE);
        else {
            sm_dirty_menu(ctx);
            g_sm.open = 0u;
        }
        return 1;
    }
    if (ev->code == PEOS_KEY_ENTER && g_sm.dialog) {
        uint8_t d = g_sm.dialog;
        sm_set_dialog(ctx, SM_NONE);
        if (d == SM_ASK_REBOOT || d == SM_ASK_OFF) sm_power(ctx, d == SM_ASK_REBOOT);
        return 1;
    }
    return g_sm.dialog ? 1 : 0;   // a dialog keeps the keys from the apps
}

// ------------------------------------------------------------------ drawing

static void sm_text(sl_target_t *t, const slsk_font_t *f, int32_t x, int32_t y, const char *s, sl_ui_color_token_t tok) {
    const sl_ui_color_t *c = sl_ui_runtime_color(tok);
    sl_text_draw(t, f, x, y + (f->px * 72 + 50) / 100, s, c->r, c->g, c->b);
}

static void sm_button(sl_ctx_t *ctx, sl_target_t *t, int which, const char *label, int primary) {
    uint32_t bx, by, bw;
    sm_button_rect(ctx, which, &bx, &by, &bw);
    int hot = g_sm.btn_hover == which;
    if (primary) sl_ui_ctx_surface(ctx, hot ? SL_UI_SURF_NIGHT_GLOW : SL_UI_SURF_NIGHT, bx, by, bw, SM_BTN_H, SM_BTN_H / 2u);
    else sl_ui_ctx_surface(ctx, hot ? SL_UI_SURF_RAISED_HI : SL_UI_SURF_RAISED, bx, by, bw, SM_BTN_H, SM_BTN_H / 2u);
    int32_t tw = (int32_t)sl_text_width(g_sm.row_f, label);
    sm_text(t, g_sm.row_f, (int32_t)(bx + bw / 2u) - tw / 2, (int32_t)by + 15, label,
            primary ? SL_UI_COLOR_NEON : SL_UI_COLOR_TEXT_PRIMARY);
}

static void sm_draw_menu(sl_ctx_t *ctx, sl_target_t *t) {
    uint32_t x = sm_menu_x(ctx);
    sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED, x, SM_TOP, SM_W, sm_menu_h(), 24u);
    for (int i = 0; i < SM_ITEMS; i++) {
        uint32_t ry = sm_row_y(i);
        if (g_sm.hover == i) sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, x + SM_PAD, ry, SM_W - 2u * SM_PAD, SM_ROW, SM_ROW / 2u);
        sm_text(t, g_sm.row_f, (int32_t)(x + SM_PAD + 18u), (int32_t)ry + 15, sl_tr(k_items[i]), SL_UI_COLOR_TEXT_PRIMARY);
    }
    const sl_ui_color_t *b = sl_ui_runtime_color(SL_UI_COLOR_BORDER);
    sl_fill_rect(ctx, x + SM_PAD + 14u, sm_row_y(SM_ITEM_REBOOT) - SM_SEP / 2u - 1u, SM_W - 2u * SM_PAD - 28u, 2u, b->r, b->g, b->b);
}

static void sm_draw_dialog(sl_ctx_t *ctx, sl_target_t *t) {
    uint32_t x, y, w, h;
    sm_dialog_rect(ctx, &x, &y, &w, &h);
    sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED, x, y, w, h, 32u);
    int32_t tx = (int32_t)x + 30, ty = (int32_t)y + 28;
    if (g_sm.dialog == SM_ABOUT) {
        sm_text(t, g_sm.big_f, tx, ty, "PeOS", SL_UI_COLOR_TEXT_PRIMARY);
        sm_text(t, g_sm.row_f, tx + (int32_t)sl_text_width(g_sm.big_f, "PeOS") + 14, ty + 14, "Technical Alpha",
                SL_UI_COLOR_TEXT_SECONDARY);
        peos_sysinfo_t si;
        memset(&si, 0, sizeof(si));
        (void)peos_sysinfo(&si);
        char rows[6][2][64];
        snprintf(rows[0][0], 64, "%s", sl_tr("about.version"));
        snprintf(rows[0][1], 64, "%s%s%s", g_sm.version, g_sm.commit[0] ? " \xC2\xB7 " : "", g_sm.commit);
        snprintf(rows[1][0], 64, "Starlight");
        snprintf(rows[1][1], 64, "%s", g_sm.starlight[0] ? g_sm.starlight : "?");
        snprintf(rows[2][0], 64, "%s", sl_tr("about.processors"));
        snprintf(rows[2][1], 64, "%u", (unsigned)(si.cpu_count ? si.cpu_count : 1u));
        snprintf(rows[3][0], 64, "%s", sl_tr("about.memory"));
        snprintf(rows[3][1], 64, "%llu MB", (unsigned long long)(si.ram_total / (1024u * 1024u)));
        snprintf(rows[4][0], 64, "%s", sl_tr("about.screen"));
        snprintf(rows[4][1], 64, "%u x %u", (unsigned)ctx->fb.width, (unsigned)ctx->fb.height);
        snprintf(rows[5][0], 64, "%s", sl_tr("about.system"));
        snprintf(rows[5][1], 64, "%s", sl_tr(g_sm.live ? "about.live" : "about.disk"));
        int32_t ry = ty + 64;
        for (int i = 0; i < 6; i++, ry += 32) {
            sm_text(t, g_sm.text_f, tx, ry, rows[i][0], SL_UI_COLOR_TEXT_SECONDARY);
            sm_text(t, i < 4 ? g_sm.mono_f : g_sm.text_f, tx + 130, ry + (i < 4 ? 1 : 0), rows[i][1],
                    SL_UI_COLOR_TEXT_PRIMARY);
        }
        if (g_sm.built[0]) {
            char b[96];
            snprintf(b, sizeof(b), sl_tr("about.built"), g_sm.built);
            sm_text(t, g_sm.text_f, tx, (int32_t)(y + h) - 28 - (int32_t)SM_BTN_H + 14, b, SL_UI_COLOR_TEXT_SECONDARY);
        }
        sm_button(ctx, t, 1, sl_tr("common.close"), 1);
        return;
    }
    int off = g_sm.dialog == SM_ASK_OFF;
    sm_text(t, g_sm.title_f, tx, ty + 4, sl_tr(off ? "power.ask_off" : "power.ask_restart"), SL_UI_COLOR_TEXT_PRIMARY);
    sm_text(t, g_sm.text_f, tx, ty + 50, sl_tr("power.apps_close"), SL_UI_COLOR_TEXT_SECONDARY);
    if (g_sm.live) {
        sm_text(t, g_sm.text_f, tx, ty + 76, sl_tr("power.live_lost"), SL_UI_COLOR_TEXT_SECONDARY);
    }
    sm_button(ctx, t, 0, sl_tr("common.cancel"), 0);
    sm_button(ctx, t, 1, sl_tr(off ? "menu.shutdown" : "menu.restart"), 1);
}

static void sm_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    sl_clip_set(ctx, x, y, w, h);
    sl_target_t t = sl_target_from_ctx(ctx);
    if (g_sm.dialog) {
        const sl_ui_color_t *night = sl_ui_runtime_color(SL_UI_COLOR_ACCENT);
        sl_fill_rect_a(ctx, x, y, w, h, night->r, night->g, night->b, SM_VEIL);
        sm_draw_dialog(ctx, &t);
    } else if (g_sm.open) {
        sm_draw_menu(ctx, &t);
    }
    sl_clip_clear(ctx);
}

void sl_sysmenu_render(sl_ctx_t *ctx) {
    if (!ctx || (!g_sm.open && !g_sm.dialog) || !sm_fonts(ctx)) return;
    sm_learn();
    if (g_sm.dialog) {
        sl_dirty_for_each_intersection(0u, 0u, ctx->fb.width, ctx->fb.height, sm_draw_rect, ctx);
        return;
    }
    uint32_t x = sm_menu_x(ctx);
    x = x > SM_SHADOW ? x - SM_SHADOW : 0u;
    sl_dirty_for_each_intersection(x, SM_TOP - SM_SHADOW, SM_W + 2u * SM_SHADOW, sm_menu_h() + 2u * SM_SHADOW,
                                   sm_draw_rect, ctx);
}
