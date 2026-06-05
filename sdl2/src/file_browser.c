/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "file_browser.h"
#include "lang.h"
#include "gptk.h"

static void normalize_path(char *path, size_t max_len) {
    if (!path || strlen(path) == 0) return;
    
    char result[MAX_PATH_LENGTH];
    char *parts[MAX_PATH_LENGTH / 2];
    int part_count = 0;
    int is_absolute = path[0] == '/';
    
    char temp[MAX_PATH_LENGTH];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *token = strtok(temp, "/");
    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
        } else if (strcmp(token, "..") == 0) {
            if (part_count > 0) {
                part_count--;
            }
        } else if (strlen(token) > 0) {
            if (part_count < (int)(sizeof(parts) / sizeof(parts[0]))) {
                parts[part_count++] = token;
            }
        }
        token = strtok(NULL, "/");
    }
    
    result[0] = '\0';
    if (is_absolute) {
        strcat(result, "/");
    }
    
    for (int i = 0; i < part_count; i++) {
        if (i > 0 || is_absolute) {
            if (strlen(result) > 1 || (is_absolute && part_count > 0)) {
                strcat(result, "/");
            }
        }
        strncat(result, parts[i], sizeof(result) - strlen(result) - 1);
    }
    
    if (strlen(result) == 0) {
        strcpy(result, ".");
    }
    
    strncpy(path, result, max_len - 1);
    path[max_len - 1] = '\0';
}

static bool is_path_within(const char *path, const char *base) {
    if (!path || !base) return false;
    
    char normalized_path[MAX_PATH_LENGTH];
    char normalized_base[MAX_PATH_LENGTH];
    strncpy(normalized_path, path, sizeof(normalized_path) - 1);
    strncpy(normalized_base, base, sizeof(normalized_base) - 1);
    normalize_path(normalized_path, sizeof(normalized_path));
    normalize_path(normalized_base, sizeof(normalized_base));
    
    size_t base_len = strlen(normalized_base);
    if (strncmp(normalized_path, normalized_base, base_len) != 0) {
        return false;
    }
    
    if (normalized_path[base_len] == '\0' || normalized_path[base_len] == '/') {
        return true;
    }
    
    return false;
}

static int compare_entries(const void *a, const void *b) {
    const FileEntry *entry1 = (const FileEntry *)a;
    const FileEntry *entry2 = (const FileEntry *)b;
    
    if (entry1->is_dir != entry2->is_dir) {
        return entry1->is_dir ? -1 : 1;
    }
    
    return strcasecmp(entry1->name, entry2->name);
}

FileBrowser* file_browser_create(void) {
    FileBrowser *fb = (FileBrowser *)malloc(sizeof(FileBrowser));
    if (!fb) return NULL;
    
    fb->count = 0;
    fb->selected_index = 0;
    fb->scroll_offset = 0;
    fb->scroll_start_time = 0;
    fb->last_selected_index = 0;
    fb->cached_scroll_tex = NULL;
    fb->cached_scroll_w = 0;
    fb->cached_scroll_h = 0;
    fb->last_scroll_offset = 0;
    memset(fb->visible_item_cache, 0, sizeof(fb->visible_item_cache));
    return fb;
}

void file_browser_destroy(FileBrowser *fb) {
    if (fb) {
        if (fb->cached_scroll_tex) {
            SDL_DestroyTexture(fb->cached_scroll_tex);
        }
        for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
            text_destroy(&fb->visible_item_cache[i]);
        }
        free(fb);
    }
}

