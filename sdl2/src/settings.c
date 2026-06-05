/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "lang.h"
#include "render_utils.h"
#include "gptk.h"

#define FOOTER_HEIGHT 45
#define TITLE_HEIGHT 70
#define ITEM_H 40

static void render_page_bg(AppContext *app) {
    SDL_Color bg = {30, 30, 50, 255};
    SDL_SetRenderDrawColor(app->renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(app->renderer);
}

static void render_page_title(AppContext *app, const char *title) {
    SDL_Color color = {200, 200, 255, 255};
    TextRender r = text_make(app, app->font, title, color);
    text_draw(&r, app->renderer, 20, 20);
    text_destroy(&r);
}

static void render_footer_bar(AppContext *app, const char *text) {
    SDL_Color color = {200, 200, 255, 255};
    SDL_Color bg = {FOOTER_BG_R, FOOTER_BG_G, FOOTER_BG_B, FOOTER_BG_A};
    SDL_Rect bg_rect = {0, app->window_height - FOOTER_HEIGHT, app->window_width, FOOTER_HEIGHT};
    SDL_SetRenderDrawColor(app->renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(app->renderer, &bg_rect);
    char translated[MAX_TEXT_LENGTH];
    gptk_translate_hint(translated, text, sizeof(translated));
    TextRender r = text_make(app, app->font_small, translated, color);
    text_draw(&r, app->renderer, 20, app->window_height - FOOTER_HEIGHT + 5);
    text_destroy(&r);
}

static void render_item(AppContext *app, const char *text, SDL_Rect rect, bool is_selected) {
    if (is_selected) {
        SDL_Color hl = {60, 60, 100, 255};
        SDL_SetRenderDrawColor(app->renderer, hl.r, hl.g, hl.b, hl.a);
        SDL_RenderFillRect(app->renderer, &rect);
    }
    SDL_Color color = is_selected ? (SDL_Color){255,255,255,255} : (SDL_Color){200,200,255,255};
    TextRender r = text_make(app, app->font_small, text, color);
    text_draw(&r, app->renderer, rect.x + 10, rect.y + (rect.h - r.h) / 2);
    text_destroy(&r);
}

static void render_checkmark(AppContext *app, SDL_Rect rect) {
    SDL_Color color = {100, 255, 100, 255};
    TextRender r = text_make(app, app->font_small, " [✓]", color);
    text_draw(&r, app->renderer, rect.x + rect.w - r.w - 10, rect.y + (rect.h - r.h) / 2);
    text_destroy(&r);
}

static void render_status(AppContext *app, SDL_Rect rect, const char *text, SDL_Color color) {
    TextRender r = text_make(app, app->font_small, text, color);
    text_draw(&r, app->renderer, rect.x + rect.w - r.w - 10, rect.y + (rect.h - r.h) / 2);
    text_destroy(&r);
}

static int calc_visible_count(AppContext *app) {
    int available = app->window_height - TITLE_HEIGHT - FOOTER_HEIGHT;
    int count = available / ITEM_H;
    return count < 3 ? 3 : count;
}

static void update_scroll(SettingsUI *ui, int visible_count, int total) {
    if (ui->selected_index >= ui->scroll_offset + visible_count) {
        ui->scroll_offset = ui->selected_index - visible_count + 1;
    }
    if (ui->selected_index < ui->scroll_offset) {
        ui->scroll_offset = ui->selected_index;
    }
    if (ui->scroll_offset + visible_count > total) {
        ui->scroll_offset = total - visible_count;
    }
    if (ui->scroll_offset < 0) ui->scroll_offset = 0;
}

typedef struct {
    bool handled;
    bool go_back;
    int selected;
    bool custom;
} ListEventResult;

static ListEventResult handle_list_event(SettingsUI *ui, SDL_Event *event, int max_index, bool has_custom) {
    ListEventResult r = {false, false, -1, false};
    if (event->type != SDL_KEYDOWN) return r;

    switch (event->key.keysym.sym) {
        case SDLK_UP:
            if (ui->selected_index > 0) ui->selected_index--;
            r.handled = true;
            break;
        case SDLK_DOWN:
            if (ui->selected_index < max_index) ui->selected_index++;
            r.handled = true;
            break;
        case SDLK_RETURN:
            r.handled = true;
            if (ui->selected_index == max_index) {
                r.go_back = true;
            } else {
                r.selected = ui->selected_index;
            }
            break;
        case SDLK_SPACE:
            if (has_custom && ui->selected_index < max_index) {
                r.handled = true;
                r.custom = true;
            }
            break;
        case SDLK_ESCAPE:
        case SDLK_q:
            r.handled = true;
            r.go_back = true;
            break;
        default:
            break;
    }
    return r;
}

static void go_back(SettingsUI *ui) {
    ui->current_menu = ui->previous_menu;
    ui->selected_index = 0;
    ui->scroll_offset = 0;
}

static void init_ui_from_settings(AppContext *app, SettingsUI *ui) {
    ui->loop_enabled = strstr(app->settings.zx_flags, "--loop") != NULL;
    ui->ym_enabled = strstr(app->settings.zx_flags, "--ym") != NULL;

    if (strstr(app->settings.zx_flags, "--analyzer")) {
        strncpy(ui->visual_mode, "analyzer", sizeof(ui->visual_mode) - 1);
    } else if (strstr(app->settings.zx_flags, "--quiet")) {
        strncpy(ui->visual_mode, "quiet", sizeof(ui->visual_mode) - 1);
    } else if (strstr(app->settings.zx_flags, "--silent")) {
        strncpy(ui->visual_mode, "silent", sizeof(ui->visual_mode) - 1);
    } else {
        strncpy(ui->visual_mode, "none", sizeof(ui->visual_mode) - 1);
    }

    if (strcmp(app->settings.zx_freq, "--frequency=44100") == 0) {
        strncpy(ui->freq_option, "44100", sizeof(ui->freq_option) - 1);
    } else if (strcmp(app->settings.zx_freq, "--frequency=48000") == 0) {
        strncpy(ui->freq_option, "48000", sizeof(ui->freq_option) - 1);
    } else if (strcmp(app->settings.zx_freq, "--frequency=22050") == 0) {
        strncpy(ui->freq_option, "22050", sizeof(ui->freq_option) - 1);
    } else {
        strncpy(ui->freq_option, "default", sizeof(ui->freq_option) - 1);
    }

    if (strcmp(app->settings.zx_backend, "--alsa") == 0) {
        strncpy(ui->backend_option, "alsa", sizeof(ui->backend_option) - 1);
    } else if (strcmp(app->settings.zx_backend, "--oss") == 0) {
        strncpy(ui->backend_option, "oss", sizeof(ui->backend_option) - 1);
    } else if (strcmp(app->settings.zx_backend, "--paudio") == 0) {
        strncpy(ui->backend_option, "paudio", sizeof(ui->backend_option) - 1);
    } else if (strcmp(app->settings.zx_backend, "--null") == 0) {
        strncpy(ui->backend_option, "null", sizeof(ui->backend_option) - 1);
    } else {
        strncpy(ui->backend_option, "default", sizeof(ui->backend_option) - 1);
    }

    if (strncmp(app->settings.zx_fps, "--updatefps=", 12) == 0) {
        ui->fps_value = atoi(app->settings.zx_fps + 12);
    } else {
        ui->fps_value = 0;
    }

    if (strncmp(app->settings.zx_seekstep, "--seekstep=", 11) == 0) {
        ui->seekstep_value = atoi(app->settings.zx_seekstep + 11);
    } else {
        ui->seekstep_value = 0;
    }

    if (strncmp(app->settings.zx_spectrum_size, "--spectrum-size=", 16) == 0) {
        ui->spectrum_size_value = atoi(app->settings.zx_spectrum_size + 16);
    } else {
        ui->spectrum_size_value = 0;
    }

    ui->debug_mode = strcmp(app->settings.zx_debug_mode, "on") == 0;
    ui->binary_mode = strcmp(app->settings.zx_output_format, "binary") == 0;
}

void mark_settings_changed(AppContext *app) {
    app->settings_changed = true;
}

void save_settings_if_changed(AppContext *app) {
    if (app->settings_changed) {
        settings_save(app);
        app->settings_changed = false;
    }
}

void apply_settings_from_ui(AppContext *app, SettingsUI *ui) {
    char new_flags[MAX_TEXT_LENGTH] = "";

    if (strlen(ui->visual_mode) > 0 && strcmp(ui->visual_mode, "none") != 0) {
        snprintf(new_flags, sizeof(new_flags), "--%s", ui->visual_mode);
    }

    if (ui->loop_enabled) {
        if (strlen(new_flags) > 0) strcat(new_flags, " ");
        strcat(new_flags, "--loop");
    }

    if (ui->ym_enabled) {
        if (strlen(new_flags) > 0) strcat(new_flags, " ");
        strcat(new_flags, "--ym");
    }

    strncpy(app->settings.zx_flags, new_flags, sizeof(app->settings.zx_flags) - 1);

    if (strcmp(ui->freq_option, "default") == 0) {
        app->settings.zx_freq[0] = '\0';
    } else {
        snprintf(app->settings.zx_freq, sizeof(app->settings.zx_freq), "--frequency=%s", ui->freq_option);
    }

    if (strcmp(ui->backend_option, "default") == 0) {
        app->settings.zx_backend[0] = '\0';
    } else {
        snprintf(app->settings.zx_backend, sizeof(app->settings.zx_backend), "--%s", ui->backend_option);
    }

    if (ui->fps_value == 0) {
        app->settings.zx_fps[0] = '\0';
    } else {
        snprintf(app->settings.zx_fps, sizeof(app->settings.zx_fps), "--updatefps=%d", ui->fps_value);
    }

    if (ui->seekstep_value == 0) {
        app->settings.zx_seekstep[0] = '\0';
    } else {
        snprintf(app->settings.zx_seekstep, sizeof(app->settings.zx_seekstep), "--seekstep=%d", ui->seekstep_value);
    }

    if (ui->spectrum_size_value == 0) {
        app->settings.zx_spectrum_size[0] = '\0';
    } else {
        snprintf(app->settings.zx_spectrum_size, sizeof(app->settings.zx_spectrum_size),
                 "--spectrum-size=%d", ui->spectrum_size_value);
    }

    strncpy(app->settings.zx_debug_mode, ui->debug_mode ? "on" : "off", sizeof(app->settings.zx_debug_mode) - 1);

    app->settings.loop = ui->loop_enabled;

    strncpy(app->settings.zx_output_format, ui->binary_mode ? "binary" : "json", sizeof(app->settings.zx_output_format) - 1);

    mark_settings_changed(app);
}

SettingsUI* settings_ui_create(void) {
    SettingsUI *ui = (SettingsUI *)malloc(sizeof(SettingsUI));
    if (!ui) return NULL;

    memset(ui, 0, sizeof(SettingsUI));
    ui->current_menu = SETTINGS_MAIN;
    ui->previous_menu = SETTINGS_MAIN;
    ui->selected_index = 0;
    ui->scroll_offset = 0;
    ui->loop_enabled = false;
    ui->ym_enabled = false;
    ui->fps_value = 0;
    ui->seekstep_value = 0;
    ui->debug_mode = false;
    ui->binary_mode = false;
    ui->languages = NULL;
    ui->language_count = 0;
    ui->info_text = NULL;
    ui->info_text_lines = 0;
    ui->version_cache[0] = '\0';
    ui->version_cached = false;
    strncpy(ui->freq_option, "default", sizeof(ui->freq_option) - 1);
    strncpy(ui->visual_mode, "analyzer", sizeof(ui->visual_mode) - 1);
    strncpy(ui->backend_option, "alsa", sizeof(ui->backend_option) - 1);
    return ui;
}

void settings_ui_destroy(SettingsUI *ui) {
    if (ui) {
        if (ui->languages) {
            lang_free_list(ui->languages, ui->language_count);
        }
        free(ui->info_text);
        free(ui);
    }
}

static void render_select_menu(AppContext *app, SettingsUI *ui,
    const char *title,
    const char **labels, int option_count,
    const char **str_values, const int *int_values,
    const char *current_str, int current_int,
    bool is_int_type, bool show_current, const char *suffix,
    bool has_custom) {
    render_page_bg(app);
    render_page_title(app, title);

    if (show_current) {
        char val_text[64];
        if (is_int_type) {
            if (current_int == 0) {
                snprintf(val_text, sizeof(val_text), "Default");
            } else {
                snprintf(val_text, sizeof(val_text), "%d%s", current_int, suffix ? suffix : "");
            }
        } else {
            snprintf(val_text, sizeof(val_text), "%s", current_str);
        }
        SDL_Color hl = {100, 255, 100, 255};
        TextRender r = text_make(app, app->font, val_text, hl);
        text_draw(&r, app->renderer, app->window_width - r.w - 20, 20);
        text_destroy(&r);
    }

    int total = option_count + 1;
    int visible_count = calc_visible_count(app);
    update_scroll(ui, visible_count, total);

    for (int vi = 0; vi < visible_count; vi++) {
        int i = vi + ui->scroll_offset;
        if (i >= total) break;
        SDL_Rect rect = {20, TITLE_HEIGHT + vi * ITEM_H, app->window_width - 40, ITEM_H - 5};

        if (i < option_count) {
            render_item(app, labels[i], rect, i == ui->selected_index);
            bool is_checked = false;
            if (is_int_type && int_values) {
                is_checked = (current_int == int_values[i]);
            } else if (!is_int_type && str_values) {
                is_checked = (strcmp(current_str, str_values[i]) == 0);
            }
            if (is_checked) {
                render_checkmark(app, rect);
            }
        } else {
            render_item(app, lang_get(app, LANG_BACK), rect, i == ui->selected_index);
        }
    }

    char footer[MAX_TEXT_LENGTH];
    if (has_custom) {
        snprintf(footer, sizeof(footer), "[↑↓] %s    [Enter] %s    [Space] %s    [Esc] %s",
                 lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_SELECT),
                 lang_get(app, LANG_CUSTOM), lang_get(app, LANG_BACK));
    } else {
        snprintf(footer, sizeof(footer), "[↑↓] %s    [Enter] %s    [Esc] %s",
                 lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_SELECT),
                 lang_get(app, LANG_BACK));
    }
    render_footer_bar(app, footer);
}

