#ifndef GPTK_H
#define GPTK_H

#include <stddef.h>
#include <stdint.h>

#define GPTK_MAX_ENTRIES 64

typedef struct {
    char keyboard_key[32];
    char handheld_btn[32];
} GptkEntry;

typedef struct {
    GptkEntry entries[GPTK_MAX_ENTRIES];
    int count;
} GptkMap;

int gptk_load(const char *exe_dir);
void gptk_free(void);
const char* gptk_translate(const char *keyboard_key);
void gptk_translate_hint(char *dest, const char *src, size_t dest_size);

#endif
