#ifndef SETTINGS_H
#define SETTINGS_H

#include "main.h"
#include "lang.h"

typedef enum {
    SETTINGS_MAIN,
    SETTINGS_PLAYBACK,
    SETTINGS_FREQUENCY,
    SETTINGS_VISUAL,
    SETTINGS_FPS,
    SETTINGS_SEEKSTEP,
    SETTINGS_SPECTRUM,
    SETTINGS_BACKEND,
    SETTINGS_DEBUG,
    SETTINGS_INFO,
    SETTINGS_INFO_MENU,
    SETTINGS_INFO_DISPLAY,
    SETTINGS_ABOUT,
    SETTINGS_MESSAGE,
    SETTINGS_LANGUAGE
} SettingsMenu;

typedef struct {
    SettingsMenu current_menu;
    SettingsMenu previous_menu;
    int selected_index;
    int scroll_offset;
    char message[MAX_TEXT_LENGTH];
    bool loop_enabled;
    bool ym_enabled;
    char freq_option[32];
    char visual_mode[32];
    int fps_value;
    int seekstep_value;
    int spectrum_size_value;
    char backend_option[32];
    bool debug_mode;
    bool binary_mode;
    LanguageEntry *languages;
    int language_count;
    char *info_text;
    int info_text_lines;
    char version_cache[256];
    bool version_cached;
} SettingsUI;

SettingsUI* settings_ui_create(void);
void settings_ui_destroy(SettingsUI *ui);
void settings_ui_render(AppContext *app, SettingsUI *ui);
void settings_ui_handle_event(AppContext *app, SettingsUI *ui, SDL_Event *event);
void apply_settings_from_ui(AppContext *app, SettingsUI *ui);
bool settings_load(AppContext *app);
bool settings_save(AppContext *app);

#endif
