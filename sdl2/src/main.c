/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>
#include "main.h"
#include "lang.h"
#include "file_browser.h"
#include "settings.h"
#include "player.h"
#include "gptk.h"

void mark_settings_changed(AppContext *app);

static bool try_load_font_from_cfg(AppContext *app) {
    char cfg_path[MAX_PATH_LENGTH];
    snprintf(cfg_path, sizeof(cfg_path), "%s/res/font.cfg", app->root_dir);

    FILE *f = fopen(cfg_path, "r");
    if (!f) return false;

    char font_name[MAX_PATH_LENGTH] = {0};
    int ttc_index = 0;
    char line[512];
    bool has_content = false;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0' || line[0] == '#') continue;
        has_content = true;

        char value[512];
        if (sscanf(line, "font = \"%[^\"]\"", value) == 1) {
            strncpy(font_name, value, sizeof(font_name) - 1);
        } else if (sscanf(line, "font = %511s", value) == 1) {
            strncpy(font_name, value, sizeof(font_name) - 1);
        } else {
            sscanf(line, "index = %d", &ttc_index);
        }
    }
    fclose(f);

    if (!has_content || font_name[0] == '\0') return false;

    char font_path[MAX_PATH_LENGTH];
    snprintf(font_path, sizeof(font_path), "%s/res/%s", app->root_dir, font_name);

    FILE *ftest = fopen(font_path, "r");
    if (!ftest) {
        fprintf(stderr, "Font from cfg not found: %s\n", font_path);
        return false;
    }
    fclose(ftest);

    size_t len = strlen(font_name);
    bool is_ttc = (len >= 4 && (strcmp(font_name + len - 4, ".ttc") == 0 ||
                                 strcmp(font_name + len - 4, ".TTC") == 0));

    if (is_ttc) {
        app->font = TTF_OpenFontIndex(font_path, 24, ttc_index);
        if (app->font) {
            app->font_small = TTF_OpenFontIndex(font_path, 18, ttc_index);
        }
    } else {
        app->font = TTF_OpenFont(font_path, 24);
        if (app->font) {
            app->font_small = TTF_OpenFont(font_path, 18);
        }
    }

    if (app->font) {
        fprintf(stderr, "Loaded font: %s (index %d)\n", font_path, ttc_index);
        return true;
    }

    fprintf(stderr, "Failed to open font from cfg: %s\n", font_path);
    return false;
}
void save_settings_if_changed(AppContext *app);

