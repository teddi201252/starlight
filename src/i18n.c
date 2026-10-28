#include "i18n.h"
#include "peos/lang.h"

#include <stdio.h>
#include <string.h>

static peos_lang_t g_cur, g_en;
static char g_code[PEOS_LANG_CODE_MAX];

static void load(peos_lang_t *lang, const char *code) {
    char path[96];
    snprintf(path, sizeof(path), PEOS_LANG_SYSTEM_DIR "/%s/" PEOS_LANG_FILE, code);
    if (peos_lang_load(lang, path) != 0) printf("starlight: no sentences in %s\n", path);
}

int sl_i18n_set(const char *code) {
    if (!peos_lang_code_valid(code)) code = PEOS_LANG_FALLBACK;
    if (g_code[0] && strcmp(code, g_code) == 0) return 0;
    snprintf(g_code, sizeof(g_code), "%s", code);
    load(&g_cur, g_code);
    if (strcmp(g_code, PEOS_LANG_FALLBACK) != 0) {
        if (!g_en.buf) load(&g_en, PEOS_LANG_FALLBACK);
    } else {
        peos_lang_free(&g_en);   // the current table is English
    }
    printf("starlight: language %s (%u sentences)\n", g_code, (unsigned)g_cur.count);
    return 1;
}

const char *sl_i18n_code(void) {
    return g_code[0] ? g_code : PEOS_LANG_FALLBACK;
}

const char *sl_tr(const char *key) {
    if (!key) return "";
    const char *t = peos_lang_get(&g_cur, key);
    if (!t) t = peos_lang_get(&g_en, key);
    return t ? t : key;
}

const char *sl_tr_n(const char *key, uint32_t n) {
    char k[128];
    snprintf(k, sizeof(k), "%s.%s", key, n == 1u ? "one" : "other");
    const char *t = peos_lang_get(&g_cur, k);
    if (!t) t = peos_lang_get(&g_en, k);
    return t ? t : sl_tr(key);
}
