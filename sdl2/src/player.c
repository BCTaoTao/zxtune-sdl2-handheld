/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pty.h>
#include "player.h"
#include "lang.h"
#include "render_utils.h"
#include "util.h"
#include "gptk.h"

#define FRAME_MAGIC 0x5A
#define FRAME_TYPE_INFO 0x01
#define FRAME_TYPE_STATE 0x02
#define FLAG_HAS_TRACK_STATE 0x01
#define FLAG_HAS_SPECTRUM 0x02

static uint16_t read_u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void read_bin_string(const uint8_t *p, int *offset, int payload_end, char *dest, size_t dest_size) {
    if (*offset + 2 > payload_end) return;
    uint16_t slen = read_u16le(p + *offset);
    *offset += 2;
    if (*offset + slen > payload_end) return;
    size_t copy_len = slen < dest_size - 1 ? slen : dest_size - 1;
    memcpy(dest, p + *offset, copy_len);
    dest[copy_len] = '\0';
    *offset += slen;
}

static void parse_info_frame(Player *p, const uint8_t *payload, int payload_len) {
    memset(&p->song_info, 0, sizeof(SongInfo));
    p->song_info.has_info = true;

    int off = 0;
    int end = payload_len;

    if (off + 4 <= end) { p->song_info.total_duration_ms = (int)read_u32le(payload + off); off += 4; }
    if (off + 4 <= end) { p->song_info.loop_duration_ms = (int)read_u32le(payload + off); off += 4; }
    read_bin_string(payload, &off, end, p->song_info.fullpath, sizeof(p->song_info.fullpath));
    read_bin_string(payload, &off, end, p->song_info.type, sizeof(p->song_info.type));
    read_bin_string(payload, &off, end, p->song_info.container, sizeof(p->song_info.container));
    read_bin_string(payload, &off, end, p->song_info.program, sizeof(p->song_info.program));
    read_bin_string(payload, &off, end, p->song_info.title, sizeof(p->song_info.title));
    read_bin_string(payload, &off, end, p->song_info.author, sizeof(p->song_info.author));

    if (p->song_info.title[0] == '\0') {
        const char *fname = strrchr(p->song_info.fullpath, '/');
        if (fname) fname++;
        else fname = p->song_info.fullpath;
        strncpy(p->song_info.title, fname, sizeof(p->song_info.title) - 1);
        p->song_info.title[sizeof(p->song_info.title) - 1] = '\0';
    }

    p->song_info.has_cached_textures = false;
}

static void parse_state_frame(Player *p, const uint8_t *payload, int payload_len) {
    int off = 0;
    int end = payload_len;

    if (off + 4 <= end) { p->playback_state.position_ms = (int)read_u32le(payload + off); off += 4; }
    if (off + 1 <= end) {
        uint8_t state_val = payload[off++];
        switch (state_val) {
            case 0: strncpy(p->playback_state.state, "playing", sizeof(p->playback_state.state) - 1); break;
            case 1: strncpy(p->playback_state.state, "paused", sizeof(p->playback_state.state) - 1); break;
            case 2: strncpy(p->playback_state.state, "stopped", sizeof(p->playback_state.state) - 1); break;
            default: snprintf(p->playback_state.state, sizeof(p->playback_state.state), "unknown(%d)", state_val); break;
        }
    }

    uint8_t flags = 0;
    if (off + 1 <= end) { flags = payload[off++]; }

    p->playback_state.has_track_state = (flags & FLAG_HAS_TRACK_STATE) != 0;
    p->playback_state.has_spectrum = (flags & FLAG_HAS_SPECTRUM) != 0;

    if (p->playback_state.has_track_state && off + 24 <= end) {
        p->playback_state.track_pos = read_u32le(payload + off); off += 4;
        p->playback_state.track_pattern = read_u32le(payload + off); off += 4;
        p->playback_state.track_line = read_u32le(payload + off); off += 4;
        p->playback_state.track_quirk = read_u32le(payload + off); off += 4;
        p->playback_state.track_channels = read_u32le(payload + off); off += 4;
        p->playback_state.track_tempo = read_u32le(payload + off); off += 4;
    }

    if (p->playback_state.has_spectrum && off + 1 <= end) {
        uint8_t spec_count = payload[off++];
        p->playback_state.spectrum_bands = 0;
        for (int i = 0; i < spec_count && off < end && i < MAX_SPECTRUM_BANDS; i++) {
            p->playback_state.spectrum[p->playback_state.spectrum_bands++] = payload[off++];
        }
    }
}