AppContext* app_create(const char *argv0) {
    AppContext *app = (AppContext *)malloc(sizeof(AppContext));
    if (!app) return NULL;
    
    memset(app, 0, sizeof(AppContext));
    app->settings_changed = false;
    
    char exe_path[MAX_PATH_LENGTH];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);
        strncpy(app->root_dir, dir, sizeof(app->root_dir) - 1);
    } else if (argv0 && strlen(argv0) > 0) {
        strncpy(exe_path, argv0, sizeof(exe_path) - 1);
        exe_path[sizeof(exe_path) - 1] = '\0';
        char *dir = dirname(exe_path);
        char cwd[MAX_PATH_LENGTH];
        if (dir[0] != '/' && getcwd(cwd, sizeof(cwd))) {
            snprintf(app->root_dir, sizeof(app->root_dir), "%s/%s", cwd, dir);
        } else {
            strncpy(app->root_dir, dir, sizeof(app->root_dir) - 1);
        }
    } else {
        if (getcwd(app->root_dir, sizeof(app->root_dir)) == NULL) {
            strncpy(app->root_dir, ".", sizeof(app->root_dir) - 1);
        }
    }
    
    snprintf(app->music_dir, sizeof(app->music_dir), "%s/Music", app->root_dir);
    snprintf(app->player_path, sizeof(app->player_path), "%s/zxtune123", app->root_dir);
    snprintf(app->state_path, sizeof(app->state_path), "%s/settings.ini", app->root_dir);
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        free(app);
        return NULL;
    }
    
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF init failed: %s\n", TTF_GetError());
        SDL_Quit();
        free(app);
        return NULL;
    }
    
    app->window_width = WINDOW_WIDTH;
    app->window_height = WINDOW_HEIGHT;
    
    app->window = SDL_CreateWindow(
        "ZXTune SDL2 Player",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        app->window_width,
        app->window_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!app->window) {
        fprintf(stderr, "Window create failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        free(app);
        return NULL;
    }
    
    app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_ACCELERATED);
    if (!app->renderer) {
        fprintf(stderr, "Renderer create failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(app->window);
        TTF_Quit();
        SDL_Quit();
        free(app);
        return NULL;
    }
    
    if (!try_load_font_from_cfg(app)) {
        char app_font_path[MAX_PATH_LENGTH];
        snprintf(app_font_path, sizeof(app_font_path), "%s/font.ttf", app->root_dir);
        const char *font_paths[] = {
            app_font_path,
            "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
            "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
            "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJKsc-Regular.otf",
            "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
            NULL
        };

        for (int i = 0; font_paths[i] != NULL; i++) {
            app->font = TTF_OpenFont(font_paths[i], 24);
            if (app->font) {
                fprintf(stderr, "Loaded font: %s\n", font_paths[i]);
                break;
            }
        }
        if (!app->font) {
            fprintf(stderr, "Warning: Failed to load any font, text will not be displayed\n");
        }

        for (int i = 0; font_paths[i] != NULL; i++) {
            app->font_small = TTF_OpenFont(font_paths[i], 18);
            if (app->font_small) {
                break;
            }
        }
        if (!app->font_small && app->font) {
            app->font_small = app->font;
        }
    }
    
    app->current_screen = SCREEN_FILE_BROWSER;
    
    settings_load(app);
    
    strncpy(app->current_language, app->settings.language, sizeof(app->current_language) - 1);
    
    if (!lang_load(app, app->settings.language)) {
        fprintf(stderr, "Failed to load %s language file, trying English...\n", app->settings.language);
        strncpy(app->current_language, "en", sizeof(app->current_language) - 1);
        lang_load(app, "en");
    }
    
    if (app->settings.window_width > 0 && app->settings.window_height > 0) {
        app->window_width = app->settings.window_width;
        app->window_height = app->settings.window_height;
        SDL_SetWindowSize(app->window, app->window_width, app->window_height);
    }

    gptk_load(app->root_dir);

    return app;
}

void app_destroy(AppContext *app) {
    if (!app) return;
    
    lang_free(app);
    gptk_free();

    if (app->font && app->font != app->font_small) {
        TTF_CloseFont(app->font);
    }
    if (app->font_small) {
        TTF_CloseFont(app->font_small);
    }
    
    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
    }
    
    TTF_Quit();
    SDL_Quit();
    
    free(app);
}