static void render_toggle_menu(AppContext *app, SettingsUI *ui,
    const char *title,
    const char **labels, bool **states, int toggle_count) {
    render_page_bg(app);
    render_page_title(app, title);

    int total = toggle_count + 1;
    int visible_count = calc_visible_count(app);
    update_scroll(ui, visible_count, total);

    for (int vi = 0; vi < visible_count; vi++) {
        int i = vi + ui->scroll_offset;
        if (i >= total) break;
        SDL_Rect rect = {20, TITLE_HEIGHT + vi * ITEM_H, app->window_width - 40, ITEM_H - 5};

        if (i < toggle_count) {
            render_item(app, labels[i], rect, i == ui->selected_index);
            bool is_on = *states[i];
            const char *status_text = is_on ? lang_get(app, LANG_ON) : lang_get(app, LANG_OFF);
            char status_display[MAX_TEXT_LENGTH];
            snprintf(status_display, sizeof(status_display), " [%s]", status_text);
            SDL_Color status_color = is_on ? (SDL_Color){100,255,100,255} : (SDL_Color){200,100,100,255};
            render_status(app, rect, status_display, status_color);
        } else {
            render_item(app, lang_get(app, LANG_BACK), rect, i == ui->selected_index);
        }
    }

    char footer[MAX_TEXT_LENGTH];
    snprintf(footer, sizeof(footer), "[↑↓] %s    [Enter] %s    [Esc] %s",
             lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_TOGGLE), lang_get(app, LANG_BACK));
    render_footer_bar(app, footer);
}