void file_browser_scan(FileBrowser *fb, const char *dir, const char *root_dir) {
    fb->count = 0;
    
    char norm_dir[MAX_PATH_LENGTH];
    char norm_root[MAX_PATH_LENGTH];
    strncpy(norm_dir, dir, sizeof(norm_dir) - 1);
    strncpy(norm_root, root_dir, sizeof(norm_root) - 1);
    norm_dir[sizeof(norm_dir) - 1] = '\0';
    norm_root[sizeof(norm_root) - 1] = '\0';
    normalize_path(norm_dir, sizeof(norm_dir));
    normalize_path(norm_root, sizeof(norm_root));
    bool is_root = (strcmp(norm_dir, norm_root) == 0);
    
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "Failed to open directory: %s\n", dir);
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcmp(entry->d_name, "..") == 0 && is_root) continue;
        
        if (fb->count >= MAX_ENTRIES) break;
        
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
        
        struct stat statbuf;
        if (stat(full_path, &statbuf) == 0) {
            strncpy(fb->entries[fb->count].name, entry->d_name, MAX_PATH_LENGTH - 1);
            fb->entries[fb->count].name[MAX_PATH_LENGTH - 1] = '\0';
            fb->entries[fb->count].is_dir = S_ISDIR(statbuf.st_mode);
            fb->count++;
        }
    }
    
    closedir(d);
    qsort(fb->entries, fb->count, sizeof(FileEntry), compare_entries);
    
    fb->selected_index = 0;
    fb->scroll_offset = 0;
    fb->scroll_start_time = SDL_GetTicks();
    fb->last_selected_index = 0;
    fb->last_scroll_offset = 0;
    if (fb->cached_scroll_tex) {
        SDL_DestroyTexture(fb->cached_scroll_tex);
        fb->cached_scroll_tex = NULL;
        fb->cached_scroll_w = 0;
        fb->cached_scroll_h = 0;
    }
    for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
        text_destroy(&fb->visible_item_cache[i]);
    }
}

static void split_filename(const char *name, char *basename, char *ext, size_t max_len) {
    if (!name || !basename || !ext) {
        if (basename) basename[0] = '\0';
        if (ext) ext[0] = '\0';
        return;
    }
    
    const char *last_dot = NULL;
    const char *dot = name;
    
    while ((dot = strchr(dot, '.')) != NULL) {
        if (dot > name) {
            last_dot = dot;
        }
        dot++;
    }
    
    if (last_dot) {
        size_t base_len = last_dot - name;
        if (base_len > max_len - 1) base_len = max_len - 1;
        strncpy(basename, name, base_len);
        basename[base_len] = '\0';
        
        strncpy(ext, last_dot + 1, max_len - 1);
        ext[max_len - 1] = '\0';
    } else {
        strncpy(basename, name, max_len - 1);
        basename[max_len - 1] = '\0';
        ext[0] = '\0';
    }
}

static char* truncate_path_left(const char *path, TTF_Font *font, int max_width, char *out_buf, size_t out_size) {
    if (!path || !font || !out_buf || out_size == 0) return "";
    out_buf[0] = '\0';
    
    if (strlen(path) == 0) {
        strncpy(out_buf, path, out_size - 1);
        return out_buf;
    }
    
    int path_len = strlen(path);
    int ellipsis_width;
    TTF_SizeUTF8(font, "...", &ellipsis_width, NULL);
    
    int full_width;
    TTF_SizeUTF8(font, path, &full_width, NULL);
    
    if (full_width <= max_width) {
        strncpy(out_buf, path, out_size - 1);
        return out_buf;
    }
    
    int start = 0;
    for (int i = 0; i < path_len; i++) {
        if (path[i] == '/') {
            int test_width;
            TTF_SizeUTF8(font, path + i, &test_width, NULL);
            if (test_width + ellipsis_width <= max_width) {
                start = i;
            } else {
                break;
            }
        }
    }
    
    snprintf(out_buf, out_size, "...%s", path + start);
    
    int final_width;
    TTF_SizeUTF8(font, out_buf, &final_width, NULL);
    
    if (final_width > max_width) {
        for (int i = start + 1; i < path_len; i++) {
            snprintf(out_buf, out_size, "...%s", path + i);
            TTF_SizeUTF8(font, out_buf, &final_width, NULL);
            if (final_width <= max_width) {
                break;
            }
        }
    }
    
    return out_buf;
}