static void draw_rounded_rect(SDL_Renderer *renderer, SDL_Rect rect, int radius, SDL_Color color) {
    if (radius > rect.w / 2) radius = rect.w / 2;
    if (radius > rect.h / 2) radius = rect.h / 2;
    
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    
    SDL_Rect center_rect = {rect.x + radius, rect.y + radius, rect.w - 2 * radius, rect.h - 2 * radius};
    SDL_RenderFillRect(renderer, &center_rect);
    
    SDL_Rect top_rect = {rect.x + radius, rect.y, rect.w - 2 * radius, radius};
    SDL_RenderFillRect(renderer, &top_rect);
    
    SDL_Rect bottom_rect = {rect.x + radius, rect.y + rect.h - radius, rect.w - 2 * radius, radius};
    SDL_RenderFillRect(renderer, &bottom_rect);
    
    SDL_Rect left_rect = {rect.x, rect.y + radius, radius, rect.h - 2 * radius};
    SDL_RenderFillRect(renderer, &left_rect);
    
    SDL_Rect right_rect = {rect.x + rect.w - radius, rect.y + radius, radius, rect.h - 2 * radius};
    SDL_RenderFillRect(renderer, &right_rect);
    
    for (int y = 0; y <= radius; y++) {
        int x = (int)sqrt((double)(radius * radius - y * y));
        
        SDL_RenderDrawLine(renderer, rect.x + radius - x, rect.y + radius - y, rect.x + radius - 1, rect.y + radius - y);
        SDL_RenderDrawLine(renderer, rect.x + rect.w - radius, rect.y + radius - y, rect.x + rect.w - radius + x - 1, rect.y + radius - y);
        SDL_RenderDrawLine(renderer, rect.x + radius - x, rect.y + rect.h - radius + y, rect.x + radius - 1, rect.y + rect.h - radius + y);
        SDL_RenderDrawLine(renderer, rect.x + rect.w - radius, rect.y + rect.h - radius + y, rect.x + rect.w - radius + x - 1, rect.y + rect.h - radius + y);
    }
}

