#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include "main.h"
#include "render_utils.h"

#define MAX_ENTRIES 1024
#define MAX_VISIBLE_ITEMS 30

typedef struct {
    char name[MAX_PATH_LENGTH];
    bool is_dir;
} FileEntry;

typedef struct {
    FileEntry entries[MAX_ENTRIES];
    int count;
    int selected_index;
    int scroll_offset;
    uint32_t scroll_start_time;
    int last_selected_index;
    SDL_Texture *cached_scroll_tex;
    int cached_scroll_w;
    int cached_scroll_h;
    TextRender visible_item_cache[MAX_VISIBLE_ITEMS];
    int last_scroll_offset;
} FileBrowser;

FileBrowser* file_browser_create(void);
void file_browser_destroy(FileBrowser *fb);
void file_browser_scan(FileBrowser *fb, const char *dir, const char *root_dir);
void file_browser_render(AppContext *app, FileBrowser *fb);
void file_browser_handle_event(AppContext *app, FileBrowser *fb, SDL_Event *event);
bool file_browser_navigate_up(AppContext *app, FileBrowser *fb);
bool file_browser_navigate_into(AppContext *app, FileBrowser *fb, const char *entry_name);

#endif