static char* truncate_text_right(const char *text, TTF_Font *font, int max_width, char *out_buf, size_t out_size) {
    if (!text || !font || !out_buf || out_size == 0) return "";
    out_buf[0] = '\0';
    if (strlen(text) == 0) {
        return out_buf;
    }
    
    int full_width;
    TTF_SizeUTF8(font, text, &full_width, NULL);
    
    if (full_width <= max_width) {
        strncpy(out_buf, text, out_size - 1);
        out_buf[out_size - 1] = '\0';
        return out_buf;
    }
    
    int ellipsis_width;
    TTF_SizeUTF8(font, "...", &ellipsis_width, NULL);
    int target_w = max_width - ellipsis_width;
    if (target_w <= 0) {
        snprintf(out_buf, out_size, "...");
        return out_buf;
    }
    
    int text_len = strlen(text);
    int lo = 0, hi = text_len;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        int safe_mid = mid;
        while (safe_mid > 0 && ((unsigned char)text[safe_mid] & 0xC0) == 0x80)
            safe_mid--;
        if (safe_mid <= 0) { hi = 0; break; }
        char test[512];
        int copy_len = safe_mid < (int)sizeof(test) - 1 ? safe_mid : (int)sizeof(test) - 1;
        memcpy(test, text, copy_len);
        test[copy_len] = '\0';
        int test_w;
        TTF_SizeUTF8(font, test, &test_w, NULL);
        if (test_w <= target_w) lo = mid;
        else hi = safe_mid - 1;
    }
    
    int safe_lo = lo;
    while (safe_lo > 0 && ((unsigned char)text[safe_lo] & 0xC0) == 0x80)
        safe_lo--;
    
    snprintf(out_buf, out_size, "%.*s...", safe_lo, text);
    return out_buf;
}

static int compute_scroll_offset(int max_base_w, int cached_w, uint32_t scroll_start_time) {
    int max_offset = cached_w - max_base_w;
    if (max_offset < 1) return 0;

    int pause_ms = 1000;
    int scroll_speed = 30;
    int scroll_ms = max_offset * 1000 / scroll_speed;
    if (scroll_ms < 300) scroll_ms = 300;
    uint32_t total_cycle = (uint32_t)(pause_ms * 2 + scroll_ms * 2);

    uint32_t elapsed = SDL_GetTicks() - scroll_start_time;
    uint32_t phase = elapsed % total_cycle;

    if (phase < (uint32_t)pause_ms) return 0;
    if (phase < (uint32_t)(pause_ms + scroll_ms))
        return (int)((float)(phase - pause_ms) / scroll_ms * max_offset);
    if (phase < (uint32_t)(pause_ms * 2 + scroll_ms))
        return max_offset;
    return max_offset - (int)((float)(phase - pause_ms * 2 - scroll_ms) / scroll_ms * max_offset);
}