static void render_dialog(AppContext *app) {
    SDL_Color text_color = {255, 255, 255, 255};
    SDL_Color highlight_text_color = {0, 200, 255, 255};
    SDL_Color dialog_bg = {50, 50, 80, 255};
    SDL_Color dark_overlay = {0, 0, 0, 128};
    SDL_Color option_box_bg = {70, 70, 100, 255};
    SDL_Color option_box_selected_bg = {80, 120, 160, 255};
    
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, dark_overlay.r, dark_overlay.g, dark_overlay.b, dark_overlay.a);
    SDL_RenderFillRect(app->renderer, &(SDL_Rect){0, 0, app->window_width, app->window_height});
    
    SDL_Rect dialog_rect;
    const char *title = NULL;
    const char *options[10];
    int option_count = 0;
    
    switch (app->current_dialog) {
        case DIALOG_EXIT:
            dialog_rect.w = 400;
            dialog_rect.h = 200;
            dialog_rect.x = (app->window_width - dialog_rect.w) / 2;
            dialog_rect.y = (app->window_height - dialog_rect.h) / 2;
            title = lang_get(app, LANG_EXIT);
            options[0] = "    Yes    ";
            options[1] = "    No     ";
            option_count = 2;
            break;
        case DIALOG_FPS_CUSTOM:
            title = lang_get(app, LANG_UI_REFRESH_RATE);
            dialog_rect.w = 450;
            dialog_rect.h = 280;
            dialog_rect.x = (app->window_width - dialog_rect.w) / 2;
            dialog_rect.y = (app->window_height - dialog_rect.h) / 2;
            options[0] = "  Confirm  ";
            options[1] = "  Cancel   ";
            option_count = 2;
            break;
        case DIALOG_SEEKSTEP_CUSTOM:
            title = lang_get(app, LANG_SEEK_STEP);
            dialog_rect.w = 450;
            dialog_rect.h = 280;
            dialog_rect.x = (app->window_width - dialog_rect.w) / 2;
            dialog_rect.y = (app->window_height - dialog_rect.h) / 2;
            options[0] = "  Confirm  ";
            options[1] = "  Cancel   ";
            option_count = 2;
            break;
        case DIALOG_SPECTRUM_CUSTOM:
            title = lang_get(app, LANG_SPECTRUM_SIZE);
            dialog_rect.w = 450;
            dialog_rect.h = 280;
            dialog_rect.x = (app->window_width - dialog_rect.w) / 2;
            dialog_rect.y = (app->window_height - dialog_rect.h) / 2;
            options[0] = "  Confirm  ";
            options[1] = "  Cancel   ";
            option_count = 2;
            break;
        default:
            return;
    }
    
    draw_rounded_rect(app->renderer, dialog_rect, 20, dialog_bg);
    
    if (title) {
        char title_text[MAX_TEXT_LENGTH];
        if (app->current_dialog == DIALOG_EXIT) {
            snprintf(title_text, sizeof(title_text), "%s?", title);
        } else {
            snprintf(title_text, sizeof(title_text), "%s", title);
        }
        SDL_Surface *title_surface = TTF_RenderUTF8_Blended(app->font, title_text, text_color);
        SDL_Texture *title_texture = SDL_CreateTextureFromSurface(app->renderer, title_surface);
        
        int title_w, title_h;
        SDL_QueryTexture(title_texture, NULL, NULL, &title_w, &title_h);
        SDL_Rect title_rect = {dialog_rect.x + (dialog_rect.w - title_w) / 2, dialog_rect.y + 30, title_w, title_h};
        SDL_RenderCopy(app->renderer, title_texture, NULL, &title_rect);
        
        SDL_FreeSurface(title_surface);
        SDL_DestroyTexture(title_texture);
    }
    
    if (app->current_dialog == DIALOG_FPS_CUSTOM || app->current_dialog == DIALOG_SEEKSTEP_CUSTOM
        || app->current_dialog == DIALOG_SPECTRUM_CUSTOM) {
        int value = (app->current_dialog == DIALOG_FPS_CUSTOM) ? app->temp_fps_value
                  : (app->current_dialog == DIALOG_SEEKSTEP_CUSTOM) ? app->temp_seekstep_value
                  : app->temp_spectrum_size_value;
        const char *suffix = (app->current_dialog == DIALOG_FPS_CUSTOM) ? " FPS"
                           : (app->current_dialog == DIALOG_SEEKSTEP_CUSTOM) ? "%"
                           : "";
        char value_text[32];
        snprintf(value_text, sizeof(value_text), "%d%s", value, suffix);
        SDL_Surface *value_surface = TTF_RenderUTF8_Blended(app->font, value_text, highlight_text_color);
        SDL_Texture *value_texture = SDL_CreateTextureFromSurface(app->renderer, value_surface);
        
        int value_w, value_h;
        SDL_QueryTexture(value_texture, NULL, NULL, &value_w, &value_h);
        SDL_Rect value_rect = {dialog_rect.x + (dialog_rect.w - value_w) / 2, dialog_rect.y + 90, value_w, value_h};
        SDL_RenderCopy(app->renderer, value_texture, NULL, &value_rect);
        
        SDL_FreeSurface(value_surface);
        SDL_DestroyTexture(value_texture);
        
        const char *hint = "[←→] ±1    [↑↓] ±10";
        SDL_Surface *hint_surface = TTF_RenderUTF8_Blended(app->font_small, hint, text_color);
        SDL_Texture *hint_texture = SDL_CreateTextureFromSurface(app->renderer, hint_surface);
        
        int hint_w, hint_h;
        SDL_QueryTexture(hint_texture, NULL, NULL, &hint_w, &hint_h);
        SDL_Rect hint_rect = {dialog_rect.x + (dialog_rect.w - hint_w) / 2, dialog_rect.y + 140, hint_w, hint_h};
        SDL_RenderCopy(app->renderer, hint_texture, NULL, &hint_rect);
        
        SDL_FreeSurface(hint_surface);
        SDL_DestroyTexture(hint_texture);
    }
    
    int start_y = dialog_rect.y + dialog_rect.h - 80;
    int total_width = 0;
    int option_widths[10];
    int option_heights[10];
    
    for (int i = 0; i < option_count; i++) {
        SDL_Surface *s = TTF_RenderUTF8_Blended(app->font_small, options[i], text_color);
        option_widths[i] = s->w;
        option_heights[i] = s->h;
        total_width += s->w + 30;
        SDL_FreeSurface(s);
    }
    total_width -= 30;
    
    int x = dialog_rect.x + (dialog_rect.w - total_width) / 2;
    
    for (int i = 0; i < option_count; i++) {
        int padding = 15;
        SDL_Rect box_rect = {x - padding, start_y - 5, option_widths[i] + 2 * padding, option_heights[i] + 10};
        
        if (i == app->dialog_selected) {
            SDL_SetRenderDrawColor(app->renderer, option_box_selected_bg.r, option_box_selected_bg.g, option_box_selected_bg.b, option_box_selected_bg.a);
        } else {
            SDL_SetRenderDrawColor(app->renderer, option_box_bg.r, option_box_bg.g, option_box_bg.b, option_box_bg.a);
        }
        SDL_RenderFillRect(app->renderer, &box_rect);
        
        SDL_Color color = (i == app->dialog_selected) ? highlight_text_color : text_color;
        SDL_Surface *option_surface = TTF_RenderUTF8_Blended(app->font_small, options[i], color);
        SDL_Texture *option_texture = SDL_CreateTextureFromSurface(app->renderer, option_surface);
        
        SDL_Rect rect = {x, start_y, option_widths[i], option_heights[i]};
        SDL_RenderCopy(app->renderer, option_texture, NULL, &rect);
        
        x += option_widths[i] + 30;
        
        SDL_FreeSurface(option_surface);
        SDL_DestroyTexture(option_texture);
    }
    
}