static int try_parse_binary_frames(Player *p) {
    int parsed = 0;
    while (p->bin_buf_len >= 4) {
        if (p->bin_buf[0] != FRAME_MAGIC) {
            memmove(p->bin_buf, p->bin_buf + 1, p->bin_buf_len - 1);
            p->bin_buf_len--;
            continue;
        }

        uint8_t frame_type = p->bin_buf[1];
        uint16_t payload_len = read_u16le(p->bin_buf + 2);
        int frame_total = 4 + payload_len;

        if (p->bin_buf_len < frame_total) break;

        const uint8_t *payload = p->bin_buf + 4;
        if (frame_type == FRAME_TYPE_INFO) {
            parse_info_frame(p, payload, payload_len);
            parsed++;
        } else if (frame_type == FRAME_TYPE_STATE) {
            parse_state_frame(p, payload, payload_len);
            parsed++;
        }

        int remaining = p->bin_buf_len - frame_total;
        if (remaining > 0) {
            memmove(p->bin_buf, p->bin_buf + frame_total, remaining);
        }
        p->bin_buf_len = remaining;
    }
    return parsed;
}

static bool append_buffer(void **buf_ptr, int *len_ptr, int *cap_ptr,
                           const void *data, size_t data_len, bool null_terminate) {
    if (*len_ptr + (int)data_len >= *cap_ptr) {
        int new_cap = *cap_ptr == 0 ? 8192 : *cap_ptr * 2;
        while (*len_ptr + (int)data_len >= new_cap) new_cap *= 2;
        void *new_buf = realloc(*buf_ptr, new_cap);
        if (!new_buf) return false;
        *buf_ptr = new_buf;
        *cap_ptr = new_cap;
    }
    if (*len_ptr + (int)data_len < *cap_ptr) {
        memcpy((char *)(*buf_ptr) + *len_ptr, data, data_len);
        *len_ptr += data_len;
        if (null_terminate) ((char *)(*buf_ptr))[*len_ptr] = '\0';
    }
    return true;
}

static void append_bin_data(Player *p, const uint8_t *data, size_t len) {
    append_buffer((void **)&p->bin_buf, &p->bin_buf_len, &p->bin_buf_capacity,
                  data, len, false);
}

static void extract_json_string(const char *line, const char *key, char *dest, size_t dest_size) {
    const char *pos = strstr(line, key);
    if (pos) {
        pos += strlen(key);
        const char *end = strchr(pos, '\"');
        if (end) {
            size_t len = end - pos;
            if (len >= dest_size) len = dest_size - 1;
            memcpy(dest, pos, len);
            dest[len] = '\0';
        }
    }
}

static void parse_json_line(Player *p, const char *line) {
    if (strstr(line, "\"type\":\"info\"") != NULL) {
        memset(&p->song_info, 0, sizeof(SongInfo));
        p->song_info.has_info = true;

        extract_json_string(line, "\"fullpath\":\"", p->song_info.fullpath, sizeof(p->song_info.fullpath));

        const char *type1 = strstr(line, "\"type\":\"");
        if (type1) {
            type1 += 8;
            const char *end1 = strchr(type1, '\"');
            if (end1) {
                const char *type2 = strstr(end1, "\"type\":\"");
                if (type2) {
                    extract_json_string(type2, "\"type\":\"", p->song_info.type, sizeof(p->song_info.type));
                }
            }
        }

        extract_json_string(line, "\"container\":\"", p->song_info.container, sizeof(p->song_info.container));
        extract_json_string(line, "\"program\":\"", p->song_info.program, sizeof(p->song_info.program));
        extract_json_string(line, "\"title\":\"", p->song_info.title, sizeof(p->song_info.title));
        extract_json_string(line, "\"author\":\"", p->song_info.author, sizeof(p->song_info.author));

        const char *total_duration = strstr(line, "\"total_duration_ms\":");
        if (total_duration) {
            p->song_info.total_duration_ms = atoi(total_duration + 20);
        }

        const char *loop_duration = strstr(line, "\"loop_duration_ms\":");
        if (loop_duration) {
            p->song_info.loop_duration_ms = atoi(loop_duration + 19);
        }

        if (p->song_info.title[0] == '\0') {
            const char *fname = strrchr(p->song_info.fullpath, '/');
            if (fname) fname++;
            else fname = p->song_info.fullpath;
            strncpy(p->song_info.title, fname, sizeof(p->song_info.title) - 1);
            p->song_info.title[sizeof(p->song_info.title) - 1] = '\0';
        }

        p->song_info.has_cached_textures = false;
    } else if (strstr(line, "\"type\":\"state\"") != NULL) {
        const char *position = strstr(line, "\"position_ms\":");
        if (position) {
            p->playback_state.position_ms = atoi(position + 14);
        }

        extract_json_string(line, "\"state\":\"", p->playback_state.state, sizeof(p->playback_state.state));

        const char *spectrum = strstr(line, "\"spectrum\":[");
        if (spectrum) {
            spectrum += 12;
            p->playback_state.has_spectrum = true;
            p->playback_state.spectrum_bands = 0;

            const char *ptr = spectrum;
            while (*ptr != ']' && *ptr != '\0' && p->playback_state.spectrum_bands < MAX_SPECTRUM_BANDS) {
                while (*ptr == ' ' || *ptr == ',') ptr++;
                if (*ptr < '0' || *ptr > '9') break;
                p->playback_state.spectrum[p->playback_state.spectrum_bands++] = atoi(ptr);
                while (*ptr >= '0' && *ptr <= '9') ptr++;
            }
        } else {
            p->playback_state.has_spectrum = false;
        }
    }
}

