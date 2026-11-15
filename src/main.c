/*
 * EXWord Explorer - full-screen 528x320 native filesystem explorer.
 *
 * Uses gint for display and keys, and the EX-word OS filesystem API through
 * gint_world_switch. Directory windows and hex buffers are fixed-size, so
 * the program can browse directories/files much larger than its RAM.
 */
#include "explorer_fs.h"
#include "explorer_usb.h"
#include "preview.h"
#include <gint/config.h>
#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/exword.h>
#include <gint/drivers/touch.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if !GINT_HW_EX
#error This program targets EX-word. Build with the EXWORD toolchain.
#endif

enum { PAGE_LINES = EX_ROWS, HEX_COLS = 12, HEX_PAGE = HEX_COLS * PAGE_LINES };
enum { SCREEN_VOLUMES, SCREEN_DIRECTORY, SCREEN_HEX, SCREEN_IMAGE, SCREEN_FONT };
enum { DIALOG_NONE, DIALOG_MENU, DIALOG_NAME, DIALOG_CONFIRM };
enum { ACT_DELETE, ACT_RENAME, ACT_NEW_FILE, ACT_NEW_FOLDER,
       ACT_COPY, ACT_CUT, ACT_PASTE, ACT_PREVIEW_BITMAP,
       ACT_PREVIEW_CJF, ACT_PREVIEW_RLE565, ACT_EDIT };
static const char *volumes[] = {
    "drv0", "sys0", "data0", "data1", "data2",
    "crd0", "crd1", "crd2"
};
#define VOLUME_COUNT ((int)(sizeof volumes / sizeof volumes[0]))

typedef struct {
    int screen, dialog, action, menu_choice;
    int selected, top, total;
    int volume_selected;
    int parents_selected[32], parents_top[32], depth;
    ex_entry_t rows[EX_ROWS];
    char directory[EX_PATH_MAX];
    char file[EX_PATH_MAX];
    char source[EX_PATH_MAX];
    char clipboard[EX_PATH_MAX], clip_name[EX_NAME_MAX];
    int clipboard_cut, clipboard_valid;
    char input[EX_NAME_MAX];
    int input_len;
    char status[100];

    int file_size, byte_offset, page_base, loaded;
    uint8_t hex[HEX_PAGE];
    int editing, digits, pending_byte;
} app_t;
static app_t app;

static const char *actions[] = {
    "Delete", "Rename", "Create file", "Create folder",
    "Copy", "Cut", "Paste", "View bitmap", "Preview CJF font",
    "View RLE565 (528x320)"
};
#define ACTION_COUNT 10

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }
static int is_press(key_event_t e)
{
    return e.type == KEYEV_DOWN || e.type == KEYEV_HOLD;
}
static void status_rc(const char *operation, int rc)
{
    snprintf(app.status, sizeof app.status, "%s: error %d", operation, rc);
}
static void status_message(const char *message)
{
    snprintf(app.status, sizeof app.status, "%s", message);
}
static void copy_string(char *dest, size_t max, const char *src)
{
    if(max == 0) return;
    size_t n = strlen(src);
    if(n >= max) n = max-1;
    memcpy(dest, src, n);
    dest[n] = 0;
}
/* Render arbitrary byte filenames safely, without changing their actual
 * bytes. Non-printable and non-ASCII display as '?', not as terminators. */