static void render_static_page(AppContext *app, const char *title,
    const char **lines, int line_count, const char *footer_text) {
    render_page_bg(app);
    render_page_title(app, title);

    int start_y = 100;
    int line_h = 40;
    for (int i = 0; i < line_count; i++) {
        TTF_Font *font = (i == 0) ? app->font : app->font_small;
        SDL_Color color = (i < 2) ? (SDL_Color){255,255,255,255} : (SDL_Color){200,220,255,255};
        TextRender r = text_make(app, font, lines[i], color);
        text_draw_center(&r, app->renderer, app->window_width / 2, start_y + i * line_h);
        text_destroy(&r);
    }

    render_footer_bar(app, footer_text);
}

static void render_message(AppContext *app, SettingsUI *ui) {
    render_page_bg(app);

    SDL_Color color = {255, 255, 255, 255};
    TextRender r = text_make(app, app->font, ui->message, color);
    text_draw_center(&r, app->renderer, app->window_width / 2, app->window_height / 2);
    text_destroy(&r);

    char footer[MAX_TEXT_LENGTH];
    snprintf(footer, sizeof(footer), "[Enter] %s", lang_get(app, LANG_OK));
    render_footer_bar(app, footer);
}

static void fetch_zxtune_info(AppContext *app, SettingsUI *ui, const char *flag) {
    free(ui->info_text);
    ui->info_text = NULL;
    ui->info_text_lines = 0;

    char cmd[MAX_PATH_LENGTH * 2];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", app->player_path, flag);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        ui->info_text = strdup("Failed to execute zxtune123");
        ui->info_text_lines = 1;
        return;
    }

    size_t capacity = 4096;
    size_t used = 0;
    char *buf = (char *)malloc(capacity);
    if (!buf) {
        pclose(fp);
        ui->info_text = strdup("Out of memory");
        ui->info_text_lines = 1;
        return;
    }
    buf[0] = '\0';

    char line[1024];
    int line_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t line_len = strlen(line);
        while (line_len > 0 && (line[line_len-1] == '\n' || line[line_len-1] == '\r')) {
            line[--line_len] = '\0';
        }
        if (used + line_len + 2 >= capacity) {
            capacity *= 2;
            char *new_buf = (char *)realloc(buf, capacity);
            if (!new_buf) break;
            buf = new_buf;
        }
        if (used > 0) {
            buf[used++] = '\n';
        }
        memcpy(buf + used, line, line_len);
        used += line_len;
        buf[used] = '\0';
        line_count++;
    }
    pclose(fp);

    if (used == 0) {
        free(buf);
        ui->info_text = strdup("(no output)");
        ui->info_text_lines = 1;
    } else {
        ui->info_text = buf;
        ui->info_text_lines = line_count;
    }
}