static void append_debug_output(Player *p, const char *data, size_t len) {
    append_buffer((void **)&p->debug_output, &p->debug_output_len, &p->debug_output_capacity,
                  data, len, true);
}

Player* player_create(void) {
    Player *p = (Player *)malloc(sizeof(Player));
    if (!p) return NULL;

    memset(p, 0, sizeof(Player));
    p->child_pid = -1;
    p->pipe_fd = -1;
    p->pty_fd = -1;
    p->debug_output = NULL;
    p->debug_output_len = 0;
    p->debug_output_capacity = 0;
    p->debug_scroll = 0;
    p->debug_wait_exit = false;
    p->bin_buf = NULL;
    p->bin_buf_len = 0;
    p->bin_buf_capacity = 0;
    p->needs_render = true;

    return p;
}

void player_destroy(Player *p) {
    if (p) {
        free(p->debug_output);
        free(p->bin_buf);
        free(p);
    }
}

bool player_play(AppContext *app, Player *p, const char *file_path) {
    if (p->is_playing) {
        player_stop(app, p);
    }

    strncpy(p->current_file, file_path, sizeof(p->current_file) - 1);
    p->current_file[sizeof(p->current_file) - 1] = '\0';

    memset(&p->song_info, 0, sizeof(SongInfo));
    memset(&p->playback_state, 0, sizeof(PlaybackState));

    free(p->debug_output);
    p->debug_output = NULL;
    p->debug_output_len = 0;
    p->debug_output_capacity = 0;
    p->debug_scroll = 0;
    p->debug_wait_exit = false;

    free(p->bin_buf);
    p->bin_buf = NULL;
    p->bin_buf_len = 0;
    p->bin_buf_capacity = 0;

    int json_pipefd[2];
    if (pipe(json_pipefd) == -1) {
        perror("pipe failed");
        return false;
    }

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (pid == -1) {
        perror("forkpty failed");
        close(json_pipefd[0]);
        close(json_pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(json_pipefd[0]);

        int max_args = 32;
        char *args[max_args];
        int arg_idx = 0;

        args[arg_idx++] = (char *)app->player_path;
        args[arg_idx++] = (char *)file_path;

        char output_fd_arg[32];
        snprintf(output_fd_arg, sizeof(output_fd_arg), "--output-fd=%d", json_pipefd[1]);
        if (arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup(output_fd_arg);
        }

        if (strcmp(app->settings.zx_output_format, "binary") == 0 && arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup("--output-format=binary");
        }

        char *tmp = my_strdup(app->settings.zx_backend);
        char *token = strtok(tmp, " ");
        while (token != NULL && arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup(token);
            token = strtok(NULL, " ");
        }
        free(tmp);

        if (app->settings.zx_freq[0] != '\0' && arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup(app->settings.zx_freq);
        }

        if (app->settings.zx_fps[0] != '\0' && arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup(app->settings.zx_fps);
        }

        if (app->settings.zx_seekstep[0] != '\0' && arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup(app->settings.zx_seekstep);
        }

        if (app->settings.zx_spectrum_size[0] != '\0' && arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup(app->settings.zx_spectrum_size);
        }

        tmp = my_strdup(app->settings.zx_flags);
        token = strtok(tmp, " ");
        while (token != NULL && arg_idx < max_args - 1) {
            args[arg_idx++] = my_strdup(token);
            token = strtok(NULL, " ");
        }
        free(tmp);

        args[arg_idx] = NULL;

        execv(app->player_path, args);
        perror("execv failed");
        _exit(1);
    }

    close(json_pipefd[1]);

    p->child_pid = pid;
    p->pipe_fd = json_pipefd[0];
    p->pty_fd = master_fd;
    p->read_buffer_len = 0;

    int flags = fcntl(p->pipe_fd, F_GETFL, 0);
    fcntl(p->pipe_fd, F_SETFL, flags | O_NONBLOCK);

    flags = fcntl(p->pty_fd, F_GETFL, 0);
    fcntl(p->pty_fd, F_SETFL, flags | O_NONBLOCK);

    p->is_playing = true;
    p->needs_render = true;

    return true;
}

void player_stop(AppContext *app, Player *p) {
    (void)app;

    if (p->is_playing) {
        p->is_playing = false;

        if (p->pty_fd >= 0) {
            close(p->pty_fd);
            p->pty_fd = -1;
        }

        if (p->child_pid > 0) {
            kill(p->child_pid, SIGTERM);
            int attempts = 0;
            while (attempts < 50) {
                int status;
                if (waitpid(p->child_pid, &status, WNOHANG) > 0) break;
                struct timespec ts = {0, 10000000};
                nanosleep(&ts, NULL);
                attempts++;
            }
            if (attempts >= 50) {
                kill(p->child_pid, SIGKILL);
                waitpid(p->child_pid, NULL, 0);
            }
            p->child_pid = -1;
        }

        if (p->pipe_fd >= 0) {
            close(p->pipe_fd);
            p->pipe_fd = -1;
        }
        p->read_buffer_len = 0;

        text_destroy(&p->song_info.cached_title);
        text_destroy(&p->song_info.cached_author);
        text_destroy(&p->song_info.cached_program);
        text_destroy(&p->song_info.cached_type);
        text_destroy(&p->song_info.cached_container);
        p->song_info.has_cached_textures = false;
    }
}

void player_update(AppContext *app, Player *p) {
    if (!p->is_playing || p->pipe_fd < 0) return;

    bool is_binary = strcmp(app->settings.zx_output_format, "binary") == 0;
    bool had_data = false;

    while (1) {
        char buf[4096];
        ssize_t n = read(p->pipe_fd, buf, sizeof(buf));
        if (n <= 0) break;
        had_data = true;

        if (is_binary) {
            append_bin_data(p, (const uint8_t *)buf, n);
        } else {
            if (p->read_buffer_len + n < (int)sizeof(p->read_buffer)) {
                memcpy(p->read_buffer + p->read_buffer_len, buf, n);
                p->read_buffer_len += n;
            }
        }
    }

    if (is_binary) {
        try_parse_binary_frames(p);
    } else {
        char *newline;
        while ((newline = memchr(p->read_buffer, '\n', p->read_buffer_len)) != NULL) {
            size_t line_len = newline - p->read_buffer;
            p->read_buffer[line_len] = '\0';
            parse_json_line(p, p->read_buffer);

            size_t remaining = p->read_buffer_len - line_len - 1;
            if (remaining > 0) {
                memmove(p->read_buffer, p->read_buffer + line_len + 1, remaining);
            }
            p->read_buffer_len = remaining;
        }
    }

    if (p->pty_fd >= 0) {
        char drain[4096];
        ssize_t dn;
        while ((dn = read(p->pty_fd, drain, sizeof(drain))) > 0) {
            if (strcmp(app->settings.zx_debug_mode, "on") == 0) {
                append_debug_output(p, drain, dn);
            }
        }
    }

    if (p->child_pid > 0) {
        int status;
        if (waitpid(p->child_pid, &status, WNOHANG) > 0) {
            p->is_playing = false;
            p->child_pid = -1;
            if (p->pty_fd >= 0) {
                char final_drain[4096];
                ssize_t fdn;
                while ((fdn = read(p->pty_fd, final_drain, sizeof(final_drain))) > 0) {
                    if (strcmp(app->settings.zx_debug_mode, "on") == 0) {
                        append_debug_output(p, final_drain, fdn);
                    }
                }
                close(p->pty_fd);
                p->pty_fd = -1;
            }
            if (p->pipe_fd >= 0) {
                close(p->pipe_fd);
                p->pipe_fd = -1;
            }
            p->read_buffer_len = 0;
            p->bin_buf_len = 0;
            p->needs_render = true;
            if (strcmp(app->settings.zx_debug_mode, "on") == 0 && p->debug_output && p->debug_output_len > 0) {
                p->debug_wait_exit = true;
                app->current_screen = SCREEN_DEBUG_EXIT;
            } else {
                app->current_screen = SCREEN_FILE_BROWSER;
            }
            return;
        }
    }

    if (had_data) {
        p->needs_render = true;
    }
}

bool player_has_new_data(Player *p) {
    if (p->needs_render) {
        p->needs_render = false;
        return true;
    }
    return false;
}

static void write_to_child(Player *p, const char *data, size_t len) {
    if (p->pty_fd >= 0) {
        write(p->pty_fd, data, len);
    }
}

void player_handle_event(AppContext *app, Player *p, SDL_Event *event) {
    (void)app;

    if (!p->is_playing || p->child_pid <= 0) return;

    if (event->type == SDL_KEYDOWN) {
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            bool has_debug = strcmp(app->settings.zx_debug_mode, "on") == 0
                             && p->debug_output && p->debug_output_len > 0;
            player_stop(app, p);
            if (has_debug) {
                p->debug_wait_exit = true;
                app->current_screen = SCREEN_DEBUG_EXIT;
            } else {
                app->current_screen = SCREEN_FILE_BROWSER;
            }
            return;
        }

        switch (event->key.keysym.sym) {
            case SDLK_SPACE:
                write_to_child(p, " ", 1);
                break;
            case SDLK_LEFT:
                write_to_child(p, "\033[D", 3);
                break;
            case SDLK_RIGHT:
                write_to_child(p, "\033[C", 3);
                break;
            case SDLK_UP:
                write_to_child(p, "\033[A", 3);
                break;
            case SDLK_DOWN:
                write_to_child(p, "\033[B", 3);
                break;
            case SDLK_PAGEUP:
                write_to_child(p, "\033[5~", 4);
                break;
            case SDLK_PAGEDOWN:
                write_to_child(p, "\033[6~", 4);
                break;
            case SDLK_HOME:
                write_to_child(p, "\033[H", 3);
                break;
            case SDLK_END:
                write_to_child(p, "\033[F", 3);
                break;
            case SDLK_RETURN:
                write_to_child(p, "\n", 1);
                break;
            case SDLK_BACKSPACE:
                write_to_child(p, "\x7f", 1);
                break;
            case SDLK_DELETE:
                write_to_child(p, "\033[3~", 4);
                break;
            case SDLK_TAB:
                write_to_child(p, "\t", 1);
                break;
            default:
                if (event->key.keysym.sym >= 32 && event->key.keysym.sym < 127) {
                    char ch = (char)event->key.keysym.sym;
                    SDL_Keymod mod = SDL_GetModState();
                    if (mod & KMOD_CTRL) {
                        if (ch >= 'a' && ch <= 'z') {
                            char ctrl_ch = ch - 'a' + 1;
                            write_to_child(p, &ctrl_ch, 1);
                        }
                    } else {
                        write_to_child(p, &ch, 1);
                    }
                }
                break;
        }
    }
}