static void printable(char *out, size_t capacity, const char *input,
    int shown)
{
    if(capacity == 0) return;
    size_t full = strlen(input), i = 0, start = 0;
    if(shown < 4) shown = 4;
    if(full > (size_t)shown) {
        start = full - (size_t)shown + 3;
        out[i++] = '.';
        out[i++] = '.';
        out[i++] = '.';
    }
    for(size_t n = start; n < full && i + 1 < capacity &&
        i < (size_t)shown; n++) {
        unsigned char ch = (unsigned char)input[n];
        out[i++] = (ch >= 32 && ch < 127) ? ch : '?';
    }
    out[i] = 0;
}
static void box_fill(int x1, int y1, int x2, int y2, int color)
{
    drect(x1, y1, x2, y2, color);
}
static void header(const char *path, const char *right)
{
    char visible[100];
    int max = right && *right ? 48 : 64;
    box_fill(0, 0, 527, 30, C_RGB(3, 9, 16));
    printable(visible, sizeof visible, path, max);
    dtext(8, 9, C_WHITE, visible);
    if(right && *right) {
        dtext(445, 9, C_WHITE, "|");
        dtext(465, 9, C_WHITE, right);
    }
}
static void loading(const char *name)
{
    char label[80];
    dclear(C_WHITE);
    header(name, "");
    dtext(220, 143, C_BLACK, "Loading...");
    dupdate();
}
static void human_size(int size, char out[8])
{
    if(size < 1024) snprintf(out, 8, "%d", size);
    else if(size < 10*1024)
        snprintf(out, 8, "%d.%dk", size/1024, ((size%1024)*10)/1024);
    else if(size < 1000*1024)
        snprintf(out, 8, "%dk", size/1024);
    else if(size < 10*1024*1024)
        snprintf(out, 8, "%d.%dM", size/(1024*1024),
            ((size%(1024*1024))*10)/(1024*1024));
    else if(size < 1000*1024*1024)
        snprintf(out, 8, "%dM", size/(1024*1024));
    else snprintf(out, 8, "%d.%dG",
            size/(1024*1024*1024),
            ((size%(1024*1024*1024))*10)/(1024*1024*1024));
}
static void footer(const char *hint)
{
    char text[110];
    box_fill(0, 296, 527, 319, C_RGB(4, 10, 17));
    if(app.status[0]) {
        printable(text, sizeof text, app.status, 65);
        dtext(8, 301, C_WHITE, text);
    }
    else {
        dtext(8, 301, C_WHITE, hint);
    }
}
static void draw_volumes(void)
{
    char label[48];
    header("EXWord Explorer /", "");
    for(int i=0; i<=VOLUME_COUNT; i++) {
        int y = 44 + i * 19;
        if(i == app.selected) box_fill(3, y-2, 523, y+17, C_RGB(23, 29, 31));
        if(i==VOLUME_COUNT) snprintf(label, sizeof label, "[USB]  Remote filesystem");
        else snprintf(label, sizeof label, "[DISK]  %s", volumes[i]);
        dtext(12, y+3, C_BLACK, label);
        dtext(345, y+3, C_DARK,
            i==VOLUME_COUNT ? "EXE start" : "EXE to browse");
    }
    footer("EXE open   ESC exit   ACON exit");
}
static void draw_directory(void)
{
    char label[100];
    header(app.directory, "");
    dtext(10, 34, C_DARK, "TYPE");
    dtext(64, 34, C_DARK, "NAME");
    snprintf(label, sizeof label, "%d item%s", app.total,
        app.total == 1 ? "" : "s");
    dtext(422, 34, C_DARK, label);

    if(app.total == 0) {
        dtext(16, 74, C_DARK, "(empty)");
    }
    for(int i=0; i<EX_ROWS && app.top+i<app.total; i++) {
        int index = app.top+i;
        int y = 52+i*12;
        ex_entry_t *e = &app.rows[i];
        if(index == app.selected)
            box_fill(3, y-2, 521, y+10, C_RGB(23, 29, 31));
        dtext(8, y, e->is_dir ? C_BLUE : C_BLACK,
            e->is_dir ? "<DIR>" : "FILE");
        printable(label, sizeof label, e->name, 48);
        dtext(74, y, C_BLACK, label);
        if(e->truncated)
            dtext(477, y, C_RED, "LONG");
        else if(e->is_dir)
            dtext(485, y, C_DARK, ">");
    }
    if(app.total > EX_ROWS) {
        int track = 235;
        int thumb = max_int(9, track*EX_ROWS / app.total);
        int y = 54 + (track-thumb)*app.top / max_int(1,app.total-EX_ROWS);
        box_fill(523, 54, 526, 54+track, C_LIGHT);
        box_fill(523, y, 526, y+thumb, C_DARK);
    }
    footer("EXE open  ESC back  PgUp/Dn  SOUND menu");
}
static char const *base_name(const char *path)
{
    const char *p = strrchr(path, '\\');
    return p ? p+1 : path;
}
static void hex_refresh(void)
{
    if(app.file_size == 0) { app.loaded = 0; return; }
    if(app.byte_offset < 0) app.byte_offset = 0;
    if(app.byte_offset >= app.file_size) app.byte_offset = app.file_size-1;
    int base = (app.byte_offset / HEX_PAGE) * HEX_PAGE;
    if(base == app.page_base && app.loaded >= 0) return;
    app.page_base = base;
    int n = ex_read(app.file, base, app.hex, HEX_PAGE, &app.file_size);
    if(n < 0) {
        app.loaded = -1;
        status_rc("Read", n);
    }
    else app.loaded = n;
}
static void draw_hex(void)
{
    char amount[8], line[32];
    human_size(app.file_size, amount);
    header(app.file, amount);
    dtext(9, 34, C_DARK, "OFFSET");
    dtext(104, 34, C_DARK, "HEX BYTES");
    dtext(402, 34, C_DARK, "ASCII");

    for(int row=0; row<EX_ROWS; row++) {
        int offset = app.page_base + row*HEX_COLS;
        if(offset >= app.file_size) break;
        int y = 52+row*12;
        snprintf(line, sizeof line, "%08X", (unsigned)offset);
        dtext(10, y, C_DARK, line);
        char ascii[HEX_COLS+1];
        for(int col=0; col<HEX_COLS; col++) {
            int index = row*HEX_COLS+col;
            int absolute = offset+col;
            if(absolute >= app.file_size || index >= app.loaded ||
                app.loaded < 0) {
                ascii[col]=' ';
                continue;
            }
            uint8_t byte = app.hex[index];
            if(absolute == app.byte_offset)
                box_fill(98 + 24*col, y-2, 119 + 24*col, y+10,
                    C_RGB(23, 29, 31));
            snprintf(line, sizeof line, "%02X", byte);
            dtext(99+24*col, y, C_BLACK, line);
            ascii[col] = (byte >= 32 && byte < 127) ? byte : '.';
        }
        ascii[HEX_COLS] = '\0';
        dtext(402, y, C_BLACK, ascii);
    }
    if(app.editing) {
        char buf[88];
        if(app.digits == 0) snprintf(buf, sizeof buf, "New byte: __  (hex)");
        else if(app.digits == 1)
            snprintf(buf, sizeof buf, "New byte: %X_  (hex)", app.pending_byte);
        else snprintf(buf, sizeof buf, "New byte: %02X  EXE save",
            app.pending_byte);
        box_fill(42, 256, 486, 290, C_RGB(25, 30, 27));
        dline(42, 256, 486, 256, C_DARK);
        dtext(55, 269, C_BLACK, buf);
    }
    footer(app.editing ? "Q-P 1-0  A-F hex  EXE save  ESC cancel" :
        "Arrows byte  PgUp/Dn page  E edit  ESC back");
}