static void render_info_menu(AppContext *app, SettingsUI *ui) {
    render_page_bg(app);
    render_page_title(app, lang_get(app, LANG_VIEW_INFO));

    const char *labels[] = {
        lang_get(app, LANG_VIEW_PLUGINS),
        lang_get(app, LANG_VIEW_BACKENDS),
        lang_get(app, LANG_VIEW_PROVIDERS),
        lang_get(app, LANG_VIEW_FREQ_TABLES)
    };
    int option_count = 4;
    int total = option_count + 1;
    int visible_count = calc_visible_count(app);
    update_scroll(ui, visible_count, total);

    for (int vi = 0; vi < visible_count; vi++) {
        int i = vi + ui->scroll_offset;
        if (i >= total) break;
        SDL_Rect rect = {20, TITLE_HEIGHT + vi * ITEM_H, app->window_width - 40, ITEM_H - 5};
        if (i < option_count) {
            render_item(app, labels[i], rect, i == ui->selected_index);
        } else {
            render_item(app, lang_get(app, LANG_BACK), rect, i == ui->selected_index);
        }
    }

    char footer[MAX_TEXT_LENGTH];
    snprintf(footer, sizeof(footer), "[↑↓] %s    [Enter] %s    [Esc] %s",
             lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_SELECT), lang_get(app, LANG_BACK));
    render_footer_bar(app, footer);
}

static void render_info_display(AppContext *app, SettingsUI *ui) {
    render_page_bg(app);
    render_page_title(app, lang_get(app, LANG_VIEW_INFO));

    if (!ui->info_text) {
        SDL_Color color = {200, 200, 255, 255};
        TextRender r = text_make(app, app->font_small, "Loading...", color);
        text_draw_center(&r, app->renderer, app->window_width / 2, app->window_height / 2);
        text_destroy(&r);
        return;
    }

    int line_h = 24;
    int start_y = TITLE_HEIGHT;
    int available_h = app->window_height - TITLE_HEIGHT - FOOTER_HEIGHT;
    int visible_lines = available_h / line_h;
    if (visible_lines < 1) visible_lines = 1;
    int max_text_width = app->window_width - 40;

    WrappedLine *wrapped = NULL;
    int wrapped_count = wrap_text(app->font_small, ui->info_text, max_text_width, &wrapped);

    if (ui->scroll_offset + visible_lines > wrapped_count) {
        ui->scroll_offset = wrapped_count - visible_lines;
    }
    if (ui->scroll_offset < 0) ui->scroll_offset = 0;

    SDL_Color color = {200, 220, 255, 255};
    int drawn = 0;
    for (int i = ui->scroll_offset; i < wrapped_count && drawn < visible_lines; i++) {
        char line_buf[1024];
        size_t len = wrapped[i].len;
        if (len >= sizeof(line_buf)) len = sizeof(line_buf) - 1;
        memcpy(line_buf, wrapped[i].start, len);
        line_buf[len] = '\0';
        TextRender r = text_make(app, app->font_small, line_buf, color);
        text_draw(&r, app->renderer, 20, start_y + drawn * line_h);
        text_destroy(&r);
        drawn++;
    }

    free(wrapped);

    char footer[MAX_TEXT_LENGTH];
    snprintf(footer, sizeof(footer), "[↑↓] %s    [Esc] %s",
             lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_BACK));
    render_footer_bar(app, footer);
}