void player_render(AppContext *app, Player *p) {
    SDL_Color bg_color = {20, 40, 80, 255};
    SDL_SetRenderDrawColor(app->renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_RenderClear(app->renderer);

    SDL_Color text_color = {200, 220, 255, 255};
    SDL_Color highlight_color = {255, 255, 255, 255};
    SDL_Color accent_color = {0, 200, 255, 255};

    int y = 30;

    if (p->song_info.has_info) {
        if (!p->song_info.has_cached_textures) {
            text_destroy(&p->song_info.cached_title);
            text_destroy(&p->song_info.cached_author);
            text_destroy(&p->song_info.cached_program);
            text_destroy(&p->song_info.cached_type);
            text_destroy(&p->song_info.cached_container);

            if (strlen(p->song_info.title) > 0) {
                if (strlen(p->song_info.container) > 0) {
                    char container_tag[68];
                    snprintf(container_tag, sizeof(container_tag), "[%s]", p->song_info.container);
                    p->song_info.cached_container = text_make(app, app->font_small, container_tag, text_color);

                    int margin = 20;
                    int gap = 10;
                    int container_x = app->window_width - margin - p->song_info.cached_container.w;
                    int max_title_w = container_x - margin - gap;

                    char title_buf[256];
                    const char *title_src = p->song_info.title;
                    int tw;
                    TTF_SizeUTF8(app->font, title_src, &tw, NULL);

                    if (tw <= max_title_w) {
                        snprintf(title_buf, sizeof(title_buf), "%s", title_src);
                    } else {
                        int suffix_w;
                        TTF_SizeUTF8(app->font, "...", &suffix_w, NULL);
                        int target_w = max_title_w - suffix_w;

                        int lo = 0, hi = (int)strlen(title_src);
                        while (lo < hi) {
                            int mid = (lo + hi + 1) / 2;
                            int safe_mid = mid;
                            while (safe_mid > 0 && ((unsigned char)title_src[safe_mid] & 0xC0) == 0x80)
                                safe_mid--;
                            if (safe_mid <= 0) { hi = 0; break; }
                            char test[512];
                            snprintf(test, sizeof(test), "%.*s", safe_mid, title_src);
                            int test_w;
                            TTF_SizeUTF8(app->font, test, &test_w, NULL);
                            if (test_w <= target_w) lo = mid;
                            else hi = safe_mid - 1;
                        }
                        int safe_lo = lo;
                        while (safe_lo > 0 && ((unsigned char)title_src[safe_lo] & 0xC0) == 0x80)
                            safe_lo--;
                        snprintf(title_buf, sizeof(title_buf), "%.*s...", safe_lo, title_src);
                    }

                    p->song_info.cached_title = text_make(app, app->font, title_buf, highlight_color);
                } else {
                    int max_title_w = app->window_width - 40;
                    int tw;
                    TTF_SizeUTF8(app->font, p->song_info.title, &tw, NULL);

                    if (tw <= max_title_w) {
                        p->song_info.cached_title = text_make(app, app->font, p->song_info.title, highlight_color);
                    } else {
                        char title_buf[256];
                        int suffix_w;
                        TTF_SizeUTF8(app->font, "...", &suffix_w, NULL);
                        int target_w = max_title_w - suffix_w;

                        const char *title_src = p->song_info.title;
                        int lo = 0, hi = (int)strlen(title_src);
                        while (lo < hi) {
                            int mid = (lo + hi + 1) / 2;
                            int safe_mid = mid;
                            while (safe_mid > 0 && ((unsigned char)title_src[safe_mid] & 0xC0) == 0x80)
                                safe_mid--;
                            if (safe_mid <= 0) { hi = 0; break; }
                            char test[512];
                            snprintf(test, sizeof(test), "%.*s", safe_mid, title_src);
                            int test_w;
                            TTF_SizeUTF8(app->font, test, &test_w, NULL);
                            if (test_w <= target_w) lo = mid;
                            else hi = safe_mid - 1;
                        }
                        int safe_lo = lo;
                        while (safe_lo > 0 && ((unsigned char)title_src[safe_lo] & 0xC0) == 0x80)
                            safe_lo--;
                        snprintf(title_buf, sizeof(title_buf), "%.*s...", safe_lo, title_src);
                        p->song_info.cached_title = text_make(app, app->font, title_buf, highlight_color);
                    }
                }
            }

            if (strlen(p->song_info.author) > 0) {
                char author_text[MAX_TEXT_LENGTH];
                snprintf(author_text, sizeof(author_text), "%s: %s", lang_get(app, LANG_AUTHOR), p->song_info.author);
                p->song_info.cached_author = text_make(app, app->font_small, author_text, text_color);
            }

            if (strlen(p->song_info.program) > 0) {
                char program_text[MAX_TEXT_LENGTH];
                snprintf(program_text, sizeof(program_text), "%s: %s", lang_get(app, LANG_PROGRAM), p->song_info.program);
                p->song_info.cached_program = text_make(app, app->font_small, program_text, text_color);
            }

            if (strlen(p->song_info.type) > 0) {
                char type_text[MAX_TEXT_LENGTH];
                snprintf(type_text, sizeof(type_text), "%s: %s", lang_get(app, LANG_TYPE), p->song_info.type);
                p->song_info.cached_type = text_make(app, app->font_small, type_text, text_color);
            }

            p->song_info.has_cached_textures = true;
        }

        if (strlen(p->song_info.title) > 0) {
            if (strlen(p->song_info.container) > 0) {
                int margin = 20;
                int container_x = app->window_width - margin - p->song_info.cached_container.w;

                y += p->song_info.cached_title.h + 10;
                int title_y = y - p->song_info.cached_title.h - 10;
                text_draw(&p->song_info.cached_title, app->renderer, margin, title_y);

                int container_y = title_y + p->song_info.cached_title.h - p->song_info.cached_container.h;
                text_draw(&p->song_info.cached_container, app->renderer, container_x, container_y);
            } else {
                y += p->song_info.cached_title.h + 10;
                text_draw(&p->song_info.cached_title, app->renderer, 20, y - p->song_info.cached_title.h - 10);
            }
        }

        if (strlen(p->song_info.author) > 0) {
            y += p->song_info.cached_author.h + 5;
            text_draw(&p->song_info.cached_author, app->renderer, 20, y - p->song_info.cached_author.h - 5);
        }

        if (strlen(p->song_info.program) > 0) {
            y += p->song_info.cached_program.h + 5;
            text_draw(&p->song_info.cached_program, app->renderer, 20, y - p->song_info.cached_program.h - 5);
        }

        if (strlen(p->song_info.type) > 0) {
            y += p->song_info.cached_type.h + 5;
            text_draw(&p->song_info.cached_type, app->renderer, 20, y - p->song_info.cached_type.h - 5);
        }

    } else {
        TextRender opening = text_make(app, app->font, lang_get(app, LANG_OPENING), highlight_color);
        text_draw_center(&opening, app->renderer, app->window_width / 2, y);
        y += opening.h + 10;
        text_destroy(&opening);

        const char *filename = strrchr(p->current_file, '/');
        if (!filename) filename = p->current_file;
        else filename++;

        int max_fname_w = app->window_width - 40;
        char fname_buf[MAX_PATH_LENGTH];
        int fname_w;
        TTF_SizeUTF8(app->font_small, filename, &fname_w, NULL);
        if (fname_w <= max_fname_w) {
            snprintf(fname_buf, sizeof(fname_buf), "%s", filename);
        } else {
            int suffix_w;
            TTF_SizeUTF8(app->font_small, "...", &suffix_w, NULL);
            int target_w = max_fname_w - suffix_w;

            int lo = 0, hi = (int)strlen(filename);
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                int safe_mid = mid;
                while (safe_mid > 0 && ((unsigned char)filename[safe_mid] & 0xC0) == 0x80)
                    safe_mid--;
                if (safe_mid <= 0) { hi = 0; break; }
                char test[512];
                snprintf(test, sizeof(test), "%.*s", safe_mid, filename);
                int test_w;
                TTF_SizeUTF8(app->font_small, test, &test_w, NULL);
                if (test_w <= target_w) lo = mid;
                else hi = safe_mid - 1;
            }
            int safe_lo = lo;
            while (safe_lo > 0 && ((unsigned char)filename[safe_lo] & 0xC0) == 0x80)
                safe_lo--;
            snprintf(fname_buf, sizeof(fname_buf), "%.*s...", safe_lo, filename);
        }

        TextRender filename_r = text_make(app, app->font_small, fname_buf, text_color);
        text_draw_center(&filename_r, app->renderer, app->window_width / 2, y);
        y += filename_r.h + 20;
        text_destroy(&filename_r);
    }

    int progress_y = y;
    int progress_height = 30;
    int progress_width = app->window_width - 40;
    SDL_Color progress_bg = {40, 60, 100, 255};
    SDL_Rect progress_bg_rect = {20, progress_y, progress_width, progress_height};
    SDL_SetRenderDrawColor(app->renderer, progress_bg.r, progress_bg.g, progress_bg.b, progress_bg.a);
    SDL_RenderFillRect(app->renderer, &progress_bg_rect);

    if (p->song_info.total_duration_ms > 0) {
        float progress = (float)p->playback_state.position_ms / p->song_info.total_duration_ms;
        if (progress > 1.0f) progress = 1.0f;
        int fill_width = (int)(progress_width * progress);
        SDL_Rect progress_fill_rect = {20, progress_y, fill_width, progress_height};
        SDL_SetRenderDrawColor(app->renderer, accent_color.r, accent_color.g, accent_color.b, accent_color.a);
        SDL_RenderFillRect(app->renderer, &progress_fill_rect);

        int total_sec = p->song_info.total_duration_ms / 1000;
        int current_sec = p->playback_state.position_ms / 1000;
        char time_text[64];
        snprintf(time_text, sizeof(time_text), "%02d:%02d / %02d:%02d",
                 current_sec / 60, current_sec % 60, total_sec / 60, total_sec % 60);
        TextRender r = text_make(app, app->font_small, time_text, highlight_color);
        text_draw(&r, app->renderer, 20 + (progress_width - r.w) / 2, progress_y + (progress_height - r.h) / 2);
        text_destroy(&r);
    }

    y = progress_y + progress_height + 20;

    if (p->playback_state.has_spectrum && p->playback_state.spectrum_bands > 0) {
        int spectrum_height = app->window_height - y - 80;
        if (spectrum_height > 50) {
            int band_width = (app->window_width - 40) / p->playback_state.spectrum_bands;
            if (band_width < 2) band_width = 2;
            int extra = (app->window_width - 40) - band_width * p->playback_state.spectrum_bands;
            int x = 20;
            for (int i = 0; i < p->playback_state.spectrum_bands; i++) {
                int height = (p->playback_state.spectrum[i] * spectrum_height) / 255;
                if (height < 1) height = 1;

                uint8_t r = (uint8_t)(p->playback_state.spectrum[i] * 0.8f);
                uint8_t g = (uint8_t)(p->playback_state.spectrum[i] * 0.4f + 100);
                uint8_t b = 255;
                SDL_SetRenderDrawColor(app->renderer, r, g, b, 255);

                int this_w = band_width + (i < extra ? 1 : 0);
                SDL_Rect band_rect = {x, y + spectrum_height - height, this_w - 1, height};
                SDL_RenderFillRect(app->renderer, &band_rect);
                x += this_w;
            }
        }
    }

    char footer_left[MAX_TEXT_LENGTH];
    char footer_right[MAX_TEXT_LENGTH];
    char footer_left_translated[MAX_TEXT_LENGTH];
    char footer_right_translated[MAX_TEXT_LENGTH];
    const char *pause_label = (strcmp(p->playback_state.state, "paused") == 0) ? lang_get(app, LANG_RESUME) : lang_get(app, LANG_PAUSE);
    snprintf(footer_left, sizeof(footer_left), "[Space] %s  [Enter] %s", lang_get(app, LANG_KEY_NEXT), pause_label);
    snprintf(footer_right, sizeof(footer_right), "%s[← →] %s  [↑ ↓] %s",
             app->settings.loop ? "R  " : "", lang_get(app, LANG_KEY_SEEK), lang_get(app, LANG_KEY_VOLUME));
    gptk_translate_hint(footer_left_translated, footer_left, sizeof(footer_left_translated));
    gptk_translate_hint(footer_right_translated, footer_right, sizeof(footer_right_translated));

    SDL_Color footer_bg_color = {FOOTER_BG_R, FOOTER_BG_G, FOOTER_BG_B, FOOTER_BG_A};
    SDL_Rect footer_bg_rect = {0, app->window_height - 45, app->window_width, 45};
    SDL_SetRenderDrawColor(app->renderer, footer_bg_color.r, footer_bg_color.g, footer_bg_color.b, footer_bg_color.a);
    SDL_RenderFillRect(app->renderer, &footer_bg_rect);

    TextRender fl = text_make(app, app->font_small, footer_left_translated, text_color);
    text_draw(&fl, app->renderer, 15, app->window_height - 40);
    text_destroy(&fl);

    TextRender fr = text_make(app, app->font_small, footer_right_translated, text_color);
    text_draw(&fr, app->renderer, app->window_width - fr.w - 15, app->window_height - 40);
    text_destroy(&fr);
}

