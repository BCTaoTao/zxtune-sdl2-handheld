#ifndef LANG_H
#define LANG_H

#include "main.h"

typedef struct {
    char code[32];
    char name[MAX_TEXT_LENGTH];
} LanguageEntry;

typedef enum {
    LANG_OK,
    LANG_OK_CANCEL,
    LANG_YES_NO,
    LANG_FILE_BROWSER,
    LANG_SETTINGS,
    LANG_PLAYING,
    LANG_EXIT,
    LANG_PLAY,
    LANG_BACK,
    LANG_SETTINGS_MENU,
    LANG_CORE_PLAYBACK,
    LANG_AUDIO_SAMPLE_RATE,
    LANG_VISUAL_DISPLAY,
    LANG_UI_REFRESH_RATE,
    LANG_SEEK_STEP,
    LANG_AUDIO_BACKEND,
    LANG_DEBUG_MODE,
    LANG_VIEW_INFO,
    LANG_ABOUT,
    LANG_LOOP_PLAYBACK,
    LANG_FORCE_YM_CHIP,
    LANG_DEFAULT,
    LANG_44100,
    LANG_48000,
    LANG_22050,
    LANG_ENABLE_ANALYZER,
    LANG_QUIET_MODE,
    LANG_COMPLETELY_SILENT,
    LANG_ALSA,
    LANG_OSS,
    LANG_PULSEAUDIO,
    LANG_NULL_TEST,
    LANG_WAIT_FOR_KEY,
    LANG_VIEW_PLUGINS,
    LANG_VIEW_BACKENDS,
    LANG_VIEW_PROVIDERS,
    LANG_VIEW_FREQ_TABLES,
    LANG_VERSION_INFO,
    LANG_ENJOY_MUSIC,
    LANG_PATH,
    LANG_NOW_PLAYING,
    LANG_PARAMETERS,
    LANG_AUTHOR,
    LANG_PROGRAM,
    LANG_TYPE,
    LANG_PLAYBACK_FINISHED,
    LANG_PAUSE,
    LANG_RESUME,
    LANG_KEY_NEXT,
    LANG_KEY_SEEK,
    LANG_KEY_VOLUME,
    LANG_LANGUAGE,
    LANG_LANGUAGE_NAME,
    LANG_ON,
    LANG_OFF,
    LANG_NAVIGATE,
    LANG_SELECT,
    LANG_TOGGLE,
    LANG_CUSTOM,
    LANG_BINARY_COMM,
    LANG_SPECTRUM_SIZE,
    LANG_DEBUG_EXIT_PROMPT,
    LANG_OPENING,
    LANG_SHOW_ERROR_INFO,
    LANG_EMPTY,
    LANG_TEXT_COUNT
} TextID;

bool lang_load(AppContext *app, const char *lang_code);
void lang_free(AppContext *app);
const char* lang_get(AppContext *app, TextID id);
int lang_scan(AppContext *app, LanguageEntry **languages);
void lang_free_list(LanguageEntry *languages, int count);

#endif