static void render_language(AppContext *app, SettingsUI *ui) {
    if (!ui->languages) {
        ui->language_count = lang_scan(app, &ui->languages);
    }

    render_page_bg(app);
    render_page_title(app, lang_get(app, LANG_LANGUAGE));

    int total = ui->language_count + 1;
    int visible_count = calc_visible_count(app);
    update_scroll(ui, visible_count, total);

    for (int vi = 0; vi < visible_count; vi++) {
        int i = vi + ui->scroll_offset;
        if (i >= total) break;
        SDL_Rect rect = {20, TITLE_HEIGHT + vi * ITEM_H, app->window_width - 40, ITEM_H - 5};

        if (i < ui->language_count) {
            render_item(app, ui->languages[i].name, rect, i == ui->selected_index);
            if (strcmp(app->settings.language, ui->languages[i].code) == 0) {
                render_checkmark(app, rect);
            }
        } else {
            render_item(app, lang_get(app, LANG_BACK), rect, i == ui->selected_index);
        }
    }

    char footer[MAX_TEXT_LENGTH];
    snprintf(footer, sizeof(footer), "[↑↓] %s    [Enter] %s    [Esc] %s",
             lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_SELECT), lang_get(app, LANG_BACK));
    render_footer_bar(app, footer);
}

static void render_settings_main(AppContext *app, SettingsUI *ui) {
    render_page_bg(app);

    const char *menu_items[] = {
        lang_get(app, LANG_CORE_PLAYBACK),
        lang_get(app, LANG_AUDIO_SAMPLE_RATE),
        lang_get(app, LANG_VISUAL_DISPLAY),
        lang_get(app, LANG_UI_REFRESH_RATE),
        lang_get(app, LANG_SEEK_STEP),
        lang_get(app, LANG_SPECTRUM_SIZE),
        lang_get(app, LANG_AUDIO_BACKEND),
        lang_get(app, LANG_DEBUG_MODE),
        lang_get(app, LANG_LANGUAGE),
        lang_get(app, LANG_VIEW_INFO),
        lang_get(app, LANG_ABOUT),
        lang_get(app, LANG_BACK)
    };
    int item_count = 12;

    render_page_title(app, lang_get(app, LANG_SETTINGS_MENU));

    int visible_count = calc_visible_count(app);
    update_scroll(ui, visible_count, item_count);

    for (int vi = 0; vi < visible_count; vi++) {
        int idx = vi + ui->scroll_offset;
        if (idx >= item_count) break;
        SDL_Rect rect = {20, TITLE_HEIGHT + vi * ITEM_H, app->window_width - 40, ITEM_H - 5};
        render_item(app, menu_items[idx], rect, idx == ui->selected_index);
    }

    char footer[MAX_TEXT_LENGTH];
    snprintf(footer, sizeof(footer), "[↑↓] %s    [Enter] %s    [Esc] %s",
             lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_SELECT), lang_get(app, LANG_BACK));
    render_footer_bar(app, footer);
}

