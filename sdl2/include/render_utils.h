#ifndef RENDER_UTILS_H
#define RENDER_UTILS_H

#include "main.h"

typedef struct {
    SDL_Texture *tex;
    int w, h;
} TextRender;

typedef struct {
    const char *start;
    size_t len;
} WrappedLine;

TextRender text_make(AppContext *app, TTF_Font *font, const char *text, SDL_Color color);
void text_draw(TextRender *r, SDL_Renderer *rend, int x, int y);
void text_draw_center(TextRender *r, SDL_Renderer *rend, int cx, int y);
void text_destroy(TextRender *r);
int wrap_text(TTF_Font *font, const char *text, int max_width, WrappedLine **out_lines);

#endif