typedef enum {
    CUSTOM_DLG_NONE,
    CUSTOM_DLG_CONFIRMED,
    CUSTOM_DLG_CANCELLED
} CustomDlgResult;

static CustomDlgResult handle_custom_value_dialog(AppContext *app, SDL_Keycode key,
                                                   int *value, int min_value, int max_value) {
    switch (key) {
        case SDLK_LEFT:
            if (*value > min_value) (*value)--;
            break;
        case SDLK_RIGHT:
            if (*value < max_value) (*value)++;
            break;
        case SDLK_UP:
            if (*value <= max_value - 10) *value += 10;
            else *value = max_value;
            break;
        case SDLK_DOWN:
            if (*value >= min_value + 10) *value -= 10;
            else *value = min_value;
            break;
        case SDLK_RETURN:
            if (app->dialog_selected == 0) {
                app->current_dialog = DIALOG_NONE;
                return CUSTOM_DLG_CONFIRMED;
            }
            app->current_dialog = DIALOG_NONE;
            return CUSTOM_DLG_CANCELLED;
        case SDLK_ESCAPE:
            app->current_dialog = DIALOG_NONE;
            return CUSTOM_DLG_CANCELLED;
        case SDLK_TAB:
            app->dialog_selected = (app->dialog_selected + 1) % 2;
            break;
        default:
            break;
    }
    return CUSTOM_DLG_NONE;
}

