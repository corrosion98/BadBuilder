/*
 * BadBuilder - On-device visual DuckyScript/BadUSB editor for Flipper Zero (Momentum)
 *
 * Architecture: a ViewDispatcher hosts two views:
 *   - a custom View (editor) that we draw by hand for the 128x64 split UI
 *   - the stock TextInput view, shown modally when a command needs an argument
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <stdlib.h>
#include <string.h>
#include "badbuilder_icons.h"
#define TAG "BadBuilder"

#define BADUSB_DIR         "/ext/badusb"
#define DEFAULT_FILE_PATH  "/ext/badusb/ducky_editor.txt"
#define LAYOUT_CONFIG_PATH "/ext/badusb/.badbuilder_layout"
#define MAX_LINES          256
#define MAX_LINE_LEN       96
#define MAX_FILENAME_LEN   64
#define TEXT_INPUT_BUF_LEN 128
#define MAX_BROWSER_FILES  64
static void bb_text_input_result(void* context);
/* ---------------------------------------------------------------------- */
/* Command table                                                          */
/* ---------------------------------------------------------------------- */

typedef enum {
    ArgNone = 0,
    ArgText,
    ArgNumber,
} ArgKind;

typedef struct {
    const char* name;
    const char* tip;
    ArgKind arg;
    const char* prompt;
} DuckyCommand;

static const DuckyCommand k_commands[] = {
    {"STRING", "Types the supplied text.", ArgText, "Enter text"},
    {"DELAY", "Pauses for N milliseconds.", ArgNumber, "Enter ms"},
    {"ENTER", "Presses the Enter key.", ArgNone, NULL},
    {"SPACE", "Presses the Space key.", ArgNone, NULL},
    {"TAB", "Presses the Tab key.", ArgNone, NULL},
    {"BACKSPACE", "Presses Backspace.", ArgNone, NULL},
    {"ESC", "Presses the Escape key.", ArgNone, NULL},
    {"UP", "Presses the Up arrow key.", ArgNone, NULL},
    {"DOWN", "Presses the Down arrow key.", ArgNone, NULL},
    {"LEFT", "Presses the Left arrow key.", ArgNone, NULL},
    {"RIGHT", "Presses the Right arrow key.", ArgNone, NULL},
    {"CTRL", "Holds Ctrl + given key.", ArgText, "Enter key"},
    {"ALT", "Holds Alt + given key.", ArgText, "Enter key"},
    {"GUI", "Holds GUI/Win + given key.", ArgText, "Enter key"},
    {"SHIFT", "Holds Shift + given key.", ArgText, "Enter key"},
};
#define COMMAND_COUNT (sizeof(k_commands) / sizeof(k_commands[0]))

/* ---------------------------------------------------------------------- */
/* Layout modes                                                           */
/* ---------------------------------------------------------------------- */

typedef enum {
    LayoutSplit = 0,
    LayoutCommandsOnly,
    LayoutCodeOnly,
    LayoutCount,
} LayoutMode;

static const char* k_layout_names[LayoutCount] = {
    "Split: Commands|Code",
    "Commands only",
    "Code only",
};

/* ---------------------------------------------------------------------- */
/* Screens / view ids                                                      */
/* ---------------------------------------------------------------------- */

typedef enum {
    BBViewEditor = 0,
    BBViewTextInput = 1,
} BBViewId;

typedef enum {
    ScreenEditor = 0,
    ScreenLayoutMenu,
    ScreenFileMenu,
    ScreenFileBrowser,
    ScreenExitConfirm,
} ScreenMode;

typedef enum {
    PaneCommands = 0,
    PaneCode = 1,
} PaneFocus;

typedef enum {
    PendingNone = 0,
    PendingNewArg,
    PendingEditArg,
    PendingSaveAs,
} PendingAction;

/* ---------------------------------------------------------------------- */
/* Document                                                                */
/* ---------------------------------------------------------------------- */

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int count;
    bool dirty;
} Document;

static void document_clear(Document* doc) {
    doc->count = 0;
    doc->dirty = false;
    memset(doc->lines, 0, sizeof(doc->lines));
}

