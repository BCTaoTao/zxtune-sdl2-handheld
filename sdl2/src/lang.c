/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "lang.h"
#include "util.h"

typedef struct {
    TextID id;
    const char *key;
} LangKeyMap;

static const LangKeyMap lang_key_map[] = {
    {LANG_OK, "OK"},
    {LANG_OK_CANCEL, "OK_CANCEL"},
    {LANG_YES_NO, "YES_NO"},
    {LANG_FILE_BROWSER, "FILE_BROWSER"},
    {LANG_SETTINGS, "SETTINGS"},
    {LANG_PLAYING, "PLAYING"},
    {LANG_EXIT, "EXIT"},
    {LANG_PLAY, "PLAY"},
    {LANG_BACK, "BACK"},
    {LANG_SETTINGS_MENU, "SETTINGS_MENU"},
    {LANG_CORE_PLAYBACK, "CORE_PLAYBACK"},
    {LANG_AUDIO_SAMPLE_RATE, "AUDIO_SAMPLE_RATE"},
    {LANG_VISUAL_DISPLAY, "VISUAL_DISPLAY"},
    {LANG_UI_REFRESH_RATE, "UI_REFRESH_RATE"},
    {LANG_SEEK_STEP, "SEEK_STEP"},
    {LANG_AUDIO_BACKEND, "AUDIO_BACKEND"},
    {LANG_DEBUG_MODE, "DEBUG_MODE"},
    {LANG_VIEW_INFO, "VIEW_INFO"},
    {LANG_ABOUT, "ABOUT"},
    {LANG_LOOP_PLAYBACK, "LOOP_PLAYBACK"},
    {LANG_FORCE_YM_CHIP, "FORCE_YM_CHIP"},
    {LANG_DEFAULT, "DEFAULT"},
    {LANG_44100, "44100"},
    {LANG_48000, "48000"},
    {LANG_22050, "22050"},
    {LANG_ENABLE_ANALYZER, "ENABLE_ANALYZER"},
    {LANG_QUIET_MODE, "QUIET_MODE"},
    {LANG_COMPLETELY_SILENT, "COMPLETELY_SILENT"},
    {LANG_ALSA, "ALSA"},
    {LANG_OSS, "OSS"},
    {LANG_PULSEAUDIO, "PULSEAUDIO"},
    {LANG_NULL_TEST, "NULL_TEST"},
    {LANG_SHOW_ERROR_INFO, "SHOW_ERROR_INFO"},
    {LANG_EMPTY, "EMPTY"},
    {LANG_VIEW_PLUGINS, "VIEW_PLUGINS"},
    {LANG_VIEW_BACKENDS, "VIEW_BACKENDS"},
    {LANG_VIEW_PROVIDERS, "VIEW_PROVIDERS"},
    {LANG_VIEW_FREQ_TABLES, "VIEW_FREQ_TABLES"},
    {LANG_VERSION_INFO, "VERSION_INFO"},
    {LANG_ENJOY_MUSIC, "ENJOY_MUSIC"},
    {LANG_PATH, "PATH"},
    {LANG_NOW_PLAYING, "NOW_PLAYING"},
    {LANG_PARAMETERS, "PARAMETERS"},
    {LANG_AUTHOR, "AUTHOR"},
    {LANG_PROGRAM, "PROGRAM"},
    {LANG_TYPE, "TYPE"},
    {LANG_PLAYBACK_FINISHED, "PLAYBACK_FINISHED"},
    {LANG_PAUSE, "PAUSE"},
    {LANG_RESUME, "RESUME"},
    {LANG_KEY_NEXT, "KEY_NEXT"},
    {LANG_KEY_SEEK, "KEY_SEEK"},
    {LANG_KEY_VOLUME, "KEY_VOLUME"},
    {LANG_LANGUAGE, "LANGUAGE"},
    {LANG_LANGUAGE_NAME, "LANGUAGE_NAME"},
    {LANG_ON, "ON"},
    {LANG_OFF, "OFF"},
    {LANG_NAVIGATE, "NAVIGATE"},
    {LANG_SELECT, "SELECT"},
    {LANG_TOGGLE, "TOGGLE"},
    {LANG_CUSTOM, "CUSTOM"},
    {LANG_BINARY_COMM, "BINARY_COMM"},
    {LANG_SPECTRUM_SIZE, "SPECTRUM_SIZE"},
    {LANG_DEBUG_EXIT_PROMPT, "DEBUG_EXIT_PROMPT"},
    {LANG_OPENING, "OPENING"},
};

static const int lang_key_map_count = sizeof(lang_key_map) / sizeof(lang_key_map[0]);

static int lang_key_to_id(const char *key) {
    for (int i = 0; i < lang_key_map_count; i++) {
        if (strcmp(key, lang_key_map[i].key) == 0) {
            return lang_key_map[i].id;
        }
    }
    return -1;
}