void settings_ui_render(AppContext *app, SettingsUI *ui) {
    switch (ui->current_menu) {
        case SETTINGS_MAIN:
            render_settings_main(app, ui);
            break;
        case SETTINGS_PLAYBACK: {
            const char *labels[] = {lang_get(app, LANG_LOOP_PLAYBACK), lang_get(app, LANG_FORCE_YM_CHIP)};
            bool *states[] = {&ui->loop_enabled, &ui->ym_enabled};
            render_toggle_menu(app, ui, lang_get(app, LANG_CORE_PLAYBACK), labels, states, 2);
            break;
        }
        case SETTINGS_FREQUENCY: {
            const char *labels[] = {lang_get(app, LANG_DEFAULT), lang_get(app, LANG_48000),
                lang_get(app, LANG_44100), lang_get(app, LANG_22050)};
            const char *values[] = {"default", "48000", "44100", "22050"};
            render_select_menu(app, ui, lang_get(app, LANG_AUDIO_SAMPLE_RATE),
                               labels, 4, values, NULL, ui->freq_option, 0,
                               false, false, NULL, false);
            break;
        }
        case SETTINGS_VISUAL: {
            const char *labels[] = {lang_get(app, LANG_DEFAULT), lang_get(app, LANG_ENABLE_ANALYZER),
                                    lang_get(app, LANG_QUIET_MODE), lang_get(app, LANG_COMPLETELY_SILENT)};
            const char *values[] = {"none", "analyzer", "quiet", "silent"};
            render_select_menu(app, ui, lang_get(app, LANG_VISUAL_DISPLAY),
                               labels, 4, values, NULL, ui->visual_mode, 0,
                               false, false, NULL, false);
            break;
        }
        case SETTINGS_FPS: {
            const char *labels[] = {lang_get(app, LANG_DEFAULT), "15 FPS",
                                    "30 FPS", "50 FPS",
                                    "60 FPS"};
            const int values[] = {0, 15, 30, 50, 60};
            render_select_menu(app, ui, lang_get(app, LANG_UI_REFRESH_RATE),
                               labels, 5, NULL, values, NULL, ui->fps_value,
                               true, true, " FPS", true);
            break;
        }
        case SETTINGS_SEEKSTEP: {
            const char *labels[] = {lang_get(app, LANG_DEFAULT), "5%",
                                    "10%", "20%",
                                    "30%", "50%"};
            const int values[] = {0, 5, 10, 20, 30, 50};
            render_select_menu(app, ui, lang_get(app, LANG_SEEK_STEP),
                               labels, 6, NULL, values, NULL, ui->seekstep_value,
                               true, true, "%", true);
            break;
        }
        case SETTINGS_SPECTRUM: {
            const char *labels[] = {lang_get(app, LANG_DEFAULT), "16", "32", "64", "96", "128", "256"};
            const int values[] = {0, 16, 32, 64, 96, 128, 256};
            render_select_menu(app, ui, lang_get(app, LANG_SPECTRUM_SIZE),
                               labels, 7, NULL, values, NULL, ui->spectrum_size_value,
                               true, true, "", true);
            break;
        }
        case SETTINGS_BACKEND: {
            const char *labels[] = {lang_get(app, LANG_DEFAULT), lang_get(app, LANG_ALSA),
                                    lang_get(app, LANG_OSS), lang_get(app, LANG_PULSEAUDIO),
                                    lang_get(app, LANG_NULL_TEST)};
            const char *values[] = {"default", "alsa", "oss", "paudio", "null"};
            render_select_menu(app, ui, lang_get(app, LANG_AUDIO_BACKEND),
                               labels, 5, values, NULL, ui->backend_option, 0,
                               false, false, NULL, false);
            break;
        }
        case SETTINGS_DEBUG: {
            const char *labels[] = {lang_get(app, LANG_SHOW_ERROR_INFO), lang_get(app, LANG_BINARY_COMM)};
            bool *states[] = {&ui->debug_mode, &ui->binary_mode};
            render_toggle_menu(app, ui, lang_get(app, LANG_DEBUG_MODE), labels, states, 2);
            break;
        }
        case SETTINGS_LANGUAGE:
            render_language(app, ui);
            break;
        case SETTINGS_INFO:
            ui->previous_menu = SETTINGS_MAIN;
            ui->current_menu = SETTINGS_INFO_MENU;
            ui->selected_index = 0;
            ui->scroll_offset = 0;
            break;
        case SETTINGS_INFO_MENU:
            render_info_menu(app, ui);
            break;
        case SETTINGS_INFO_DISPLAY:
            render_info_display(app, ui);
            break;
        case SETTINGS_ABOUT: {
            if (!ui->version_cached) {
                char cmd[MAX_PATH_LENGTH * 2];
                snprintf(cmd, sizeof(cmd), "%s --version 2>&1", app->player_path);
                FILE *fp = popen(cmd, "r");
                if (fp) {
                    if (fgets(ui->version_cache, sizeof(ui->version_cache), fp)) {
                        size_t l = strlen(ui->version_cache);
                        while (l > 0 && (ui->version_cache[l-1] == '\n' || ui->version_cache[l-1] == '\r')) {
                            ui->version_cache[--l] = '\0';
                        }
                    }
                    pclose(fp);
                }
                ui->version_cached = true;
            }
            const char *lines[] = {
                "ZXTune SDL2 Player", "Version: Alpha 0.1", "Frontend Author: BCTaoTao",
                ui->version_cache[0] ? ui->version_cache : "zxtune123",
                "",
                lang_get(app, LANG_ENJOY_MUSIC)
            };
            char footer[MAX_TEXT_LENGTH];
            snprintf(footer, sizeof(footer), "[Esc] %s", lang_get(app, LANG_BACK));
            render_static_page(app, lang_get(app, LANG_ABOUT), lines, 6, footer);

            SDL_Color license_color = {140, 160, 200, 255};
            int license_y = app->window_height - FOOTER_HEIGHT - 10;
            const char *credits[] = {
                "Application License: LGPLv3",
                "Powered by SDL2 & SDL2_ttf by Sam Lantinga",
                "Backend: ZXTune123 by vitamin-caig"
            };
            for (int i = 2; i >= 0; i--) {
                TextRender r = text_make(app, app->font_small, credits[i], license_color);
                text_draw(&r, app->renderer, app->window_width - r.w - 20, license_y - r.h);
                license_y -= r.h + 4;
                text_destroy(&r);
            }
            break;
        }
        case SETTINGS_MESSAGE:
            render_message(app, ui);
            break;
        default:
            snprintf(ui->message, sizeof(ui->message), "This feature is coming soon!");
            render_message(app, ui);
            break;
    }
}