void player_render_debug_exit(AppContext *app, Player *p) {
    SDL_Color bg_color = {0, 0, 0, 255};
    SDL_SetRenderDrawColor(app->renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_RenderClear(app->renderer);

    SDL_Color title_color = {255, 255, 0, 255};
    SDL_Color debug_color = {0, 255, 0, 255};
    SDL_Color prompt_color = {255, 100, 100, 255};

    TextRender title_r = text_make(app, app->font, "Debug Output", title_color);
    text_draw(&title_r, app->renderer, 20, 10);
    text_destroy(&title_r);

    int debug_y = 50;
    int footer_h = 50;
    int debug_h = app->window_height - debug_y - footer_h;
    int line_h = 20;
    int visible_lines = debug_h / line_h;
    if (visible_lines < 1) visible_lines = 1;
    int max_text_width = app->window_width - 40;

    if (p->debug_output && p->debug_output_len > 0) {
        WrappedLine *wrapped = NULL;
        int wrapped_count = wrap_text(app->font_small, p->debug_output, max_text_width, &wrapped);

        if (p->debug_scroll + visible_lines > wrapped_count) {
            p->debug_scroll = wrapped_count - visible_lines;
        }
        if (p->debug_scroll < 0) p->debug_scroll = 0;

        int drawn = 0;
        for (int i = p->debug_scroll; i < wrapped_count && drawn < visible_lines; i++) {
            char line_buf[1024];
            size_t len = wrapped[i].len;
            if (len >= sizeof(line_buf)) len = sizeof(line_buf) - 1;
            memcpy(line_buf, wrapped[i].start, len);
            line_buf[len] = '\0';
            TextRender r = text_make(app, app->font_small, line_buf, debug_color);
            text_draw(&r, app->renderer, 20, debug_y + drawn * line_h);
            text_destroy(&r);
            drawn++;
        }

        free(wrapped);
    }

    SDL_Color footer_bg = {20, 20, 40, 255};
    SDL_Rect footer_rect = {0, app->window_height - footer_h, app->window_width, footer_h};
    SDL_SetRenderDrawColor(app->renderer, footer_bg.r, footer_bg.g, footer_bg.b, footer_bg.a);
    SDL_RenderFillRect(app->renderer, &footer_rect);

    char prompt[MAX_TEXT_LENGTH];
    snprintf(prompt, sizeof(prompt), "%s", lang_get(app, LANG_DEBUG_EXIT_PROMPT));
    TextRender pr = text_make(app, app->font, prompt, prompt_color);
    text_draw_center(&pr, app->renderer, app->window_width / 2, app->window_height - footer_h + (footer_h - pr.h) / 2);
    text_destroy(&pr);
}