static const char* lang_id_to_key(TextID id) {
    for (int i = 0; i < lang_key_map_count; i++) {
        if (lang_key_map[i].id == (int)id) {
            return lang_key_map[i].key;
        }
    }
    return NULL;
}

bool lang_load(AppContext *app, const char *lang_code) {
    char lang_path[MAX_PATH_LENGTH];
    snprintf(lang_path, sizeof(lang_path), "%s/LANG/%s.lang", app->root_dir, lang_code);
    
    FILE *file = fopen(lang_path, "r");
    if (!file) {
        fprintf(stderr, "Failed to open language file: %s\n", lang_path);
        return false;
    }

    for (int i = 0; i < LANG_TEXT_COUNT; i++) {
        if (app->texts[i]) {
            free(app->texts[i]);
            app->texts[i] = NULL;
        }
    }

    char line[MAX_TEXT_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = trim_whitespace(line);
            char *value = trim_whitespace(eq + 1);
            
            int id = lang_key_to_id(key);
            if (id >= 0 && id < LANG_TEXT_COUNT) {
                app->texts[id] = my_strdup(value);
            }
        }
    }

    fclose(file);
    strncpy(app->current_language, lang_code, sizeof(app->current_language) - 1);
    app->current_language[sizeof(app->current_language) - 1] = '\0';
    return true;
}

void lang_free(AppContext *app) {
    for (int i = 0; i < LANG_TEXT_COUNT; i++) {
        if (app->texts[i]) {
            free(app->texts[i]);
            app->texts[i] = NULL;
        }
    }
}

const char* lang_get(AppContext *app, TextID id) {
    if (id >= 0 && id < LANG_TEXT_COUNT && app->texts[id]) {
        return app->texts[id];
    }

    const char *key_name = lang_id_to_key(id);
    if (key_name) {
        static char fallback[32][128];
        static int fallback_idx = 0;
        int i = fallback_idx;
        fallback_idx = (fallback_idx + 1) % 32;
        snprintf(fallback[i], sizeof(fallback[i]), "{{%s}}", key_name);
        return fallback[i];
    }

    return "{{?}}";
}

int lang_scan(AppContext *app, LanguageEntry **languages) {
    char lang_dir[MAX_PATH_LENGTH];
    snprintf(lang_dir, sizeof(lang_dir), "%s/LANG", app->root_dir);
    
    DIR *dir = opendir(lang_dir);
    if (!dir) {
        *languages = NULL;
        return 0;
    }
    
    int count = 0;
    int capacity = 8;
    *languages = (LanguageEntry *)malloc(capacity * sizeof(LanguageEntry));
    if (!*languages) {
        closedir(dir);
        return 0;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        int len = strlen(entry->d_name);
        if (len > 5 && strcmp(entry->d_name + len - 5, ".lang") == 0) {
            if (count >= capacity) {
                capacity *= 2;
                LanguageEntry *new_langs = (LanguageEntry *)realloc(*languages, capacity * sizeof(LanguageEntry));
                if (!new_langs) {
                    break;
                }
                *languages = new_langs;
            }
            
            strncpy((*languages)[count].code, entry->d_name, len - 5);
            (*languages)[count].code[len - 5] = '\0';
            
            char lang_path[MAX_PATH_LENGTH];
            snprintf(lang_path, sizeof(lang_path), "%s/%s", lang_dir, entry->d_name);
            FILE *file = fopen(lang_path, "r");
            if (file) {
                char line[MAX_TEXT_LENGTH];
                bool found = false;
                while (fgets(line, sizeof(line), file) && !found) {
                    char *eq = strchr(line, '=');
                    if (eq) {
                        *eq = '\0';
                        char *key = trim_whitespace(line);
                        char *value = trim_whitespace(eq + 1);
                        if (strcmp(key, "LANGUAGE_NAME") == 0) {
                            strncpy((*languages)[count].name, value, sizeof((*languages)[count].name) - 1);
                            (*languages)[count].name[sizeof((*languages)[count].name) - 1] = '\0';
                            found = true;
                        }
                    }
                }
                fclose(file);
                if (!found) {
                    strncpy((*languages)[count].name, (*languages)[count].code, sizeof((*languages)[count].name) - 1);
                    (*languages)[count].name[sizeof((*languages)[count].name) - 1] = '\0';
                }
            } else {
                strncpy((*languages)[count].name, (*languages)[count].code, sizeof((*languages)[count].name) - 1);
                (*languages)[count].name[sizeof((*languages)[count].name) - 1] = '\0';
            }
            
            count++;
        }
    }
    
    closedir(dir);
    
    if (count == 0) {
        free(*languages);
        *languages = NULL;
    }
    
    return count;
}

void lang_free_list(LanguageEntry *languages, int count) {
    if (languages) {
        free(languages);
    }
}
