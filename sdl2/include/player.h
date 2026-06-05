#ifndef PLAYER_H
#define PLAYER_H

#include "main.h"
#include "render_utils.h"

#define MAX_SPECTRUM_BANDS 256

typedef struct {
    char fullpath[MAX_PATH_LENGTH];
    char type[64];
    char container[64];
    char program[256];
    char title[256];
    char author[256];
    int total_duration_ms;
    int loop_duration_ms;
    bool has_info;
    TextRender cached_title;
    TextRender cached_author;
    TextRender cached_program;
    TextRender cached_type;
    TextRender cached_container;
    bool has_cached_textures;
} SongInfo;

typedef struct {
    int position_ms;
    char state[32];
    uint8_t spectrum[MAX_SPECTRUM_BANDS];
    int spectrum_bands;
    bool has_spectrum;
    bool has_track_state;
    uint32_t track_pos;
    uint32_t track_pattern;
    uint32_t track_line;
    uint32_t track_quirk;
    uint32_t track_channels;
    uint32_t track_tempo;
} PlaybackState;

typedef struct {
    bool is_playing;
    char current_file[MAX_PATH_LENGTH];
    pid_t child_pid;
    int pipe_fd;
    int pty_fd;
    char read_buffer[8192];
    int read_buffer_len;
    SongInfo song_info;
    PlaybackState playback_state;
    char *debug_output;
    int debug_output_len;
    int debug_output_capacity;
    int debug_scroll;
    bool debug_wait_exit;
    uint8_t *bin_buf;
    int bin_buf_len;
    int bin_buf_capacity;
    bool needs_render;
} Player;

Player* player_create(void);
void player_destroy(Player *p);
bool player_play(AppContext *app, Player *p, const char *file_path);
void player_stop(AppContext *app, Player *p);
void player_render(AppContext *app, Player *p);
void player_render_debug_exit(AppContext *app, Player *p);
void player_handle_event(AppContext *app, Player *p, SDL_Event *event);
void player_update(AppContext *app, Player *p);
bool player_has_new_data(Player *p);

#endif