void settings_ui_handle_event(AppContext *app, SettingsUI *ui, SDL_Event *event) {
    if (event->type != SDL_KEYDOWN) return;

    switch (ui->current_menu) {
        case SETTINGS_MAIN: {
            int item_count = 12;
            ListEventResult r = handle_list_event(ui, event, item_count - 1, false);
            if (!r.handled) break;
            if (r.go_back) {
                save_settings_if_changed(app);
                app->current_screen = SCREEN_FILE_BROWSER;
                ui->selected_index = 0;
                ui->scroll_offset = 0;
            } else if (r.selected >= 0) {
                init_ui_from_settings(app, ui);
                ui->previous_menu = SETTINGS_MAIN;
                ui->selected_index = 0;
                ui->scroll_offset = 0;
                static const SettingsMenu targets[] = {
                    SETTINGS_PLAYBACK, SETTINGS_FREQUENCY, SETTINGS_VISUAL,
                    SETTINGS_FPS, SETTINGS_SEEKSTEP, SETTINGS_SPECTRUM, SETTINGS_BACKEND,
                    SETTINGS_DEBUG, SETTINGS_LANGUAGE, SETTINGS_INFO,
                    SETTINGS_ABOUT
                };
                if (r.selected < 11) {
                    ui->current_menu = targets[r.selected];
                } else {
                    save_settings_if_changed(app);
                    app->current_screen = SCREEN_FILE_BROWSER;
                }
            }
            break;
        }
        case SETTINGS_PLAYBACK: {
            ListEventResult r = handle_list_event(ui, event, 2, false);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            if (r.selected == 0) { ui->loop_enabled = !ui->loop_enabled; apply_settings_from_ui(app, ui); mark_settings_changed(app); }
            else if (r.selected == 1) { ui->ym_enabled = !ui->ym_enabled; apply_settings_from_ui(app, ui); mark_settings_changed(app); }
            break;
        }
        case SETTINGS_FREQUENCY: {
            ListEventResult r = handle_list_event(ui, event, 4, false);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            const char *values[] = {"default", "48000", "44100", "22050"};
            if (r.selected >= 0 && r.selected < 4) {
                strncpy(ui->freq_option, values[r.selected], sizeof(ui->freq_option) - 1);
                apply_settings_from_ui(app, ui);
            }
            break;
        }
        case SETTINGS_VISUAL: {
            ListEventResult r = handle_list_event(ui, event, 4, false);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            const char *values[] = {"none", "analyzer", "quiet", "silent"};
            if (r.selected >= 0 && r.selected < 4) {
                strncpy(ui->visual_mode, values[r.selected], sizeof(ui->visual_mode) - 1);
                apply_settings_from_ui(app, ui);
            }
            break;
        }
        case SETTINGS_FPS: {
            ListEventResult r = handle_list_event(ui, event, 5, true);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            if (r.custom) {
                app->temp_fps_value = ui->fps_value > 0 ? ui->fps_value : 10;
                app->current_dialog = DIALOG_FPS_CUSTOM;
                app->dialog_selected = 0;
                break;
            }
            const int values[] = {0, 15, 30, 50, 60};
            if (r.selected >= 0 && r.selected < 5) {
                ui->fps_value = values[r.selected];
                apply_settings_from_ui(app, ui);
            }
            break;
        }
        case SETTINGS_SEEKSTEP: {
            ListEventResult r = handle_list_event(ui, event, 6, true);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            if (r.custom) {
                app->temp_seekstep_value = ui->seekstep_value > 0 ? ui->seekstep_value : 10;
                app->current_dialog = DIALOG_SEEKSTEP_CUSTOM;
                app->dialog_selected = 0;
                break;
            }
            const int values[] = {0, 5, 10, 20, 30, 50};
            if (r.selected >= 0 && r.selected < 6) {
                ui->seekstep_value = values[r.selected];
                apply_settings_from_ui(app, ui);
            }
            break;
        }
        case SETTINGS_SPECTRUM: {
            ListEventResult r = handle_list_event(ui, event, 7, true);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            if (r.custom) {
                app->temp_spectrum_size_value = ui->spectrum_size_value > 0 ? ui->spectrum_size_value : 64;
                app->current_dialog = DIALOG_SPECTRUM_CUSTOM;
                app->dialog_selected = 0;
                break;
            }
            const int values[] = {0, 16, 32, 64, 96, 128, 256};
            if (r.selected >= 0 && r.selected < 7) {
                ui->spectrum_size_value = values[r.selected];
                apply_settings_from_ui(app, ui);
            }
            break;
        }
        case SETTINGS_BACKEND: {
            ListEventResult r = handle_list_event(ui, event, 5, false);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            const char *values[] = {"default", "alsa", "oss", "paudio", "null"};
            if (r.selected >= 0 && r.selected < 5) {
                strncpy(ui->backend_option, values[r.selected], sizeof(ui->backend_option) - 1);
                apply_settings_from_ui(app, ui);
            }
            break;
        }
        case SETTINGS_DEBUG: {
            ListEventResult r = handle_list_event(ui, event, 2, false);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            if (r.selected == 0) { ui->debug_mode = !ui->debug_mode; apply_settings_from_ui(app, ui); }
            else if (r.selected == 1) { ui->binary_mode = !ui->binary_mode; apply_settings_from_ui(app, ui); }
            break;
        }
        case SETTINGS_LANGUAGE: {
            if (!ui->languages) {
                ui->language_count = lang_scan(app, &ui->languages);
            }
            ListEventResult r = handle_list_event(ui, event, ui->language_count, false);
            if (!r.handled) break;
            if (r.go_back) { save_settings_if_changed(app); go_back(ui); break; }
            if (r.selected >= 0 && r.selected < ui->language_count) {
                const char *lang_code = ui->languages[r.selected].code;
                strncpy(app->settings.language, lang_code, sizeof(app->settings.language) - 1);
                strncpy(app->current_language, lang_code, sizeof(app->current_language) - 1);
                lang_load(app, lang_code);
                mark_settings_changed(app);
                save_settings_if_changed(app);
            }
            break;
        }
        case SETTINGS_ABOUT:
        case SETTINGS_MESSAGE:
            if (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_ESCAPE) {
                go_back(ui);
            }
            break;
        case SETTINGS_INFO_MENU: {
            ListEventResult r = handle_list_event(ui, event, 4, false);
            if (!r.handled) break;
            if (r.go_back) {
                go_back(ui);
                break;
            }
            static const char *flags[] = {"--list-plugins", "--list-backends", "--list-providers", "--list-freqtables"};
            if (r.selected >= 0 && r.selected < 4) {
                fetch_zxtune_info(app, ui, flags[r.selected]);
                ui->previous_menu = SETTINGS_INFO_MENU;
                ui->current_menu = SETTINGS_INFO_DISPLAY;
                ui->selected_index = 0;
                ui->scroll_offset = 0;
            }
            break;
        }
        case SETTINGS_INFO_DISPLAY:
            if (event->key.keysym.sym == SDLK_ESCAPE || event->key.keysym.sym == SDLK_RETURN) {
                ui->previous_menu = SETTINGS_MAIN;
                ui->current_menu = SETTINGS_INFO_MENU;
                ui->selected_index = 0;
                ui->scroll_offset = 0;
            } else if (event->key.keysym.sym == SDLK_UP) {
                if (ui->scroll_offset > 0) ui->scroll_offset--;
            } else if (event->key.keysym.sym == SDLK_DOWN) {
                if (ui->scroll_offset < ui->info_text_lines - 1) ui->scroll_offset++;
            } else if (event->key.keysym.sym == SDLK_PAGEUP) {
                ui->scroll_offset -= 15;
                if (ui->scroll_offset < 0) ui->scroll_offset = 0;
            } else if (event->key.keysym.sym == SDLK_PAGEDOWN) {
                ui->scroll_offset += 15;
                int max_scroll = ui->info_text_lines - 5;
                if (max_scroll < 0) max_scroll = 0;
                if (ui->scroll_offset > max_scroll) ui->scroll_offset = max_scroll;
            }
            break;
        default:
            break;
    }
}

