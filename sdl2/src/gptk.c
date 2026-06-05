/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gptk.h"
#include "main.h"
#include "util.h"

static GptkMap g_gptk_map = {0};

static void tolower_str(char *s) {
    for (; *s; s++) {
        *s = tolower((unsigned char)*s);
    }
}

static int entry_exists(const char *key) {
    for (int i = 0; i < g_gptk_map.count; i++) {
        if (strcasecmp(g_gptk_map.entries[i].keyboard_key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

int gptk_load(const char *exe_dir) {
    g_gptk_map.count = 0;

    char gptk_path[MAX_PATH_LENGTH];
    snprintf(gptk_path, sizeof(gptk_path), "%s/zxtune.gptk", exe_dir);

    FILE *fp = fopen(gptk_path, "r");
    if (!fp) {
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) && g_gptk_map.count < GPTK_MAX_ENTRIES) {
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;

        char handheld_raw[64];
        char keyboard_raw[64];

        size_t left_len = eq - line;
        if (left_len >= sizeof(handheld_raw)) left_len = sizeof(handheld_raw) - 1;
        strncpy(handheld_raw, line, left_len);
        handheld_raw[left_len] = '\0';
        strncpy(keyboard_raw, eq + 1, sizeof(keyboard_raw) - 1);
        keyboard_raw[sizeof(keyboard_raw) - 1] = '\0';

        char *handheld = trim_whitespace(handheld_raw);
        char *keyboard = trim_whitespace(keyboard_raw);

        if (strlen(handheld) == 0 || strlen(keyboard) == 0) {
            continue;
        }

        tolower_str(handheld);
        tolower_str(keyboard);

        if (entry_exists(keyboard)) {
            continue;
        }

        strncpy(g_gptk_map.entries[g_gptk_map.count].keyboard_key, keyboard, sizeof(g_gptk_map.entries[0].keyboard_key) - 1);
        strncpy(g_gptk_map.entries[g_gptk_map.count].handheld_btn, handheld, sizeof(g_gptk_map.entries[0].handheld_btn) - 1);
        g_gptk_map.count++;
    }

    fclose(fp);
    return g_gptk_map.count;
}

void gptk_free(void) {
    g_gptk_map.count = 0;
}

const char* gptk_translate(const char *keyboard_key) {
    if (!keyboard_key || g_gptk_map.count == 0) {
        return NULL;
    }
    char key_lower[64];
    strncpy(key_lower, keyboard_key, sizeof(key_lower) - 1);
    key_lower[sizeof(key_lower) - 1] = '\0';
    tolower_str(key_lower);

    for (int i = 0; i < g_gptk_map.count; i++) {
        if (strcmp(g_gptk_map.entries[i].keyboard_key, key_lower) == 0) {
            return g_gptk_map.entries[i].handheld_btn;
        }
    }
    return NULL;
}

void gptk_translate_hint(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;

    size_t di = 0;
    size_t si = 0;
    size_t src_len = strlen(src);

    while (si < src_len && di + 1 < dest_size) {
        if (src[si] == '[') {
            const char *close = strchr(src + si + 1, ']');
            if (close && close - src < (int)src_len) {
                size_t key_start = si + 1;
                size_t key_len = close - (src + key_start);
                char key_buf[64];
                if (key_len < sizeof(key_buf) - 1) {
                    if (key_len >= sizeof(key_buf)) key_len = sizeof(key_buf) - 1;
                    strncpy(key_buf, src + key_start, key_len);
                    key_buf[key_len] = '\0';

                    const char *translated = gptk_translate(key_buf);
                    if (translated) {
                        dest[di++] = '[';
                        size_t tlen = strlen(translated);
                        if (di + (int)tlen < dest_size - 1) {
                            strcpy(dest + di, translated);
                            di += tlen;
                        }
                        dest[di++] = ']';
                        si = (close - src) + 1;
                        continue;
                    } else {
                        size_t copy_len = (close - src) + 1 - si;
                        if (di + copy_len >= dest_size - 1) {
                            copy_len = dest_size - di - 1;
                        }
                        strncpy(dest + di, src + si, copy_len);
                        di += copy_len;
                        si += copy_len;
                        continue;
                    }
                }
            }
        }

        dest[di++] = src[si++];
    }

    dest[di] = '\0';
}