void file_browser_render(AppContext *app, FileBrowser *fb) {
    if (fb->last_selected_index != fb->selected_index || fb->last_scroll_offset != fb->scroll_offset) {
        fb->scroll_start_time = SDL_GetTicks();
        fb->last_selected_index = fb->selected_index;
        fb->last_scroll_offset = fb->scroll_offset;

        if (fb->cached_scroll_tex) {
            SDL_DestroyTexture(fb->cached_scroll_tex);
            fb->cached_scroll_tex = NULL;
            fb->cached_scroll_w = 0;
            fb->cached_scroll_h = 0;
        }
        for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
            text_destroy(&fb->visible_item_cache[i]);
        }
    }

    SDL_Color bg_color = {30, 30, 50, 255};
    SDL_SetRenderDrawColor(app->renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_RenderClear(app->renderer);
    
    SDL_Color text_color = {200, 200, 255, 255};
    SDL_Color selected_color = {255, 255, 255, 255};
    SDL_Color dir_color = {100, 255, 100, 255};
    SDL_Color ext_color = {255, 150, 50, 255};
    
    char prefix[MAX_TEXT_LENGTH];
    snprintf(prefix, sizeof(prefix), "%s: ", lang_get(app, LANG_FILE_BROWSER));
    TextRender prefix_r = text_make(app, app->font, prefix, text_color);
    
    int max_path_width = app->window_width - 40 - prefix_r.w;
    char path_buf[MAX_TEXT_LENGTH * 2];
    char *display_path = truncate_path_left(app->settings.dir, app->font, max_path_width, path_buf, sizeof(path_buf));
    TextRender path_r = text_make(app, app->font, display_path, text_color);

    text_draw(&prefix_r, app->renderer, 20, 20);
    text_draw(&path_r, app->renderer, app->window_width - 20 - path_r.w, 20);

    text_destroy(&prefix_r);
    text_destroy(&path_r);

    if (fb->count == 0) {
        const char *empty_text = lang_get(app, LANG_EMPTY);
        TextRender empty_r = text_make(app, app->font, empty_text, text_color);
        text_draw(&empty_r, app->renderer, (app->window_width - empty_r.w) / 2,
                  (app->window_height - empty_r.h) / 2);
        text_destroy(&empty_r);
    }

    int visible_count = (app->window_height - 70 - 45) / 30;
    if (visible_count < 5) visible_count = 5;
    if (visible_count > MAX_VISIBLE_ITEMS) visible_count = MAX_VISIBLE_ITEMS;
    int start_y = 70;
    int item_height = 30;
    
    if (fb->selected_index >= fb->scroll_offset + visible_count) {
        fb->scroll_offset = fb->selected_index - visible_count + 1;
    }
    if (fb->selected_index < fb->scroll_offset) {
        fb->scroll_offset = fb->selected_index;
    }
    
    int display_idx = 0;
    for (int i = 0; i < fb->count; i++) {
        if (display_idx < fb->scroll_offset) {
            display_idx++;
            continue;
        }
        
        int visible_idx = display_idx - fb->scroll_offset;
        if (visible_idx >= visible_count) break;
        
        SDL_Rect item_rect = {20, start_y + visible_idx * item_height, app->window_width - 40, item_height - 5};
        
        if (i == fb->selected_index) {
            SDL_Color highlight = {60, 60, 100, 255};
            SDL_SetRenderDrawColor(app->renderer, highlight.r, highlight.g, highlight.b, highlight.a);
            SDL_RenderFillRect(app->renderer, &item_rect);
        }
        
        if (fb->entries[i].is_dir) {
            char *dir_tag;
            if (strcmp(fb->entries[i].name, "..") == 0) {
                dir_tag = "[<UP]";
            } else {
                dir_tag = "[DIR]";
            }

            int tag_w, tag_h;
            TTF_SizeUTF8(app->font_small, dir_tag, &tag_w, &tag_h);

            int max_name_w = item_rect.w - 10 - 10;
            max_name_w -= tag_w + 10;
            if (max_name_w < 20) max_name_w = 20;

            int full_name_w;
            TTF_SizeUTF8(app->font_small, fb->entries[i].name, &full_name_w, NULL);
            bool needs_truncation = (full_name_w > max_name_w);

            bool is_selected = (i == fb->selected_index);
            SDL_Color name_color = dir_color;

            if (is_selected && needs_truncation) {
                if (!fb->cached_scroll_tex) {
                    SDL_Surface *s = TTF_RenderUTF8_Blended(app->font_small, fb->entries[i].name, name_color);
                    if (s) {
                        fb->cached_scroll_tex = SDL_CreateTextureFromSurface(app->renderer, s);
                        SDL_QueryTexture(fb->cached_scroll_tex, NULL, NULL,
                                         &fb->cached_scroll_w, &fb->cached_scroll_h);
                        SDL_FreeSurface(s);
                    }
                }

                int offset = compute_scroll_offset(max_name_w, fb->cached_scroll_w, fb->scroll_start_time);

                SDL_Rect src_rect = {offset, 0, max_name_w, fb->cached_scroll_h};
                SDL_Rect dst_rect = {item_rect.x + 10,
                                     item_rect.y + (item_rect.h - fb->cached_scroll_h) / 2,
                                     max_name_w, fb->cached_scroll_h};
                SDL_RenderCopy(app->renderer, fb->cached_scroll_tex, &src_rect, &dst_rect);
            } else {
                if (!fb->visible_item_cache[visible_idx].tex) {
                    const char *display_name = fb->entries[i].name;
                    char trunc_buf[MAX_TEXT_LENGTH * 2];
                    if (needs_truncation) {
                        display_name = truncate_text_right(fb->entries[i].name, app->font_small, max_name_w,
                                                           trunc_buf, sizeof(trunc_buf));
                    }
                    fb->visible_item_cache[visible_idx] = text_make(app, app->font_small, display_name, name_color);
                }
                text_draw(&fb->visible_item_cache[visible_idx], app->renderer,
                          item_rect.x + 10,
                          item_rect.y + (item_rect.h - fb->visible_item_cache[visible_idx].h) / 2);
            }

            SDL_Color tag_color = dir_color;
            SDL_Surface *tag_surf = TTF_RenderUTF8_Blended(app->font_small, dir_tag, tag_color);
            SDL_Texture *tag_tex = SDL_CreateTextureFromSurface(app->renderer, tag_surf);
            int tag_x = item_rect.x + item_rect.w - tag_w - 10;
            SDL_Rect tag_rect = {tag_x, item_rect.y + (item_rect.h - tag_h) / 2, tag_w, tag_h};
            SDL_RenderCopy(app->renderer, tag_tex, NULL, &tag_rect);
            SDL_FreeSurface(tag_surf);
            SDL_DestroyTexture(tag_tex);
        } else {
            char basename[MAX_PATH_LENGTH];
            char ext[MAX_PATH_LENGTH];
            split_filename(fb->entries[i].name, basename, ext, MAX_PATH_LENGTH);

            int ext_w = 0, ext_h = 0;
            char ext_display[MAX_PATH_LENGTH + 10];
            if (strlen(ext) > 0) {
                snprintf(ext_display, sizeof(ext_display), "[%s]", ext);
                TTF_SizeUTF8(app->font_small, ext_display, &ext_w, &ext_h);
            }

            int max_base_w = item_rect.w - 10 - 10;
            if (ext_w > 0) {
                max_base_w -= ext_w + 10;
            }
            if (max_base_w < 20) max_base_w = 20;

            int full_base_w;
            TTF_SizeUTF8(app->font_small, basename, &full_base_w, NULL);
            bool needs_truncation = (full_base_w > max_base_w);

            bool is_selected = (i == fb->selected_index);
            SDL_Color base_color = is_selected ? selected_color : text_color;

            if (is_selected && needs_truncation) {
                if (!fb->cached_scroll_tex) {
                    SDL_Surface *s = TTF_RenderUTF8_Blended(app->font_small, basename, base_color);
                    if (s) {
                        fb->cached_scroll_tex = SDL_CreateTextureFromSurface(app->renderer, s);
                        SDL_QueryTexture(fb->cached_scroll_tex, NULL, NULL,
                                         &fb->cached_scroll_w, &fb->cached_scroll_h);
                        SDL_FreeSurface(s);
                    }
                }

                int offset = compute_scroll_offset(max_base_w, fb->cached_scroll_w, fb->scroll_start_time);

                SDL_Rect src_rect = {offset, 0, max_base_w, fb->cached_scroll_h};
                SDL_Rect dst_rect = {item_rect.x + 10,
                                     item_rect.y + (item_rect.h - fb->cached_scroll_h) / 2,
                                     max_base_w, fb->cached_scroll_h};
                SDL_RenderCopy(app->renderer, fb->cached_scroll_tex, &src_rect, &dst_rect);
            } else {
                if (!fb->visible_item_cache[visible_idx].tex) {
                    const char *display_name = basename;
                    char trunc_buf[MAX_TEXT_LENGTH * 2];
                    if (needs_truncation) {
                        display_name = truncate_text_right(basename, app->font_small, max_base_w,
                                                           trunc_buf, sizeof(trunc_buf));
                    }
                    fb->visible_item_cache[visible_idx] = text_make(app, app->font_small, display_name, base_color);
                }
                text_draw(&fb->visible_item_cache[visible_idx], app->renderer,
                          item_rect.x + 10,
                          item_rect.y + (item_rect.h - fb->visible_item_cache[visible_idx].h) / 2);
            }

            if (strlen(ext) > 0) {
                SDL_Color ext_col = ext_color;
                SDL_Surface *ext_surface = TTF_RenderUTF8_Blended(app->font_small, ext_display, ext_col);
                SDL_Texture *ext_texture = SDL_CreateTextureFromSurface(app->renderer, ext_surface);

                int ext_x = item_rect.x + item_rect.w - ext_w - 10;
                SDL_Rect ext_rect = {ext_x, item_rect.y + (item_rect.h - ext_h) / 2, ext_w, ext_h};
                SDL_RenderCopy(app->renderer, ext_texture, NULL, &ext_rect);

                SDL_FreeSurface(ext_surface);
                SDL_DestroyTexture(ext_texture);
            }
        }
        
        display_idx++;
    }
    
    char footer[MAX_TEXT_LENGTH];
    char footer_translated[MAX_TEXT_LENGTH];
    snprintf(footer, sizeof(footer), "[↑↓] %s    [Enter] %s    [S] %s    [Esc] %s",
             lang_get(app, LANG_NAVIGATE), lang_get(app, LANG_PLAY), lang_get(app, LANG_SETTINGS), lang_get(app, LANG_EXIT));
    gptk_translate_hint(footer_translated, footer, sizeof(footer_translated));

    TextRender footer_r = text_make(app, app->font_small, footer_translated, text_color);

    SDL_Color footer_bg_color = {FOOTER_BG_R, FOOTER_BG_G, FOOTER_BG_B, FOOTER_BG_A};
    SDL_Rect footer_bg_rect = {0, app->window_height - 45, app->window_width, 45};
    SDL_SetRenderDrawColor(app->renderer, footer_bg_color.r, footer_bg_color.g, footer_bg_color.b, footer_bg_color.a);
    SDL_RenderFillRect(app->renderer, &footer_bg_rect);

    text_draw(&footer_r, app->renderer, 20, app->window_height - 40);

    if (app->settings.loop) {
        TextRender loop_r = text_make(app, app->font_small, "R", text_color);
        text_draw(&loop_r, app->renderer, app->window_width - loop_r.w - 20, app->window_height - 40);
        text_destroy(&loop_r);
    }

    text_destroy(&footer_r);
}