static int menu_allowed(int option)
{
    int has = app.total > 0 && app.selected >= 0 &&
        app.selected >= app.top && app.selected < app.top+EX_ROWS;
    int is_file = has && !app.rows[app.selected-app.top].is_dir &&
        !app.rows[app.selected-app.top].truncated;
    int valid = has && !app.rows[app.selected-app.top].truncated;
    if(option == ACT_DELETE || option == ACT_RENAME) return valid;
    if(option == ACT_COPY || option == ACT_CUT ||
       option == ACT_PREVIEW_BITMAP || option == ACT_PREVIEW_CJF ||
       option == ACT_PREVIEW_RLE565) return is_file;
    if(option == ACT_PASTE) return app.clipboard_valid;
    return 1;
}
static void draw_dialog(void)
{
    if(app.dialog == DIALOG_NONE) return;
    box_fill(30, 65, 497, 267, C_RGB(27, 30, 30));
    dline(30, 65, 497, 65, C_DARK);
    dline(30, 267, 497, 267, C_DARK);
    if(app.dialog == DIALOG_MENU) {
        dtext(48, 78, C_BLACK, "FILE OPERATIONS");
        for(int i=0; i<ACTION_COUNT; i++) {
            int y = 95+i*16;
            if(i == app.menu_choice)
                box_fill(40, y-3, 486, y+13, C_LIGHT);
            dtext(52, y, menu_allowed(i) ? C_BLACK : C_DARK, actions[i]);
        }
        dtext(48, 251, C_DARK, "EXE select     ESC close");
    }
    else if(app.dialog == DIALOG_NAME) {
        const char *what = app.action==ACT_RENAME ? "RENAME" :
            app.action==ACT_NEW_FILE ? "NEW FILE" : "NEW FOLDER";
        char shown[EX_NAME_MAX+8];
        dtext(48, 80, C_BLACK, what);
        printable(shown, sizeof shown, app.input, 49);
        dtext(48, 124, C_BLACK, shown);
        drect(48, 139, 480, 139, C_DARK);
        dtext(48, 170, C_DARK, "Type a filename (including extension)");
        dtext(48, 192, C_DARK, "ALT + A = '.'    ALT + Q-P = 1-0");
        dtext(48, 247, C_BLACK, "EXE confirm      ESC cancel");
    }
    else if(app.dialog == DIALOG_CONFIRM) {
        char label[90];
        snprintf(label, sizeof label, "%s?",
            app.action==ACT_DELETE ? "DELETE PERMANENTLY" :
            app.action==ACT_EDIT ? "WRITE BYTE TO FILE" : "CONFIRM");
        dtext(48, 82, C_RED, label);
        if(app.action==ACT_DELETE)
            printable(label, sizeof label, base_name(app.source), 45);
        else
            snprintf(label, sizeof label, "Offset %08X = %02X",
                (unsigned)app.byte_offset, app.pending_byte);
        dtext(48, 129, C_BLACK, label);
        dtext(48, 171, C_RED, "System files can be damaged!");
        dtext(48, 247, C_BLACK, "EXE YES       ESC NO");
    }
}
static void redraw(void)
{
    if(app.screen == SCREEN_IMAGE || app.screen == SCREEN_FONT) {
        if(preview_render()<0) status_message("Preview rendering failed");
        return;
    }
    dclear(C_WHITE);
    if(app.screen == SCREEN_VOLUMES) draw_volumes();
    if(app.screen == SCREEN_DIRECTORY) draw_directory();
    if(app.screen == SCREEN_HEX) draw_hex();
    draw_dialog();
    dupdate();
}
static void reload_directory(void)
{
    loading(app.directory);
    int old_top = app.top;
    int rc = ex_list(app.directory, app.top, app.rows);
    if(rc == -1) {
        app.total=0;
        status_message("Empty directory");
        return;
    }
    if(rc < 0) {
        app.total = 0;
        status_rc("List", rc);
        return;
    }
    app.total = rc;
    if(app.total == 0) { app.selected = app.top = 0; return; }
    app.selected = max_int(0, min_int(app.selected, app.total - 1));
    if(app.top > app.selected) app.top = app.selected;
    if(app.selected >= app.top+EX_ROWS)
        app.top = app.selected-EX_ROWS+1;
    if(app.top > max_int(0, app.total-EX_ROWS))
        app.top = max_int(0, app.total-EX_ROWS);
    if(old_top != app.top &&
       ex_list_window(app.directory, app.top, app.total, app.rows) < 0)
        status_message("Directory refresh failed");
}
static void choose_line(int next)
{
    int count = app.screen == SCREEN_VOLUMES ? VOLUME_COUNT+1 : app.total;
    if(count <= 0) return;
    app.selected = max_int(0, min_int(count-1, next));
    if(app.screen == SCREEN_DIRECTORY) {
        int old = app.top;
        if(app.selected < app.top) app.top = app.selected;
        else if(app.selected >= app.top + EX_ROWS)
            app.top = app.selected-EX_ROWS+1;
        if(old != app.top) {
            int rc = ex_list_window(app.directory, app.top, app.total, app.rows);
            if(rc < 0) status_rc("Scroll", rc);
        }
    }
}
static int current_entry_path(char out[EX_PATH_MAX])
{
    if(app.screen != SCREEN_DIRECTORY || !app.total ||
        app.selected < app.top || app.selected >= app.top + EX_ROWS)
        return -1;
    ex_entry_t *e = &app.rows[app.selected-app.top];
    if(e->truncated) return -1;
    return ex_join(out, app.directory, e->name);
}
static void enter_selected(void)
{
    char target[EX_PATH_MAX];
    if(app.screen == SCREEN_VOLUMES) {
        if(app.selected==VOLUME_COUNT) {
            ex_usb_remote_screen();
            app.status[0]=0;
            return;
        }
        app.volume_selected = app.selected;
        snprintf(target, sizeof target, "\\\\%s", volumes[app.selected]);
    }
    else if(current_entry_path(target) < 0) {
        status_message("This name is too long to open safely");
        return;
    }

    if(app.screen == SCREEN_DIRECTORY &&
       !app.rows[app.selected-app.top].is_dir) {
        int size = 0;
        uint8_t first = 0;
        int rc = ex_read(target, 0, &first, 1, &size);
        if(rc < 0) { status_rc("Open", rc); return; }
        copy_string(app.file, sizeof app.file, target);
        app.file_size = size;
        app.byte_offset = 0;
        app.page_base = -1;
        app.loaded = -1;
        app.editing = 0;
        app.screen = SCREEN_HEX;
        hex_refresh();
        return;
    }

    ex_entry_t temp[EX_ROWS];
    loading(target);
    int rc = ex_list(target, 0, temp);
    if(rc < 0 && rc != -1) { status_rc("Open directory", rc); return; }
    if(rc == -1) rc = 0;
    if(app.screen == SCREEN_DIRECTORY && app.depth < 32) {
        app.parents_selected[app.depth] = app.selected;
        app.parents_top[app.depth] = app.top;
        app.depth++;
    }
    else if(app.screen == SCREEN_VOLUMES) app.depth = 0;
    copy_string(app.directory, sizeof app.directory, target);
    app.screen = SCREEN_DIRECTORY;
    app.selected = app.top = 0;
    app.total = rc;
    memcpy(app.rows, temp, sizeof temp);
    if(rc == 0) status_message("Empty directory");
    else app.status[0] = 0;
}
static void go_back(void)
{
    app.status[0] = 0;
    if(app.screen == SCREEN_HEX || app.screen == SCREEN_IMAGE ||
       app.screen == SCREEN_FONT) {
        app.screen = SCREEN_DIRECTORY;
        app.editing = 0;
        reload_directory();
    }
    else if(app.screen == SCREEN_DIRECTORY) {
        char *last = strrchr(app.directory, '\\');
        if(last && last > app.directory+1) {
            *last = '\0';
            if(app.depth > 0) {
                app.depth--;
                app.selected = app.parents_selected[app.depth];
                app.top = app.parents_top[app.depth];
            }
            else app.selected = app.top = 0;
            reload_directory();
        }
        else {
            app.screen = SCREEN_VOLUMES;
            app.depth = 0;
            app.selected = app.volume_selected;
            app.top = 0;
        }
    }
}
static void start_context(void)
{
    if(app.screen != SCREEN_DIRECTORY) return;
    app.dialog = DIALOG_MENU;
    app.menu_choice = ACT_NEW_FILE;
}
static void start_name(int action)
{
    app.action = action;
    app.dialog = DIALOG_NAME;
    app.input[0] = '\0';
    if(action == ACT_RENAME) {
        const char *old = app.rows[app.selected-app.top].name;
        copy_string(app.input, sizeof app.input, old);
    }
    app.input_len = strlen(app.input);
}
static int safe_filename(const char *name)
{
    if(!*name || !strcmp(name, ".") || !strcmp(name, ".."))
        return 0;
    for(size_t i=0; name[i]; i++) {
        unsigned char c=name[i];
        if(c<' ' || c==127 || c=='\\' || c=='/' || c==':' || c=='*'
           || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') return 0;
    }
    return 1;
}
static void finish_name(void)
{
    if(!safe_filename(app.input)) {
        status_message("Invalid name");
        return;
    }
    int exists = ex_exists_in(app.directory, app.input);
    if(exists < 0) { status_rc("Check name", exists); return; }
    if(app.action == ACT_RENAME &&
        !strcmp(app.input, base_name(app.source))) {
        app.dialog = DIALOG_NONE;
        return;
    }
    if(exists) { status_message("Name already exists"); return; }
    char target[EX_PATH_MAX];
    if(ex_join(target, app.directory, app.input) < 0) {
        status_message("Path too long"); return;
    }
    int rc = -1;
    if(app.action == ACT_RENAME) rc = ex_rename(app.source, target);
    else if(app.action == ACT_NEW_FILE) rc = ex_create(target, 0);
    else if(app.action == ACT_NEW_FOLDER) rc = ex_create(target, 1);
    if(rc < 0) { status_rc("File operation", rc); return; }
    app.dialog = DIALOG_NONE;
    status_message("Done");
    reload_directory();
}
static void paste_clipboard(void)
{
    if(!app.clipboard_valid) return;
    char target[EX_PATH_MAX];
    if(ex_join(target, app.directory, app.clip_name) < 0) {
        status_message("Destination path too long"); return;
    }
    if(!strcmp(target, app.clipboard)) {
        status_message("Source and destination are identical"); return;
    }
    int exists=ex_exists_in(app.directory, app.clip_name);
    if(exists < 0) { status_rc("Check name", exists); return; }
    if(exists) { status_message("Destination already exists"); return; }
    int rc=ex_copy_file(app.clipboard, target);
    if(rc < 0) { status_rc("Copy", rc); return; }
    if(app.clipboard_cut) {
        rc=ex_delete(app.clipboard);
        if(rc < 0) {
            status_message("Copied, but original could not be deleted");
        }
        else {
            app.clipboard_valid=0;
            status_message("Moved");
        }
    }
    else status_message("Copied");
    reload_directory();
}
static void invoke_action(int action)
{
    app.dialog = DIALOG_NONE;
    if(action == ACT_NEW_FILE || action == ACT_NEW_FOLDER) {
        start_name(action); return;
    }
    if(action == ACT_PASTE) { paste_clipboard(); return; }
    if(current_entry_path(app.source) < 0) {
        status_message("Cannot select that entry"); return;
    }
    if(action == ACT_PREVIEW_BITMAP || action == ACT_PREVIEW_CJF ||
       action == ACT_PREVIEW_RLE565) {
        int type=action==ACT_PREVIEW_BITMAP ? PREVIEW_BITMAP :
            action==ACT_PREVIEW_CJF ? PREVIEW_CJF : PREVIEW_RLE565;
        loading(app.source);
        int rc=preview_open(app.source,type);
        if(rc<0) { status_rc("Unsupported preview", rc); return; }
        app.screen=type==PREVIEW_CJF ? SCREEN_FONT : SCREEN_IMAGE;
        return;
    }
    if(action == ACT_RENAME) { start_name(ACT_RENAME); return; }
    if(action == ACT_DELETE) {
        app.action = ACT_DELETE;
        app.dialog = DIALOG_CONFIRM;
        return;
    }
    if(action == ACT_COPY || action == ACT_CUT) {
        copy_string(app.clipboard, sizeof app.clipboard, app.source);
        copy_string(app.clip_name, sizeof app.clip_name,
            app.rows[app.selected-app.top].name);
        app.clipboard_cut = action == ACT_CUT;
        app.clipboard_valid=1;
        status_message(action==ACT_CUT ? "Cut: navigate then paste" :
            "Copied to clipboard");
    }
}
static void finish_confirm(void)
{
    app.dialog = DIALOG_NONE;
    if(app.action == ACT_DELETE) {
        int rc=ex_delete(app.source);
        if(rc < 0) status_rc("Delete", rc);
        else {
            status_message("Deleted");
            reload_directory();
        }
    }
    else if(app.action == ACT_EDIT) {
        int rc=ex_write_byte(app.file, app.byte_offset,
            (uint8_t)app.pending_byte);
        if(rc < 0) status_rc("Write byte", rc);
        else {
            status_message("Byte saved");
            app.page_base = -1;
            hex_refresh();
        }
        app.editing=0;
    }
}
static int char_from_key(key_event_t ev)
{
    if(ev.key < KEY_EX_Q || ev.key > KEY_EX_M) return -1;
    const char *letters = "QWERTYUIOPASDFGHJKLZXCVBNM";
    int i=ev.key-KEY_EX_Q;
    int alt=keydown(KEY_ALPHA) || ev.alpha;
    int shift=keydown(KEY_SHIFT) || ev.shift;
    if(alt && ev.key==KEY_EX_A) return '.';
    if(alt && i<10) return "1234567890"[i];
    char c=letters[i];
    if(!shift) c += 'a'-'A';
    return c;
}
static int hex_digit(key_event_t ev)
{
    if(ev.key>=KEY_EX_Q && ev.key<=KEY_EX_P)
        return "1234567890"[ev.key-KEY_EX_Q]-'0';
    switch(ev.key) {
        case KEY_EX_A: return 10; case KEY_EX_B: return 11;
        case KEY_EX_C: return 12; case KEY_EX_D: return 13;
        case KEY_EX_E: return 14; case KEY_EX_F: return 15;
    }
    return -1;
}
static void name_key(key_event_t ev)
{
    if(ev.key==KEY_EXIT) { app.dialog=DIALOG_NONE; return; }
    if(ev.key==KEY_EXE) { finish_name(); return; }
    if(ev.key==KEY_DEL) {
        if(app.input_len) app.input[--app.input_len]='\0';
        return;
    }
    int c=char_from_key(ev);
    if(c>=0 && app.input_len + 1 < EX_NAME_MAX) {
        app.input[app.input_len++]=(char)c;
        app.input[app.input_len]=0;
    }
}
static void hex_key(key_event_t ev)
{
    if(ev.key==KEY_EXIT) {
        if(app.editing) { app.editing=0; return; }
        go_back(); return;
    }
    if(app.editing) {
        if(ev.key==KEY_DEL) {
            if(app.digits == 2) { app.pending_byte >>=4; app.digits=1; }
            else { app.pending_byte=0; app.digits=0; }
        }
        else if(ev.key==KEY_EXE && app.digits==2) {
            app.action=ACT_EDIT;
            app.dialog=DIALOG_CONFIRM;
        }
        else {
            int n=hex_digit(ev);
            if(n>=0 && app.digits<2) {
                app.pending_byte=(app.pending_byte<<4)|n;
                app.digits++;
            }
        }
        return;
    }
    if(ev.key==KEY_EX_E && app.file_size>0) {
        app.editing=1; app.digits=0; app.pending_byte=0; return;
    }
    int next=app.byte_offset;
    if(ev.key==KEY_RIGHT) next++;
    if(ev.key==KEY_LEFT) next--;
    if(ev.key==KEY_UP) next-=HEX_COLS;
    if(ev.key==KEY_DOWN) next+=HEX_COLS;
    if(ev.key==KEY_PAGEUP) next-=HEX_PAGE;
    if(ev.key==KEY_PAGEDOWN) next+=HEX_PAGE;
    if(app.file_size>0)
        app.byte_offset=max_int(0,min_int(app.file_size-1,next));
    hex_refresh();
}
static void handle_key(key_event_t ev)
{
    if(!is_press(ev)) return;
    if(ev.key==KEY_ACON) {
        app.screen=-1;
        return;
    }
    if(app.dialog==DIALOG_CONFIRM) {
        if(ev.key==KEY_EXIT) app.dialog=DIALOG_NONE;
        else if(ev.key==KEY_EXE && ev.type==KEYEV_DOWN)
            finish_confirm();
        return;
    }
    if(app.dialog==DIALOG_NAME) {
        if(ev.type==KEYEV_DOWN) name_key(ev);
        return;
    }
    if(app.dialog==DIALOG_MENU) {
        if(ev.key==KEY_EXIT) app.dialog=DIALOG_NONE;
        else if(ev.key==KEY_UP || ev.key==KEY_DOWN) {
            int direction=ev.key==KEY_UP ? -1 : 1;
            for(int i=0; i<ACTION_COUNT; i++) {
                app.menu_choice=(app.menu_choice+ACTION_COUNT+direction)%
                    ACTION_COUNT;
                if(menu_allowed(app.menu_choice)) break;
            }
        }
        else if(ev.key==KEY_EXE && menu_allowed(app.menu_choice)) {
            invoke_action(app.menu_choice);
        }
        return;
    }
    if(app.screen==SCREEN_HEX) { hex_key(ev); return; }
    if(app.screen==SCREEN_IMAGE || app.screen==SCREEN_FONT) {
        if(ev.key==KEY_EXIT) go_back();
        else if(app.screen==SCREEN_FONT) {
            if(ev.key==KEY_PAGEUP || ev.key==KEY_UP) preview_page(-1);
            if(ev.key==KEY_PAGEDOWN || ev.key==KEY_DOWN) preview_page(1);
            if(ev.key==KEY_LEFT) preview_range(-1);
            if(ev.key==KEY_RIGHT) preview_range(1);
        }
        return;
    }
    if(ev.key==KEY_EXIT) {
        if(app.screen==SCREEN_VOLUMES) app.screen=-1;
        else go_back();
        return;
    }
    if(ev.key==KEY_EX_AUDIO) { start_context(); return; }
    if(ev.key==KEY_EXE && ev.type==KEYEV_DOWN) {
        if(app.screen==SCREEN_VOLUMES || app.total>0) enter_selected();
        return;
    }
    if(ev.key==KEY_UP) choose_line(app.selected-1);
    if(ev.key==KEY_DOWN) choose_line(app.selected+1);
    if(ev.key==KEY_PAGEUP) choose_line(app.selected-EX_ROWS);
    if(ev.key==KEY_PAGEDOWN) choose_line(app.selected+EX_ROWS);
}

int main(void)
{
    memset(&app, 0, sizeof app);
    app.screen=SCREEN_VOLUMES;
    app.page_base=-1;
    redraw();

    while(app.screen>=0) {
        key_event_t ev=waitevent(NULL);
        if(ev.type==KEYEV_TOUCH_DOWN) {
            /* Fullscreen touch: tap a file/volume; tap again to open. */
            if(app.dialog==DIALOG_NONE && app.screen==SCREEN_DIRECTORY &&
                ev.y>=50 && ev.y<50+EX_ROWS*12) {
                int index=app.top+(ev.y-50)/12;
                if(index<app.total) {
                    if(index==app.selected) enter_selected();
                    else choose_line(index);
                }
            }
            else if(app.dialog==DIALOG_NONE && app.screen==SCREEN_VOLUMES &&
                ev.y>=42 && ev.y<42+(VOLUME_COUNT+1)*19) {
                int index=(ev.y-42)/19;
                if(index<=VOLUME_COUNT) {
                    if(index==app.selected) enter_selected();
                    else choose_line(index);
                }
            }
            else if(app.dialog==DIALOG_NONE && app.screen==SCREEN_HEX &&
                    ev.y>=50 && ev.y<50+EX_ROWS*12) {
                int row=(ev.y-50)/12;
                app.byte_offset=min_int(max_int(0,app.file_size-1),
                    app.page_base+HEX_COLS*row);
                hex_refresh();
            }
        }
        else handle_key(ev);
        if(app.screen>=0) redraw();
    }

    touch_exword_quit();
    /* The EX-word runtime uses -2 for a clean return to the host menu. */
    return -2;
}