static bool document_insert_line(Document* doc, int at_index, const char* text) {
    if(doc->count >= MAX_LINES) return false;
    if(at_index < 0) at_index = 0;
    if(at_index > doc->count) at_index = doc->count;
    for(int i = doc->count; i > at_index; i--) {
        strncpy(doc->lines[i], doc->lines[i - 1], MAX_LINE_LEN - 1);
        doc->lines[i][MAX_LINE_LEN - 1] = '\0';
    }
    strncpy(doc->lines[at_index], text, MAX_LINE_LEN - 1);
    doc->lines[at_index][MAX_LINE_LEN - 1] = '\0';
    doc->count++;
    doc->dirty = true;
    return true;
}

static void document_delete_line(Document* doc, int index) {
    if(index < 0 || index >= doc->count) return;
    for(int i = index; i < doc->count - 1; i++) {
        strncpy(doc->lines[i], doc->lines[i + 1], MAX_LINE_LEN - 1);
        doc->lines[i][MAX_LINE_LEN - 1] = '\0';
    }
    doc->count--;
    memset(doc->lines[doc->count], 0, MAX_LINE_LEN);
    doc->dirty = true;
}

/* Reserved for future line-reorder UI (move up/down); wired into a future
   key binding once screen real estate allows a dedicated "edit mode". */
static void document_move_line(Document* doc, int index, int direction) __attribute__((unused));
static void document_move_line(Document* doc, int index, int direction) {
    int target = index + direction;
    if(index < 0 || index >= doc->count) return;
    if(target < 0 || target >= doc->count) return;
    char tmp[MAX_LINE_LEN];
    strncpy(tmp, doc->lines[index], MAX_LINE_LEN);
    strncpy(doc->lines[index], doc->lines[target], MAX_LINE_LEN);
    strncpy(doc->lines[target], tmp, MAX_LINE_LEN);
    doc->dirty = true;
}

/* ---------------------------------------------------------------------- */
/* App struct                                                              */
/* ---------------------------------------------------------------------- */

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* editor_view;
    TextInput* text_input;
    Storage* storage;
    DialogsApp* dialogs;
    FuriMutex* mutex;

    Document doc;

    LayoutMode layout;
    ScreenMode screen;
    PaneFocus focus;

    int cmd_selected;
    int cmd_scroll;
    int code_selected; /* -1 if document empty */
    int code_scroll;

    int tip_scroll_offset;

    char text_input_buf[TEXT_INPUT_BUF_LEN];
    PendingAction pending_action;
    int pending_cmd_index;
    int pending_edit_index;

    int file_menu_selected;

    char file_browser_names[MAX_BROWSER_FILES][MAX_FILENAME_LEN];
    int file_browser_count;
    int file_browser_selected;
    int file_browser_scroll;

    char current_file_path[160];

    bool running;
} BadBuilderApp;

/* ---------------------------------------------------------------------- */
/* Persistence                                                             */
/* ---------------------------------------------------------------------- */

static void bb_ensure_dir(BadBuilderApp* app) {
    if(!storage_dir_exists(app->storage, BADUSB_DIR)) {
        storage_common_mkdir(app->storage, BADUSB_DIR);
    }
}