bool settings_load(AppContext *app) {
    FILE *file = fopen(app->state_path, "r");
    if (!file) {
        snprintf(app->settings.dir, sizeof(app->settings.dir), "%s", app->music_dir);
        app->settings.loop = false;
        strncpy(app->settings.zx_flags, "--analyzer", sizeof(app->settings.zx_flags) - 1);
        app->settings.zx_flags[sizeof(app->settings.zx_flags) - 1] = '\0';
        strncpy(app->settings.zx_backend, "--alsa", sizeof(app->settings.zx_backend) - 1);
        app->settings.zx_backend[sizeof(app->settings.zx_backend) - 1] = '\0';
        app->settings.zx_freq[0] = '\0';
        strncpy(app->settings.zx_fps, "--updatefps=30", sizeof(app->settings.zx_fps) - 1);
        app->settings.zx_fps[sizeof(app->settings.zx_fps) - 1] = '\0';
        strncpy(app->settings.zx_debug_mode, "off", sizeof(app->settings.zx_debug_mode) - 1);
        app->settings.zx_debug_mode[sizeof(app->settings.zx_debug_mode) - 1] = '\0';
        strncpy(app->settings.zx_output_format, "json", sizeof(app->settings.zx_output_format) - 1);
        app->settings.zx_output_format[sizeof(app->settings.zx_output_format) - 1] = '\0';
        app->settings.zx_seekstep[0] = '\0';
        app->settings.zx_spectrum_size[0] = '\0';
        app->settings.window_width = WINDOW_WIDTH;
        app->settings.window_height = WINDOW_HEIGHT;
        strncpy(app->settings.language, "en", sizeof(app->settings.language) - 1);
        return false;
    }

    char line[MAX_TEXT_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *value = eq + 1;

            while (key[0] == ' ' || key[0] == '\t') key++;
            while (value[0] == ' ' || value[0] == '\t') value++;
            size_t len = strlen(value);
            while (len > 0 && (value[len-1] == '\n' || value[len-1] == '\r')) {
                value[len-1] = '\0';
                len--;
            }

            if (strcmp(key, "dir") == 0) {
                strncpy(app->settings.dir, value, sizeof(app->settings.dir) - 1);
            } else if (strcmp(key, "zx_flags") == 0) {
                strncpy(app->settings.zx_flags, value, sizeof(app->settings.zx_flags) - 1);
            } else if (strcmp(key, "zx_backend") == 0) {
                strncpy(app->settings.zx_backend, value, sizeof(app->settings.zx_backend) - 1);
            } else if (strcmp(key, "zx_freq") == 0) {
                strncpy(app->settings.zx_freq, value, sizeof(app->settings.zx_freq) - 1);
            } else if (strcmp(key, "zx_fps") == 0) {
                strncpy(app->settings.zx_fps, value, sizeof(app->settings.zx_fps) - 1);
            } else if (strcmp(key, "zx_debug_mode") == 0) {
                strncpy(app->settings.zx_debug_mode, value, sizeof(app->settings.zx_debug_mode) - 1);
            } else if (strcmp(key, "zx_output_format") == 0) {
                strncpy(app->settings.zx_output_format, value, sizeof(app->settings.zx_output_format) - 1);
            } else if (strcmp(key, "zx_seekstep") == 0) {
                strncpy(app->settings.zx_seekstep, value, sizeof(app->settings.zx_seekstep) - 1);
            } else if (strcmp(key, "zx_spectrum_size") == 0) {
                strncpy(app->settings.zx_spectrum_size, value, sizeof(app->settings.zx_spectrum_size) - 1);
            } else if (strcmp(key, "window_width") == 0) {
                app->settings.window_width = atoi(value);
            } else if (strcmp(key, "window_height") == 0) {
                app->settings.window_height = atoi(value);
            } else if (strcmp(key, "language") == 0) {
                strncpy(app->settings.language, value, sizeof(app->settings.language) - 1);
            }
        }
    }

    fclose(file);

    app->settings.loop = strstr(app->settings.zx_flags, "--loop") != NULL;

    return true;
}

bool settings_save(AppContext *app) {
    FILE *file = fopen(app->state_path, "w");
    if (!file) {
        fprintf(stderr, "Failed to save settings: %s\n", app->state_path);
        return false;
    }

    fprintf(file, "dir=%s\n", app->settings.dir);
    fprintf(file, "zx_flags=%s\n", app->settings.zx_flags);
    fprintf(file, "zx_backend=%s\n", app->settings.zx_backend);
    fprintf(file, "zx_freq=%s\n", app->settings.zx_freq);
    fprintf(file, "zx_fps=%s\n", app->settings.zx_fps);
    fprintf(file, "zx_debug_mode=%s\n", app->settings.zx_debug_mode);
    fprintf(file, "zx_output_format=%s\n", app->settings.zx_output_format);
    fprintf(file, "zx_seekstep=%s\n", app->settings.zx_seekstep);
    fprintf(file, "zx_spectrum_size=%s\n", app->settings.zx_spectrum_size);
    fprintf(file, "window_width=%d\n", app->settings.window_width);
    fprintf(file, "window_height=%d\n", app->settings.window_height);
    fprintf(file, "language=%s\n", app->settings.language);

    fclose(file);
    return true;
}