int app_run(AppContext *app) {
    FileBrowser *fb = file_browser_create();
    SettingsUI *settings_ui = settings_ui_create();
    Player *player = player_create();

    if (!fb || !settings_ui || !player) {
        fprintf(stderr, "Failed to create UI components\n");
        if (fb) file_browser_destroy(fb);
        if (settings_ui) settings_ui_destroy(settings_ui);
        if (player) player_destroy(player);
        return 1;
    }

    settings_ui->loop_enabled = app->settings.loop;
    file_browser_scan(fb, app->settings.dir, app->music_dir);
    if (fb->count == 0) {
        strncpy(app->settings.dir, app->music_dir, sizeof(app->settings.dir) - 1);
        file_browser_scan(fb, app->settings.dir, app->music_dir);
    }
    
    bool running = true;
    SDL_Event event;
    app->current_dialog = DIALOG_NONE;
    app->dialog_selected = 0;
    
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (app->current_dialog != DIALOG_NONE) {
                if (event.type == SDL_KEYDOWN) {
                    switch (app->current_dialog) {
                        case DIALOG_EXIT:
                            switch (event.key.keysym.sym) {
                                case SDLK_LEFT:
                                case SDLK_UP:
                                    if (app->dialog_selected > 0) app->dialog_selected--;
                                    break;
                                case SDLK_RIGHT:
                                case SDLK_DOWN:
                                    if (app->dialog_selected < 1) app->dialog_selected++;
                                    break;
                                case SDLK_RETURN:
                                    if (app->dialog_selected == 0) {
                                        running = false;
                                    } else {
                                        app->current_dialog = DIALOG_NONE;
                                    }
                                    break;
                                case SDLK_ESCAPE:
                                    app->current_dialog = DIALOG_NONE;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case DIALOG_FPS_CUSTOM: {
                            CustomDlgResult res = handle_custom_value_dialog(app, event.key.keysym.sym,
                                &app->temp_fps_value, 1, 60);
                            if (res == CUSTOM_DLG_CONFIRMED) {
                                settings_ui->fps_value = app->temp_fps_value;
                                apply_settings_from_ui(app, settings_ui);
                            }
                            break;
                        }
                        case DIALOG_SEEKSTEP_CUSTOM: {
                            CustomDlgResult res = handle_custom_value_dialog(app, event.key.keysym.sym,
                                &app->temp_seekstep_value, 1, 100);
                            if (res == CUSTOM_DLG_CONFIRMED) {
                                settings_ui->seekstep_value = app->temp_seekstep_value;
                                apply_settings_from_ui(app, settings_ui);
                            }
                            break;
                        }
                        case DIALOG_SPECTRUM_CUSTOM: {
                            CustomDlgResult res = handle_custom_value_dialog(app, event.key.keysym.sym,
                                &app->temp_spectrum_size_value, 9, 256);
                            if (res == CUSTOM_DLG_CONFIRMED) {
                                settings_ui->spectrum_size_value = app->temp_spectrum_size_value;
                                apply_settings_from_ui(app, settings_ui);
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
                continue;
            }
            
            if (event.type == SDL_QUIT) {
                app->current_dialog = DIALOG_EXIT;
                app->dialog_selected = 0;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                app->window_width = event.window.data1;
                app->window_height = event.window.data2;
            }
            
            switch (app->current_screen) {
                case SCREEN_FILE_BROWSER:
                    file_browser_handle_event(app, fb, &event);
                    if (event.type == SDL_KEYDOWN) {
                        switch (event.key.keysym.sym) {
                            case SDLK_RETURN:
                                if (fb->count > 0) {
                                    if (fb->entries[fb->selected_index].is_dir) {
                                        if (strcmp(fb->entries[fb->selected_index].name, "..") == 0) {
                                            file_browser_navigate_up(app, fb);
                                        } else {
                                            file_browser_navigate_into(app, fb, fb->entries[fb->selected_index].name);
                                        }
                                    } else {
                                        char full_path[MAX_PATH_LENGTH];
                                        snprintf(full_path, sizeof(full_path), "%s/%s", 
                                                 app->settings.dir, fb->entries[fb->selected_index].name);
                                        if (player_play(app, player, full_path)) {
                                            app->current_screen = SCREEN_PLAYING;
                                        }
                                    }
                                }
                                break;
                            case SDLK_RIGHT:
                                if (fb->count > 0 && fb->entries[fb->selected_index].is_dir) {
                                    if (strcmp(fb->entries[fb->selected_index].name, "..") == 0) {
                                        file_browser_navigate_up(app, fb);
                                    } else {
                                        file_browser_navigate_into(app, fb, fb->entries[fb->selected_index].name);
                                    }
                                }
                                break;
                            case SDLK_s:
                                app->current_screen = SCREEN_SETTINGS;
                                settings_ui->selected_index = 0;
                                break;
                            case SDLK_r:
                                settings_ui->loop_enabled = !settings_ui->loop_enabled;
                                app->settings.loop = settings_ui->loop_enabled;
                                apply_settings_from_ui(app, settings_ui);
                                mark_settings_changed(app);
                                break;
                            case SDLK_ESCAPE:
                                app->current_dialog = DIALOG_EXIT;
                                app->dialog_selected = 0;
                                break;
                            case SDLK_q:
                            case SDLK_LEFT:
                                file_browser_navigate_up(app, fb);
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case SCREEN_SETTINGS:
                    settings_ui_handle_event(app, settings_ui, &event);
                    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_SPACE) {
                        if (settings_ui->current_menu == SETTINGS_FPS) {
                            app->temp_fps_value = (settings_ui->fps_value == 0) ? 30 : settings_ui->fps_value;
                            app->current_dialog = DIALOG_FPS_CUSTOM;
                            app->dialog_selected = 0;
                        } else if (settings_ui->current_menu == SETTINGS_SEEKSTEP) {
                            app->temp_seekstep_value = (settings_ui->seekstep_value == 0) ? 10 : settings_ui->seekstep_value;
                            app->current_dialog = DIALOG_SEEKSTEP_CUSTOM;
                            app->dialog_selected = 0;
                        } else if (settings_ui->current_menu == SETTINGS_SPECTRUM) {
                            app->temp_spectrum_size_value = (settings_ui->spectrum_size_value == 0)
                                ? 64 : settings_ui->spectrum_size_value;
                            app->current_dialog = DIALOG_SPECTRUM_CUSTOM;
                            app->dialog_selected = 0;
                        }
                    }
                    break;
                case SCREEN_PLAYING:
                    player_handle_event(app, player, &event);
                    break;
                case SCREEN_DEBUG_EXIT:
                    if (event.type == SDL_KEYDOWN) {
                        if (event.key.keysym.sym == SDLK_UP) {
                            if (player->debug_scroll > 0) player->debug_scroll--;
                        } else if (event.key.keysym.sym == SDLK_DOWN) {
                            player->debug_scroll++;
                        } else if (event.key.keysym.sym == SDLK_PAGEUP) {
                            player->debug_scroll -= 15;
                            if (player->debug_scroll < 0) player->debug_scroll = 0;
                        } else if (event.key.keysym.sym == SDLK_PAGEDOWN) {
                            player->debug_scroll += 15;
                        } else {
                            player->debug_wait_exit = false;
                            app->current_screen = SCREEN_FILE_BROWSER;
                        }
                    }
                    break;

            }
        }
        
        if (app->current_screen == SCREEN_PLAYING) {
            player_update(app, player);
        }

        bool needs_full_render = false;
        switch (app->current_screen) {
            case SCREEN_FILE_BROWSER:
                file_browser_render(app, fb);
                needs_full_render = true;
                break;
            case SCREEN_SETTINGS:
                settings_ui_render(app, settings_ui);
                needs_full_render = true;
                break;
            case SCREEN_PLAYING:
                if (player_has_new_data(player) || app->current_dialog != DIALOG_NONE) {
                    player_render(app, player);
                    needs_full_render = true;
                }
                break;
            case SCREEN_DEBUG_EXIT:
                player_render_debug_exit(app, player);
                needs_full_render = true;
                break;
            default:
                break;
        }

        if (app->current_dialog != DIALOG_NONE) {
            render_dialog(app);
            needs_full_render = true;
        }

        if (needs_full_render) {
            SDL_RenderPresent(app->renderer);
        }

        SDL_Delay(16);
    }
    
    app->settings.window_width = app->window_width;
    app->settings.window_height = app->window_height;
    mark_settings_changed(app);
    save_settings_if_changed(app);
    
    file_browser_destroy(fb);
    settings_ui_destroy(settings_ui);
    player_destroy(player);
    
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc;
    AppContext *app = app_create(argv[0]);
    if (!app) {
        fprintf(stderr, "Failed to create application\n");
        return 1;
    }
    
    int ret = app_run(app);
    
    app_destroy(app);
    
    return ret;
}