static void bb_save_layout(BadBuilderApp* app) {
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, LAYOUT_CONFIG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        uint8_t val = (uint8_t)app->layout;
        storage_file_write(file, &val, 1);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void bb_load_layout(BadBuilderApp* app) {
    app->layout = LayoutSplit;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, LAYOUT_CONFIG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t val = 0;
        if(storage_file_read(file, &val, 1) == 1) {
            if(val < LayoutCount) app->layout = (LayoutMode)val;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

static bool bb_save_document(BadBuilderApp* app, const char* path) {
    bb_ensure_dir(app);
    File* file = storage_file_alloc(app->storage);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        for(int i = 0; i < app->doc.count; i++) {
            size_t len = strlen(app->doc.lines[i]);
            storage_file_write(file, app->doc.lines[i], len);
            storage_file_write(file, "\n", 1);
        }
        app->doc.dirty = false;
        strncpy(app->current_file_path, path, sizeof(app->current_file_path) - 1);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static bool bb_load_document(BadBuilderApp* app, const char* path) {
    File* file = storage_file_alloc(app->storage);
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) {
        document_clear(&app->doc);
        char linebuf[MAX_LINE_LEN];
        int linelen = 0;
        uint8_t ch;
        while(storage_file_read(file, &ch, 1) == 1) {
            if(ch == '\n' || ch == '\r') {
                if(linelen > 0) {
                    linebuf[linelen] = '\0';
                    if(app->doc.count < MAX_LINES) {
                        strncpy(app->doc.lines[app->doc.count], linebuf, MAX_LINE_LEN - 1);
                        app->doc.lines[app->doc.count][MAX_LINE_LEN - 1] = '\0';
                        app->doc.count++;
                    }
                    linelen = 0;
                }
            } else if(linelen < MAX_LINE_LEN - 1) {
                linebuf[linelen++] = (char)ch;
            }
        }
        if(linelen > 0) {
            linebuf[linelen] = '\0';
            if(app->doc.count < MAX_LINES) {
                strncpy(app->doc.lines[app->doc.count], linebuf, MAX_LINE_LEN - 1);
                app->doc.lines[app->doc.count][MAX_LINE_LEN - 1] = '\0';
                app->doc.count++;
            }
        }
        app->doc.dirty = false;
        strncpy(app->current_file_path, path, sizeof(app->current_file_path) - 1);
        app->code_selected = app->doc.count > 0 ? 0 : -1;
        app->code_scroll = 0;
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static void bb_refresh_file_list(BadBuilderApp* app) {
    app->file_browser_count = 0;
    bb_ensure_dir(app);
    File* dir = storage_file_alloc(app->storage);
    if(storage_dir_open(dir, BADUSB_DIR)) {
        FileInfo info;
        char name[MAX_FILENAME_LEN];
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            if(name[0] == '.') continue;
            if(app->file_browser_count < MAX_BROWSER_FILES) {
                strncpy(
                    app->file_browser_names[app->file_browser_count],
                    name,
                    MAX_FILENAME_LEN - 1);
                app->file_browser_names[app->file_browser_count][MAX_FILENAME_LEN - 1] = '\0';
                app->file_browser_count++;
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    app->file_browser_selected = 0;
    app->file_browser_scroll = 0;
}

/* ---------------------------------------------------------------------- */
/* Command insertion                                                       */
/* ---------------------------------------------------------------------- */

static void bb_build_line(const DuckyCommand* cmd, const char* arg, char* out, size_t out_len) {
    if(cmd->arg == ArgNone || arg == NULL || arg[0] == '\0') {
        snprintf(out, out_len, "%s", cmd->name);
    } else {
        snprintf(out, out_len, "%s %s", cmd->name, arg);
    }
}

static void bb_insert_new_command(BadBuilderApp* app, int cmd_index, const char* arg) {
    char line[MAX_LINE_LEN];
    bb_build_line(&k_commands[cmd_index], arg, line, sizeof(line));
    int insert_at = (app->code_selected >= 0) ? app->code_selected + 1 : app->doc.count;
    if(document_insert_line(&app->doc, insert_at, line)) {
        app->code_selected = insert_at;
    }
}

/* ---------------------------------------------------------------------- */
/* TextInput plumbing                                                      */
/* ---------------------------------------------------------------------- */

static void bb_open_text_input(
    BadBuilderApp* app,
    const char* header,
    const char* prefill,
    PendingAction action,
    int cmd_index,
    int edit_index) {
    memset(app->text_input_buf, 0, sizeof(app->text_input_buf));
    if(prefill) strncpy(app->text_input_buf, prefill, TEXT_INPUT_BUF_LEN - 1);

    app->pending_action = action;
    app->pending_cmd_index = cmd_index;
    app->pending_edit_index = edit_index;

    text_input_reset(app->text_input);
    // keyboard crash fix
    text_input_set_result_callback(
    app->text_input,
    bb_text_input_result,
    app,
    app->text_input_buf,
    TEXT_INPUT_BUF_LEN,
    true);
    text_input_set_header_text(app->text_input, header);
    /* re-attach the buffer since text_input_reset() clears internal state */
}

/* ---------------------------------------------------------------------- */
/* Drawing                                                                  */
/* ---------------------------------------------------------------------- */

#define TOOLTIP_HEIGHT 14
#define CONTENT_TOP    9
#define CONTENT_BOTTOM (64 - TOOLTIP_HEIGHT)
#define LINE_HEIGHT    9

static void draw_tooltip(Canvas* canvas, BadBuilderApp* app, const char* text) {
    canvas_draw_line(canvas, 0, 64 - TOOLTIP_HEIGHT, 127, 64 - TOOLTIP_HEIGHT);
    canvas_set_font(canvas, FontSecondary);

    char buf[160];
    snprintf(buf, sizeof(buf), "TIP: %s", text ? text : "");
    size_t text_w = canvas_string_width(canvas, buf);
    int y = 64 - TOOLTIP_HEIGHT / 2 + 3;

    if(text_w <= 124) {
        canvas_draw_str(canvas, 2, y, buf);
    } else {
        char scrolled[180];
        snprintf(scrolled, sizeof(scrolled), "%s    ", buf);
        size_t slen = strlen(scrolled);
        int offset = app->tip_scroll_offset % (int)slen;
        char window[64];
        size_t wi = 0;
        for(int i = 0; i < 40 && wi < sizeof(window) - 1; i++) {
            window[wi++] = scrolled[(offset + i) % slen];
        }
        window[wi] = '\0';
        canvas_draw_str(canvas, 2, y, window);
    }
}

static void draw_commands_pane(Canvas* canvas, BadBuilderApp* app, int x, int w) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 2, 7, "CMD");
    canvas_draw_line(canvas, x, 9, x + w, 9);

    int rows = (CONTENT_BOTTOM - CONTENT_TOP) / LINE_HEIGHT;
    if(app->cmd_selected < app->cmd_scroll) app->cmd_scroll = app->cmd_selected;
    if(app->cmd_selected >= app->cmd_scroll + rows) app->cmd_scroll = app->cmd_selected - rows + 1;

    for(int row = 0; row < rows; row++) {
        int idx = app->cmd_scroll + row;
        if(idx >= (int)COMMAND_COUNT) break;
        int y = CONTENT_TOP + row * LINE_HEIGHT + LINE_HEIGHT - 2;
        bool selected = (idx == app->cmd_selected) && (app->focus == PaneCommands);
        if(selected) {
            canvas_draw_box(canvas, x, CONTENT_TOP + row * LINE_HEIGHT, w, LINE_HEIGHT);
            canvas_set_color(canvas, ColorWhite);
        }
        char line[24];
        snprintf(line, sizeof(line), "%s%s", selected ? "> " : "  ", k_commands[idx].name);
        canvas_draw_str(canvas, x + 1, y, line);
        if(selected) canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_code_pane(Canvas* canvas, BadBuilderApp* app, int x, int w) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 2, 7, "CODE");
    canvas_draw_line(canvas, x, 9, x + w, 9);

    int rows = (CONTENT_BOTTOM - CONTENT_TOP) / LINE_HEIGHT;

    if(app->code_selected >= 0) {
        if(app->code_selected < app->code_scroll) app->code_scroll = app->code_selected;
        if(app->code_selected >= app->code_scroll + rows)
            app->code_scroll = app->code_selected - rows + 1;
    }

    if(app->doc.count == 0) {
        canvas_draw_str(canvas, x + 2, CONTENT_TOP + LINE_HEIGHT - 2, "(empty)");
        return;
    }

    int max_chars = (w / 5) > 3 ? (w / 5) - 3 : 4;
    if(max_chars > 40) max_chars = 40;

    for(int row = 0; row < rows; row++) {
        int idx = app->code_scroll + row;
        if(idx >= app->doc.count) break;
        int y = CONTENT_TOP + row * LINE_HEIGHT + LINE_HEIGHT - 2;
        bool selected = (idx == app->code_selected) && (app->focus == PaneCode);
        if(selected) {
            canvas_draw_box(canvas, x, CONTENT_TOP + row * LINE_HEIGHT, w, LINE_HEIGHT);
            canvas_set_color(canvas, ColorWhite);
        }
        char trimmed[42];
        strncpy(trimmed, app->doc.lines[idx], max_chars);
        trimmed[max_chars] = '\0';
        char line[96];
        snprintf(line, sizeof(line), "%d %s", idx + 1, trimmed);
        canvas_draw_str(canvas, x + 1, y, line);
        if(selected) canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_layout_menu(Canvas* canvas, BadBuilderApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Layout Settings");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_line(canvas, 0, 13, 127, 13);
    for(int i = 0; i < LayoutCount; i++) {
        int y = 24 + i * 12;
        bool selected = (i == app->cmd_selected);
        if(selected) {
            canvas_draw_box(canvas, 0, y - 9, 128, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        char marker = (i == (int)app->layout) ? '*' : ' ';
        char line[32];
        snprintf(line, sizeof(line), "%c %s", marker, k_layout_names[i]);
        canvas_draw_str(canvas, 2, y, line);
        if(selected) canvas_set_color(canvas, ColorBlack);
    }
    draw_tooltip(canvas, app, "OK=select  Back=cancel");
}

static void draw_file_menu(Canvas* canvas, BadBuilderApp* app) {
    static const char* items[] = {"New", "Open", "Save", "Save As", "Back"};
    int n = 5;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "File");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_line(canvas, 0, 13, 127, 13);
    for(int i = 0; i < n; i++) {
        int y = 24 + i * 10;
        bool selected = (i == app->file_menu_selected);
        if(selected) {
            canvas_draw_box(canvas, 0, y - 8, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 2, y, items[i]);
        if(selected) canvas_set_color(canvas, ColorBlack);
    }
    draw_tooltip(canvas, app, "OK=select  Back=cancel");
}

static void draw_file_browser(Canvas* canvas, BadBuilderApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Open file");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(app->file_browser_count == 0) {
        canvas_draw_str(canvas, 2, 26, "No files in");
        canvas_draw_str(canvas, 2, 36, BADUSB_DIR);
        draw_tooltip(canvas, app, "Back=cancel");
        return;
    }

    int rows = 4;
    if(app->file_browser_selected < app->file_browser_scroll)
        app->file_browser_scroll = app->file_browser_selected;
    if(app->file_browser_selected >= app->file_browser_scroll + rows)
        app->file_browser_scroll = app->file_browser_selected - rows + 1;

    for(int row = 0; row < rows; row++) {
        int idx = app->file_browser_scroll + row;
        if(idx >= app->file_browser_count) break;
        int y = 24 + row * 10;
        bool selected = (idx == app->file_browser_selected);
        if(selected) {
            canvas_draw_box(canvas, 0, y - 8, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 2, y, app->file_browser_names[idx]);
        if(selected) canvas_set_color(canvas, ColorBlack);
    }
    draw_tooltip(canvas, app, "OK=open  Back=cancel");
}

static void draw_exit_confirm(Canvas* canvas, BadBuilderApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 20, "Unsaved changes!");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 34, "Back again = discard");
    canvas_draw_str(canvas, 2, 44, "OK = save & exit");
    draw_tooltip(canvas, app, "Any other key cancels");
}

static void bb_render(Canvas* canvas, void* model) {
    BadBuilderApp* app = *(BadBuilderApp**)model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    switch(app->screen) {
    case ScreenLayoutMenu:
        draw_layout_menu(canvas, app);
        return;
    case ScreenFileMenu:
        draw_file_menu(canvas, app);
        return;
    case ScreenFileBrowser:
        draw_file_browser(canvas, app);
        return;
    case ScreenExitConfirm:
        draw_exit_confirm(canvas, app);
        return;
    case ScreenEditor:
    default:
        break;
    }

    const char* tip = "Select a command";
    switch(app->layout) {
    case LayoutCommandsOnly:
        draw_commands_pane(canvas, app, 0, 128);
        if(app->cmd_selected >= 0 && app->cmd_selected < (int)COMMAND_COUNT)
            tip = k_commands[app->cmd_selected].tip;
        break;
    case LayoutCodeOnly:
        draw_code_pane(canvas, app, 0, 128);
        tip = (app->doc.count > 0) ? "UP/DOWN:browse  LongLEFT:layout" :
                                      "No lines. Hold LEFT for layout.";
        break;
    case LayoutSplit:
    default:
        draw_commands_pane(canvas, app, 0, 62);
        canvas_draw_line(canvas, 63, 9, 63, CONTENT_BOTTOM);
        draw_code_pane(canvas, app, 65, 63);
        if(app->focus == PaneCommands && app->cmd_selected >= 0 &&
           app->cmd_selected < (int)COMMAND_COUNT) {
            tip = k_commands[app->cmd_selected].tip;
        } else if(app->focus == PaneCode) {
            tip = (app->doc.count > 0) ? "OK:edit  LongOK:save  LEFT:cmds" :
                                          "LEFT: go to commands";
        }
        break;
    }
    draw_tooltip(canvas, app, tip);
}

/* ---------------------------------------------------------------------- */
/* Editor key handling. Returns true if handled without needing a view     */
/* switch; false means the caller should switch to the TextInput view.     */
/* ---------------------------------------------------------------------- */

static bool bb_handle_editor_key(BadBuilderApp* app, InputEvent* event) {
    bool in_commands = (app->layout == LayoutCommandsOnly) ||
                        (app->layout == LayoutSplit && app->focus == PaneCommands);
    bool in_code = (app->layout == LayoutCodeOnly) ||
                   (app->layout == LayoutSplit && app->focus == PaneCode);

    if(event->type == InputTypeLong && event->key == InputKeyLeft) {
        app->screen = ScreenLayoutMenu;
        app->cmd_selected = (int)app->layout;
        return true;
    }

    if(event->type == InputTypeLong && event->key == InputKeyRight) {
        app->screen = ScreenFileMenu;
        app->file_menu_selected = 0;
        return true;
    }

    if(event->type == InputTypeLong && event->key == InputKeyOk && in_code) {
        bb_save_document(
            app, app->current_file_path[0] ? app->current_file_path : DEFAULT_FILE_PATH);
        return true;
    }

    if(event->type == InputTypeLong && event->key == InputKeyDown && in_code &&
       app->code_selected >= 0) {
        document_delete_line(&app->doc, app->code_selected);
        if(app->code_selected >= app->doc.count) app->code_selected = app->doc.count - 1;
        return true;
    }

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;

    switch(event->key) {
    case InputKeyUp:
        if(in_commands && app->cmd_selected > 0) app->cmd_selected--;
        else if(in_code && app->code_selected > 0) app->code_selected--;
        break;
    case InputKeyDown:
        if(in_commands && app->cmd_selected < (int)COMMAND_COUNT - 1) app->cmd_selected++;
        else if(in_code && app->code_selected < app->doc.count - 1) app->code_selected++;
        break;
    case InputKeyLeft:
        if(app->layout == LayoutSplit) app->focus = PaneCommands;
        break;
    case InputKeyRight:
        if(app->layout == LayoutSplit) app->focus = PaneCode;
        break;
    case InputKeyOk:
        if(in_commands) {
            const DuckyCommand* cmd = &k_commands[app->cmd_selected];
            if(cmd->arg == ArgNone) {
                bb_insert_new_command(app, app->cmd_selected, NULL);
            } else {
                bb_open_text_input(
                    app,
                    cmd->prompt ? cmd->prompt : "Enter args",
                    NULL,
                    PendingNewArg,
                    app->cmd_selected,
                    -1);
                return false;
            }
        } else if(in_code && app->code_selected >= 0 && app->code_selected < app->doc.count) {
            char* line = app->doc.lines[app->code_selected];
            for(size_t i = 0; i < COMMAND_COUNT; i++) {
                size_t nlen = strlen(k_commands[i].name);
                if(strncmp(line, k_commands[i].name, nlen) == 0 && k_commands[i].arg != ArgNone &&
                   (line[nlen] == ' ' || line[nlen] == '\0')) {
                    const char* arg_start = line + nlen;
                    while(*arg_start == ' ') arg_start++;
                    bb_open_text_input(
                        app,
                        k_commands[i].prompt ? k_commands[i].prompt : "Enter args",
                        arg_start,
                        PendingEditArg,
                        (int)i,
                        app->code_selected);
                    return false;
                }
            }
        }
        break;
    default:
        break;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* View input/draw glue                                                    */
/* ---------------------------------------------------------------------- */

static void bb_view_draw_callback(Canvas* canvas, void* model) {
    bb_render(canvas, model);
}

static bool bb_view_input_callback(InputEvent* event, void* context) {
    BadBuilderApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool need_view_switch = false;

    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        if(app->screen == ScreenEditor) {
            if(app->doc.dirty) {
                app->screen = ScreenExitConfirm;
            } else {
                app->running = false;
            }
        } else if(app->screen == ScreenExitConfirm) {
            app->running = false;
        } else {
            app->screen = ScreenEditor;
        }
        furi_mutex_release(app->mutex);
        if(!app->running) view_dispatcher_stop(app->view_dispatcher);
        return true;
    }

    switch(app->screen) {
    case ScreenEditor:
        need_view_switch = !bb_handle_editor_key(app, event);
        break;

    case ScreenLayoutMenu:
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyUp && app->cmd_selected > 0) app->cmd_selected--;
            if(event->key == InputKeyDown && app->cmd_selected < LayoutCount - 1)
                app->cmd_selected++;
            if(event->key == InputKeyOk) {
                app->layout = (LayoutMode)app->cmd_selected;
                bb_save_layout(app);
                app->cmd_selected = 0;
                app->focus = PaneCommands;
                app->screen = ScreenEditor;
            }
        }
        break;

    case ScreenFileMenu:
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyUp && app->file_menu_selected > 0) app->file_menu_selected--;
            if(event->key == InputKeyDown && app->file_menu_selected < 4)
                app->file_menu_selected++;
            if(event->key == InputKeyOk) {
                switch(app->file_menu_selected) {
                case 0:
                    document_clear(&app->doc);
                    app->code_selected = -1;
                    strncpy(
                        app->current_file_path,
                        DEFAULT_FILE_PATH,
                        sizeof(app->current_file_path) - 1);
                    app->screen = ScreenEditor;
                    break;
                case 1:
                    bb_refresh_file_list(app);
                    app->screen = ScreenFileBrowser;
                    break;
                case 2:
                    bb_save_document(
                        app,
                        app->current_file_path[0] ? app->current_file_path :
                                                     DEFAULT_FILE_PATH);
                    app->screen = ScreenEditor;
                    break;
                case 3:
                    bb_open_text_input(app, "Filename", NULL, PendingSaveAs, -1, -1);
                    need_view_switch = true;
                    break;
                case 4:
                    app->screen = ScreenEditor;
                    break;
                }
            }
        }
        break;

    case ScreenFileBrowser:
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyUp && app->file_browser_selected > 0)
                app->file_browser_selected--;
            if(event->key == InputKeyDown &&
               app->file_browser_selected < app->file_browser_count - 1)
                app->file_browser_selected++;
            if(event->key == InputKeyOk && app->file_browser_count > 0) {
                char full[200];
                snprintf(
                    full,
                    sizeof(full),
                    "%s/%s",
                    BADUSB_DIR,
                    app->file_browser_names[app->file_browser_selected]);
                bb_load_document(app, full);
                app->screen = ScreenEditor;
            }
        }
        break;

    case ScreenExitConfirm:
        if(event->type == InputTypeShort && event->key == InputKeyOk) {
            bb_save_document(
                app, app->current_file_path[0] ? app->current_file_path : DEFAULT_FILE_PATH);
            app->running = false;
        } else if(event->type == InputTypeShort) {
            app->screen = ScreenEditor;
        }
        break;

    default:
        break;
    }

    app->tip_scroll_offset++;
    furi_mutex_release(app->mutex);

    if(need_view_switch) {
        view_dispatcher_switch_to_view(app->view_dispatcher, BBViewTextInput);
    } else if(!app->running) {
        view_dispatcher_stop(app->view_dispatcher);
    }
    return true;
}

static void bb_text_input_result(void* context) {
    BadBuilderApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    switch(app->pending_action) {
    case PendingNewArg:
        bb_insert_new_command(app, app->pending_cmd_index, app->text_input_buf);
        break;
    case PendingEditArg: {
        char line[MAX_LINE_LEN];
        bb_build_line(
            &k_commands[app->pending_cmd_index], app->text_input_buf, line, sizeof(line));
        if(app->pending_edit_index >= 0 && app->pending_edit_index < app->doc.count) {
            strncpy(app->doc.lines[app->pending_edit_index], line, MAX_LINE_LEN - 1);
            app->doc.lines[app->pending_edit_index][MAX_LINE_LEN - 1] = '\0';
            app->doc.dirty = true;
            app->code_selected = app->pending_edit_index;
        }
        break;
    }
    case PendingSaveAs: {
        if(app->text_input_buf[0]) {
            char full[200];
            snprintf(full, sizeof(full), "%s/%s", BADUSB_DIR, app->text_input_buf);
            bb_save_document(app, full);
        }
        break;
    }
    case PendingNone:
    default:
        break;
    }

    app->pending_action = PendingNone;
    app->screen = ScreenEditor;
    furi_mutex_release(app->mutex);
    view_dispatcher_switch_to_view(app->view_dispatcher, BBViewEditor);
}

/* ---------------------------------------------------------------------- */
/* App lifecycle                                                           */
/* ---------------------------------------------------------------------- */

static BadBuilderApp* badbuilder_app_alloc(void) {
    BadBuilderApp* app = malloc(sizeof(BadBuilderApp));
    memset(app, 0, sizeof(BadBuilderApp));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    document_clear(&app->doc);
    app->code_selected = -1;
    app->cmd_selected = 0;
    app->focus = PaneCommands;
    app->screen = ScreenEditor;
    app->pending_action = PendingNone;
    strncpy(app->current_file_path, DEFAULT_FILE_PATH, sizeof(app->current_file_path) - 1);

    bb_ensure_dir(app);
    bb_load_layout(app);
    bb_load_document(app, DEFAULT_FILE_PATH);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->editor_view = view_alloc();
    view_allocate_model(app->editor_view, ViewModelTypeLockFree, sizeof(BadBuilderApp*));
    {
        BadBuilderApp** model = (BadBuilderApp**)view_get_model(app->editor_view);
        *model = app;
    }
    view_set_context(app->editor_view, app);
    view_set_draw_callback(app->editor_view, bb_view_draw_callback);
    view_set_input_callback(app->editor_view, bb_view_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, BBViewEditor, app->editor_view);

    app->text_input = text_input_alloc();
    text_input_set_result_callback(
        app->text_input,
        bb_text_input_result,
        app,
        app->text_input_buf,
        TEXT_INPUT_BUF_LEN,
        true);
    view_dispatcher_add_view(
        app->view_dispatcher, BBViewTextInput, text_input_get_view(app->text_input));

    view_dispatcher_switch_to_view(app->view_dispatcher, BBViewEditor);

    app->running = true;
    return app;
}

static void badbuilder_app_free(BadBuilderApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, BBViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, BBViewEditor);
    text_input_free(app->text_input);
    view_free(app->editor_view);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);

    furi_mutex_free(app->mutex);
    free(app);
}

int32_t badbuilder_app(void* p) {
    UNUSED(p);
    BadBuilderApp* app = badbuilder_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    badbuilder_app_free(app);
    return 0;
}
