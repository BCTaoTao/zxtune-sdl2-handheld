/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL_ttf.h>
#include "render_utils.h"

TextRender text_make(AppContext *app, TTF_Font *font, const char *text, SDL_Color color) {
    TextRender r = {NULL, 0, 0};
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text, color);
    if (s) {
        r.tex = SDL_CreateTextureFromSurface(app->renderer, s);
        SDL_QueryTexture(r.tex, NULL, NULL, &r.w, &r.h);
        SDL_FreeSurface(s);
    }
    return r;
}

void text_draw(TextRender *r, SDL_Renderer *rend, int x, int y) {
    if (r && r->tex) {
        SDL_Rect rect = {x, y, r->w, r->h};
        SDL_RenderCopy(rend, r->tex, NULL, &rect);
    }
}

void text_draw_center(TextRender *r, SDL_Renderer *rend, int cx, int y) {
    if (r && r->tex) {
        SDL_Rect rect = {cx - r->w / 2, y, r->w, r->h};
        SDL_RenderCopy(rend, r->tex, NULL, &rect);
    }
}

void text_destroy(TextRender *r) {
    if (r && r->tex) {
        SDL_DestroyTexture(r->tex);
        r->tex = NULL;
        r->w = 0;
        r->h = 0;
    }
}

int wrap_text(TTF_Font *font, const char *text, int max_width, WrappedLine **out_lines) {
    WrappedLine *wrapped = NULL;
    int count = 0;
    int capacity = 0;

    const char *dp = text;
    while (*dp) {
        const char *eol = strchr(dp, '\n');
        size_t line_len;
        if (eol) {
            line_len = eol - dp;
        } else {
            line_len = strlen(dp);
        }

        if (line_len == 0) {
            if (count >= capacity) {
                int new_capacity = capacity == 0 ? 64 : capacity * 2;
                WrappedLine *new_wrapped = (WrappedLine *)realloc(wrapped, sizeof(WrappedLine) * new_capacity);
                if (!new_wrapped) {
                    free(wrapped);
                    *out_lines = NULL;
                    return 0;
                }
                wrapped = new_wrapped;
                capacity = new_capacity;
            }
            wrapped[count].start = dp;
            wrapped[count].len = 0;
            count++;
            if (eol) { dp = eol + 1; } else { break; }
            continue;
        }

        const char *line_start = dp;
        const char *line_end = dp + line_len;

        while (line_start < line_end) {
            const char *last_space = NULL;
            int last_fit_bytes = 0;
            char measure_buf[1024];

            const char *scan = line_start;
            while (scan < line_end) {
                size_t char_len = 1;
                unsigned char c = (unsigned char)*scan;
                if (c >= 0xF0) char_len = 4;
                else if (c >= 0xE0) char_len = 3;
                else if (c >= 0xC0) char_len = 2;

                const char *next = scan + char_len;
                if (next > line_end) next = line_end;

                size_t sub_len = next - line_start;
                if (sub_len >= sizeof(measure_buf)) break;

                memcpy(measure_buf, line_start, sub_len);
                measure_buf[sub_len] = '\0';

                int w;
                if (TTF_SizeUTF8(font, measure_buf, &w, NULL) != 0) break;
                if (w > max_width) break;

                last_fit_bytes = (int)sub_len;
                if (*scan == ' ') last_space = scan;

                scan = next;
            }

            if (scan >= line_end && last_fit_bytes > 0) {
                if (count >= capacity) {
                    int new_capacity = capacity == 0 ? 64 : capacity * 2;
                    WrappedLine *new_wrapped = (WrappedLine *)realloc(wrapped, sizeof(WrappedLine) * new_capacity);
                    if (!new_wrapped) {
                        free(wrapped);
                        *out_lines = NULL;
                        return 0;
                    }
                    wrapped = new_wrapped;
                    capacity = new_capacity;
                }
                wrapped[count].start = line_start;
                wrapped[count].len = line_end - line_start;
                count++;
                break;
            }

            if (last_fit_bytes == 0) {
                size_t char_len = 1;
                unsigned char c = (unsigned char)*line_start;
                if (c >= 0xF0) char_len = 4;
                else if (c >= 0xE0) char_len = 3;
                else if (c >= 0xC0) char_len = 2;

                if (count >= capacity) {
                    int new_capacity = capacity == 0 ? 64 : capacity * 2;
                    WrappedLine *new_wrapped = (WrappedLine *)realloc(wrapped, sizeof(WrappedLine) * new_capacity);
                    if (!new_wrapped) {
                        free(wrapped);
                        *out_lines = NULL;
                        return 0;
                    }
                    wrapped = new_wrapped;
                    capacity = new_capacity;
                }
                wrapped[count].start = line_start;
                wrapped[count].len = char_len;
                count++;
                line_start += char_len;
            } else if (last_space && last_space > line_start) {
                if (count >= capacity) {
                    int new_capacity = capacity == 0 ? 64 : capacity * 2;
                    WrappedLine *new_wrapped = (WrappedLine *)realloc(wrapped, sizeof(WrappedLine) * new_capacity);
                    if (!new_wrapped) {
                        free(wrapped);
                        *out_lines = NULL;
                        return 0;
                    }
                    wrapped = new_wrapped;
                    capacity = new_capacity;
                }
                wrapped[count].start = line_start;
                wrapped[count].len = last_space - line_start;
                count++;
                line_start = last_space + 1;
            } else {
                if (count >= capacity) {
                    int new_capacity = capacity == 0 ? 64 : capacity * 2;
                    WrappedLine *new_wrapped = (WrappedLine *)realloc(wrapped, sizeof(WrappedLine) * new_capacity);
                    if (!new_wrapped) {
                        free(wrapped);
                        *out_lines = NULL;
                        return 0;
                    }
                    wrapped = new_wrapped;
                    capacity = new_capacity;
                }
                wrapped[count].start = line_start;
                wrapped[count].len = last_fit_bytes;
                count++;
                line_start += last_fit_bytes;
            }
        }

        if (eol) { dp = eol + 1; } else { break; }
    }

    *out_lines = wrapped;
    return count;
}