bool file_browser_navigate_up(AppContext *app, FileBrowser *fb) {
    char new_dir[MAX_PATH_LENGTH];
    snprintf(new_dir, sizeof(new_dir), "%s/..", app->settings.dir);
    normalize_path(new_dir, sizeof(new_dir));
    
    if (!is_path_within(new_dir, app->music_dir)) {
        return false;
    }
    
    strncpy(app->settings.dir, new_dir, sizeof(app->settings.dir) - 1);
    file_browser_scan(fb, app->settings.dir, app->music_dir);
    return true;
}

bool file_browser_navigate_into(AppContext *app, FileBrowser *fb, const char *entry_name) {
    char new_dir[MAX_PATH_LENGTH];
    snprintf(new_dir, sizeof(new_dir), "%s/%s", app->settings.dir, entry_name);
    normalize_path(new_dir, sizeof(new_dir));
    
    if (!is_path_within(new_dir, app->music_dir)) {
        return false;
    }
    
    strncpy(app->settings.dir, new_dir, sizeof(app->settings.dir) - 1);
    file_browser_scan(fb, app->settings.dir, app->music_dir);
    return true;
}

void file_browser_handle_event(AppContext *app, FileBrowser *fb, SDL_Event *event) {
    if (event->type == SDL_KEYDOWN) {
        switch (event->key.keysym.sym) {
            case SDLK_UP:
                if (fb->selected_index > 0) {
                    fb->selected_index--;
                }
                break;
            case SDLK_DOWN:
                if (fb->selected_index < fb->count - 1) {
                    fb->selected_index++;
                }
                break;
            case SDLK_PAGEUP:
                fb->selected_index = fb->selected_index > 15 ? fb->selected_index - 15 : 0;
                break;
            case SDLK_PAGEDOWN:
                fb->selected_index = fb->selected_index + 15 < fb->count ? fb->selected_index + 15 : fb->count - 1;
                break;
            default:
                break;
        }
    }
}
