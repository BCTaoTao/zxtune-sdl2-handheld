#ifndef MAIN_H
#define MAIN_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define MAX_PATH_LENGTH 4096
#define MAX_TEXT_LENGTH 1024
#define FOOTER_BG_R 10
#define FOOTER_BG_G 10
#define FOOTER_BG_B 20
#define FOOTER_BG_A 255

typedef enum {
    DIALOG_NONE,
    DIALOG_EXIT,
    DIALOG_FPS_CUSTOM,
    DIALOG_SEEKSTEP_CUSTOM,
    DIALOG_SPECTRUM_CUSTOM
} DialogType;

typedef enum {
    SCREEN_FILE_BROWSER,
    SCREEN_SETTINGS,
    SCREEN_PLAYING,
    SCREEN_DEBUG_EXIT
} ScreenType;

typedef struct {
    char dir[MAX_PATH_LENGTH];
    bool loop;
    char zx_flags[MAX_TEXT_LENGTH];
    char zx_backend[MAX_TEXT_LENGTH];
    char zx_freq[MAX_TEXT_LENGTH];
    char zx_fps[MAX_TEXT_LENGTH];
    char zx_debug_mode[MAX_TEXT_LENGTH];
    char zx_output_format[MAX_TEXT_LENGTH];
    char zx_seekstep[MAX_TEXT_LENGTH];
    char zx_spectrum_size[MAX_TEXT_LENGTH];
    int window_width;
    int window_height;
    char language[32];
} Settings;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    TTF_Font *font_small;
    Settings settings;
    ScreenType current_screen;
    DialogType current_dialog;
    int dialog_selected;
    int temp_fps_value;
    int temp_seekstep_value;
    int temp_spectrum_size_value;
    char current_language[32];
    char *texts[256];
    char root_dir[MAX_PATH_LENGTH];
    char music_dir[MAX_PATH_LENGTH];
    char player_path[MAX_PATH_LENGTH];
    char state_path[MAX_PATH_LENGTH];
    int window_width;
    int window_height;
    bool settings_changed;
} AppContext;

AppContext* app_create(const char *argv0);
void app_destroy(AppContext *app);
int app_run(AppContext *app);

#endif
