/**
 * @file flipper_tv_remote.c
 * @brief Flipper TV Remote – main entry point, app lifecycle, and file I/O.
 */

#include "flipper_tv_remote.h"
#include "views/tv_remote_learn.h"
#include "views/tv_remote_remote.h"

#include <string.h>
#include <stdlib.h>

/* ---- Button name table ---- */

const char* const tv_remote_button_names[TV_BUTTON_COUNT] = {
    "Power",
    "Mute",
    "Vol_up",
    "Vol_dn",
    "Ch_up",
    "Ch_dn",
    "Up",
    "Down",
    "Left",
    "Right",
    "Ok",
    "Back",
    "Home",
    "Play_pause",
};

/* ---- File I/O helpers ---- */

void tv_remote_app_clear_buttons(TvRemoteApp* app) {
    for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
        if(app->buttons[i].signal.timings) {
            free(app->buttons[i].signal.timings);
            app->buttons[i].signal.timings = NULL;
        }
        memset(&app->buttons[i].signal, 0, sizeof(TvRemoteIrSignal));
        app->buttons[i].learned = false;
    }
}

void tv_remote_build_path(FuriString* out, const char* name) {
    furi_string_printf(out, "%s/" TV_REMOTE_FILE_PREFIX "%s.ir", TV_REMOTE_FILE_DIR, name);
}

bool tv_remote_app_save_named(TvRemoteApp* app, const char* name) {
    bool any_learned = false;
    for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
        if(app->buttons[i].learned) { any_learned = true; break; }
    }
    if(!any_learned) return true;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, TV_REMOTE_FILE_DIR);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* path = furi_string_alloc();
    tv_remote_build_path(path, name);

    bool success = false;
    do {
        if(!flipper_format_file_open_always(ff, furi_string_get_cstr(path))) break;
        if(!flipper_format_write_header_cstr(ff, TV_REMOTE_FILE_HEADER, TV_REMOTE_FILE_VERSION))
            break;

        success = true;
        for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
            if(!app->buttons[i].learned) continue;
            TvRemoteIrSignal* sig = &app->buttons[i].signal;

            if(!flipper_format_write_comment_cstr(ff, "")) { success = false; break; }
            if(!flipper_format_write_string_cstr(ff, "name", tv_remote_button_names[i])) {
                success = false; break;
            }
            if(sig->is_raw) {
                if(!flipper_format_write_string_cstr(ff, "type", "raw")) {
                    success = false; break;
                }
                if(!flipper_format_write_uint32(ff, "frequency", &sig->frequency, 1)) {
                    success = false; break;
                }
                if(!flipper_format_write_float(ff, "duty_cycle", &sig->duty_cycle, 1)) {
                    success = false; break;
                }
                if(!flipper_format_write_uint32(
                       ff, "data", sig->timings, (uint16_t)sig->timings_size)) {
                    success = false; break;
                }
            } else {
                if(!flipper_format_write_string_cstr(ff, "type", "parsed")) {
                    success = false; break;
                }
                if(!flipper_format_write_string_cstr(
                       ff, "protocol",
                       infrared_get_protocol_name(sig->message.protocol))) {
                    success = false; break;
                }
                if(!flipper_format_write_hex(
                       ff, "address", (const uint8_t*)&sig->message.address, 4)) {
                    success = false; break;
                }
                if(!flipper_format_write_hex(
                       ff, "command", (const uint8_t*)&sig->message.command, 4)) {
                    success = false; break;
                }
            }
        }
    } while(false);

    furi_string_free(path);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return success;
}

bool tv_remote_app_load_named(TvRemoteApp* app, const char* name) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* path = furi_string_alloc();
    tv_remote_build_path(path, name);
    FuriString* str_name = furi_string_alloc();
    FuriString* type_str = furi_string_alloc();

    bool success = false;
    do {
        if(!flipper_format_buffered_file_open_existing(ff, furi_string_get_cstr(path))) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        if(!flipper_format_read_header(ff, header, &version)) {
            furi_string_free(header);
            break;
        }
        furi_string_free(header);

        while(flipper_format_read_string(ff, "name", str_name)) {
            if(!flipper_format_read_string(ff, "type", type_str)) break;

            const char* name_cstr = furi_string_get_cstr(str_name);
            const char* type_cstr = furi_string_get_cstr(type_str);

            int btn_idx = -1;
            for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
                if(strcmp(name_cstr, tv_remote_button_names[i]) == 0) {
                    btn_idx = (int)i;
                    break;
                }
            }

            if(strcmp(type_cstr, "raw") == 0) {
                uint32_t frequency = 0;
                float duty_cycle = 0;
                if(!flipper_format_read_uint32(ff, "frequency", &frequency, 1)) break;
                if(!flipper_format_read_float(ff, "duty_cycle", &duty_cycle, 1)) break;

                uint32_t timings_count = 0;
                if(!flipper_format_get_value_count(ff, "data", &timings_count)) break;
                if(timings_count == 0) continue;

                uint32_t* timings = malloc(sizeof(uint32_t) * timings_count);
                if(!flipper_format_read_uint32(ff, "data", timings, (uint16_t)timings_count)) {
                    free(timings); break;
                }
                if(btn_idx >= 0) {
                    TvRemoteIrSignal* sig = &app->buttons[btn_idx].signal;
                    if(sig->timings) free(sig->timings);
                    sig->is_raw = true;
                    sig->timings = timings;
                    sig->timings_size = timings_count;
                    sig->frequency = frequency;
                    sig->duty_cycle = duty_cycle;
                    app->buttons[btn_idx].learned = true;
                } else {
                    free(timings);
                }
            } else if(strcmp(type_cstr, "parsed") == 0) {
                FuriString* protocol_str = furi_string_alloc();
                if(!flipper_format_read_string(ff, "protocol", protocol_str)) {
                    furi_string_free(protocol_str); break;
                }
                uint32_t address = 0, command = 0;
                if(!flipper_format_read_hex(ff, "address", (uint8_t*)&address, 4)) {
                    furi_string_free(protocol_str); break;
                }
                if(!flipper_format_read_hex(ff, "command", (uint8_t*)&command, 4)) {
                    furi_string_free(protocol_str); break;
                }
                if(btn_idx >= 0) {
                    TvRemoteIrSignal* sig = &app->buttons[btn_idx].signal;
                    sig->is_raw = false;
                    sig->message.protocol =
                        infrared_get_protocol_by_name(furi_string_get_cstr(protocol_str));
                    sig->message.address = address;
                    sig->message.command = command;
                    sig->message.repeat = false;
                    app->buttons[btn_idx].learned = true;
                }
                furi_string_free(protocol_str);
            }
        }
        success = true;
    } while(false);

    furi_string_free(path);
    furi_string_free(type_str);
    furi_string_free(str_name);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return success;
}

void tv_remote_scan_remotes(TvRemoteApp* app) {
    tv_remote_free_remote_names(app);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(storage);
    const char* dir_path = TV_REMOTE_FILE_DIR;
    const size_t prefix_len = strlen(TV_REMOTE_FILE_PREFIX);

    if(storage_dir_open(dir, dir_path)) {
        FileInfo fi;
        char fname[128];
        while(storage_dir_read(dir, &fi, fname, sizeof(fname))) {
            if(file_info_is_dir(&fi)) continue;
            /* Match tv_remote_*.ir */
            size_t flen = strlen(fname);
            if(flen <= prefix_len + 3) continue; /* too short */
            if(strncmp(fname, TV_REMOTE_FILE_PREFIX, prefix_len) != 0) continue;
            if(strcmp(fname + flen - 3, ".ir") != 0) continue;

            /* Extract name between prefix and ".ir" */
            size_t name_len = flen - prefix_len - 3;
            if(name_len == 0 || name_len > TV_REMOTE_NAME_MAX) continue;

            char* n = malloc(name_len + 1);
            memcpy(n, fname + prefix_len, name_len);
            n[name_len] = '\0';

            char** new_names = realloc(app->remote_names, (app->remote_count + 1) * sizeof(char*));
            if(!new_names) { free(n); break; }
            app->remote_names = new_names;
            app->remote_names[app->remote_count++] = n;
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);
}

void tv_remote_free_remote_names(TvRemoteApp* app) {
    for(size_t i = 0; i < app->remote_count; i++) {
        free(app->remote_names[i]);
    }
    free(app->remote_names);
    app->remote_names = NULL;
    app->remote_count = 0;
}

bool tv_remote_delete_remote(TvRemoteApp* app, const char* name) {
    UNUSED(app);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc();
    tv_remote_build_path(path, name);
    bool ok = storage_simply_remove(storage, furi_string_get_cstr(path));
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* ---- Navigation callbacks ---- */

static uint32_t tv_remote_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t tv_remote_back_to_menu_callback(void* context) {
    UNUSED(context);
    return TvRemoteViewMainMenu;
}

static uint32_t tv_remote_back_to_learn_menu_callback(void* context) {
    UNUSED(context);
    return TvRemoteViewLearnMenu;
}

/* ---- Select Remote view (dynamic submenu) ---- */

#define SELECT_CANCEL_INDEX 0xFFFFFFFFu

static void tv_remote_select_submenu_callback(void* context, uint32_t index) {
    TvRemoteApp* app = context;
    if(index == SELECT_CANCEL_INDEX) {
        tv_remote_free_remote_names(app);
        submenu_reset(app->select_submenu);
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewMainMenu);
        return;
    }
    if(index >= app->remote_count) return;
    const char* chosen = app->remote_names[index];

    /* Capture chosen name into a local buffer before freeing remote_names */
    char chosen_name[TV_REMOTE_NAME_MAX + 1];
    strncpy(chosen_name, chosen, TV_REMOTE_NAME_MAX);
    chosen_name[TV_REMOTE_NAME_MAX] = '\0';

    /* Always clean up the list before switching views */
    tv_remote_free_remote_names(app);
    submenu_reset(app->select_submenu);

    switch(app->select_mode) {
    case TvRemoteSelectModeUse:
        tv_remote_app_clear_buttons(app);
        tv_remote_app_load_named(app, chosen_name);
        strncpy(app->current_remote_name, chosen_name, TV_REMOTE_NAME_MAX);
        app->current_remote_name[TV_REMOTE_NAME_MAX] = '\0';
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewRemote);
        break;
    case TvRemoteSelectModeOverwrite:
        strncpy(app->current_remote_name, chosen_name, TV_REMOTE_NAME_MAX);
        app->current_remote_name[TV_REMOTE_NAME_MAX] = '\0';
        tv_remote_app_clear_buttons(app);
        app->learn_index = 0;
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewLearn);
        break;
    case TvRemoteSelectModeDelete:
        tv_remote_delete_remote(app, chosen_name);
        notification_message(app->notifications, &sequence_success);
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewMainMenu);
        break;
    }
}

/* Helper: scan remotes, populate submenu, and switch to select view.
 * Does nothing (shows error notification) if no remotes are found. */
static void tv_remote_show_select_view(TvRemoteApp* app) {
    tv_remote_scan_remotes(app);
    submenu_reset(app->select_submenu);

    if(app->remote_count == 0) {
        tv_remote_free_remote_names(app);
        notification_message(app->notifications, &sequence_error);
        return;
    }

    for(size_t i = 0; i < app->remote_count; i++) {
        submenu_add_item(
            app->select_submenu,
            app->remote_names[i],
            (uint32_t)i,
            tv_remote_select_submenu_callback,
            app);
    }
    submenu_add_item(
        app->select_submenu, "Cancel", SELECT_CANCEL_INDEX,
        tv_remote_select_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewSelectRemote);
}

/* ---- Learn Menu view ---- */

typedef enum {
    LearnMenuNew = 0,
    LearnMenuUpdate,
} LearnMenuIndex;

static void tv_remote_learn_menu_callback(void* context, uint32_t index) {
    TvRemoteApp* app = context;
    switch(index) {
    case LearnMenuNew:
        /* Open text input to name the new remote */
        app->text_input_buf[0] = '\0';
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewTextInput);
        break;
    case LearnMenuUpdate:
        app->select_mode = TvRemoteSelectModeOverwrite;
        tv_remote_show_select_view(app);
        break;
    default:
        break;
    }
}

/* ---- TextInput confirm callback ---- */

static void tv_remote_text_input_callback(void* context) {
    TvRemoteApp* app = context;

    /* Sanitise: keep only alphanumeric and underscore */
    char* src = app->text_input_buf;
    char* dst = app->current_remote_name;
    size_t out_len = 0;
    for(; *src && out_len < TV_REMOTE_NAME_MAX; src++) {
        char c = *src;
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_') {
            *dst++ = c;
            out_len++;
        } else if(c == ' ') {
            *dst++ = '_';
            out_len++;
        }
    }
    *dst = '\0';

    if(out_len == 0) {
        /* Empty name – go back */
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewLearnMenu);
        return;
    }

    tv_remote_app_clear_buttons(app);
    app->learn_index = 0;
    view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewLearn);
}

/* ---- Main menu callback ---- */

static void tv_remote_main_menu_callback(void* context, uint32_t index) {
    TvRemoteApp* app = context;
    switch(index) {
    case TvRemoteMenuLearn:
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewLearnMenu);
        break;
    case TvRemoteMenuUse:
        app->select_mode = TvRemoteSelectModeUse;
        tv_remote_show_select_view(app);
        break;
    case TvRemoteMenuDelete:
        app->select_mode = TvRemoteSelectModeDelete;
        tv_remote_show_select_view(app);
        break;
    case TvRemoteMenuButtonMap:
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewButtonMap);
        break;
    default:
        break;
    }
}

/* ---- Button Map view ---- */

typedef struct {
    uint8_t scroll; /**< Top row index (0-based). */
} TvRemoteButtonMapModel;

/* All rows in display order */
static const char* const bmap_lines[] = {
    "  === D-Pad ===",
    "Up       - Up",
    "Down     - Down",
    "Left     - Left",
    "Right    - Right",
    "Up[Hold] - Vol Up",
    "Dn[Hold] - Vol Dn",
    "Lt[Hold] - Ch Dn",
    "Rt[Hold] - Ch Up",
    "  === Center ===",
    "OK       - OK",
    "OK[Hold] - Home",
    "  === Bottom ===",
    "Back     - Back",
    "Back x2  - Power",
    "Back[Hold]- Exit",
};
#define BMAP_LINE_COUNT ((int)(sizeof(bmap_lines) / sizeof(bmap_lines[0])))
#define BMAP_VISIBLE_ROWS 4
#define BMAP_ROW_H 11
#define BMAP_Y0 20

static void tv_remote_bmap_draw_callback(Canvas* canvas, void* model_void) {
    TvRemoteButtonMapModel* model = model_void;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Button Map");
    canvas_set_font(canvas, FontSecondary);
    int top = (int)model->scroll;
    for(int i = 0; i < BMAP_VISIBLE_ROWS; i++) {
        int idx = top + i;
        if(idx >= BMAP_LINE_COUNT) break;
        canvas_draw_str(canvas, 0, BMAP_Y0 + i * BMAP_ROW_H, bmap_lines[idx]);
    }
    /* Scroll indicator */
    if(BMAP_LINE_COUNT > BMAP_VISIBLE_ROWS) {
        int bar_h = 44;
        int thumb_h = bar_h * BMAP_VISIBLE_ROWS / BMAP_LINE_COUNT;
        if(thumb_h < 4) thumb_h = 4;
        int max_scroll = BMAP_LINE_COUNT - BMAP_VISIBLE_ROWS;
        int thumb_y = BMAP_Y0 + (bar_h - thumb_h) * top / (max_scroll > 0 ? max_scroll : 1);
        canvas_draw_line(canvas, 127, BMAP_Y0, 127, BMAP_Y0 + bar_h);
        canvas_draw_box(canvas, 126, thumb_y, 2, thumb_h);
    }
}

static bool tv_remote_bmap_input_callback(InputEvent* event, void* context) {
    TvRemoteApp* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key == InputKeyBack) return false; /* let ViewDispatcher handle */
    int delta = 0;
    if(event->key == InputKeyUp) delta = -1;
    else if(event->key == InputKeyDown) delta = 1;
    else return false;
    with_view_model(
        app->button_map_view,
        TvRemoteButtonMapModel * model,
        {
            int s = (int)model->scroll + delta;
            int max = BMAP_LINE_COUNT - BMAP_VISIBLE_ROWS;
            if(s < 0) s = 0;
            if(s > max) s = max;
            model->scroll = (uint8_t)s;
        },
        true);
    return true;
}

static void tv_remote_bmap_enter_callback(void* context) {
    TvRemoteApp* app = context;
    with_view_model(
        app->button_map_view,
        TvRemoteButtonMapModel * model,
        { model->scroll = 0; },
        true);
}

static View* tv_remote_bmap_view_alloc(TvRemoteApp* app) {
    View* view = view_alloc();
    view_set_context(view, app);
    view_allocate_model(view, ViewModelTypeLocking, sizeof(TvRemoteButtonMapModel));
    view_set_draw_callback(view, tv_remote_bmap_draw_callback);
    view_set_input_callback(view, tv_remote_bmap_input_callback);
    view_set_enter_callback(view, tv_remote_bmap_enter_callback);
    with_view_model(
        view, TvRemoteButtonMapModel * model, { model->scroll = 0; }, true);
    return view;
}

/* ---- Back-button double-tap timer ---- */

static void tv_remote_back_timer_callback(void* context) {
    TvRemoteApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, TvRemoteCustomEventBackTimeout);
}

/* ---- App lifecycle ---- */

TvRemoteApp* tv_remote_app_alloc(void) {
    TvRemoteApp* app = malloc(sizeof(TvRemoteApp));
    memset(app, 0, sizeof(TvRemoteApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
        app->buttons[i].name = tv_remote_button_names[i];
    }

    app->worker = NULL;
    app->worker_active = false;
    app->remote_selected = 0;
    app->tx_active = false;
    app->back_pending = false;
    app->back_timer = furi_timer_alloc(tv_remote_back_timer_callback, FuriTimerTypeOnce, app);

    /* ViewDispatcher */
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* ── Main menu ── */
    app->main_menu = submenu_alloc();
    submenu_add_item(
        app->main_menu, "Learn Remote", TvRemoteMenuLearn, tv_remote_main_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Use Remote", TvRemoteMenuUse, tv_remote_main_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Delete Remote", TvRemoteMenuDelete, tv_remote_main_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Button Map", TvRemoteMenuButtonMap, tv_remote_main_menu_callback, app);
    View* main_menu_view = submenu_get_view(app->main_menu);
    view_set_previous_callback(main_menu_view, tv_remote_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewMainMenu, main_menu_view);

    /* ── Learn menu ── */
    app->learn_menu = submenu_alloc();
    submenu_add_item(
        app->learn_menu, "New Remote", LearnMenuNew, tv_remote_learn_menu_callback, app);
    submenu_add_item(
        app->learn_menu, "Update Remote", LearnMenuUpdate, tv_remote_learn_menu_callback, app);
    View* learn_menu_view = submenu_get_view(app->learn_menu);
    view_set_previous_callback(learn_menu_view, tv_remote_back_to_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewLearnMenu, learn_menu_view);

    /* ── Select Remote submenu ── */
    app->select_submenu = submenu_alloc();
    View* select_view = submenu_get_view(app->select_submenu);
    view_set_previous_callback(select_view, tv_remote_back_to_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewSelectRemote, select_view);

    /* ── TextInput ── */
    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Remote name:");
    text_input_set_result_callback(
        app->text_input,
        tv_remote_text_input_callback,
        app,
        app->text_input_buf,
        TV_REMOTE_NAME_MAX,
        true);
    View* text_input_view = text_input_get_view(app->text_input);
    view_set_previous_callback(text_input_view, tv_remote_back_to_learn_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewTextInput, text_input_view);

    /* ── Learn view ── */
    app->learn_view = tv_remote_learn_view_alloc(app);
    view_set_previous_callback(app->learn_view, tv_remote_back_to_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewLearn, app->learn_view);

    /* ── Remote view ── */
    app->remote_view = tv_remote_remote_view_alloc(app);
    view_set_previous_callback(app->remote_view, tv_remote_back_to_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewRemote, app->remote_view);

    /* ── Button Map view ── */
    app->button_map_view = tv_remote_bmap_view_alloc(app);
    view_set_previous_callback(app->button_map_view, tv_remote_back_to_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewButtonMap, app->button_map_view);

    return app;
}

void tv_remote_app_free(TvRemoteApp* app) {
    furi_assert(app);

    if(app->worker_active && app->worker != NULL) {
        if(app->tx_active) {
            infrared_worker_tx_stop(app->worker);
        } else {
            infrared_worker_rx_stop(app->worker);
        }
        infrared_worker_free(app->worker);
        app->worker = NULL;
    }

    furi_timer_stop(app->back_timer);
    furi_timer_free(app->back_timer);

    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewMainMenu);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewLearnMenu);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewLearn);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewRemote);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewSelectRemote);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewButtonMap);

    submenu_free(app->main_menu);
    submenu_free(app->learn_menu);
    submenu_free(app->select_submenu);
    text_input_free(app->text_input);
    tv_remote_learn_view_free(app->learn_view);
    tv_remote_remote_view_free(app->remote_view);
    view_free(app->button_map_view);

    view_dispatcher_free(app->view_dispatcher);

    tv_remote_app_clear_buttons(app);
    tv_remote_free_remote_names(app);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
}

/* ---- Entry point ---- */

int32_t flipper_tv_remote_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TV_REMOTE_APP_TAG, "Starting TV Remote app");

    TvRemoteApp* app = tv_remote_app_alloc();

    view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewMainMenu);
    view_dispatcher_run(app->view_dispatcher);

    tv_remote_app_free(app);
    FURI_LOG_I(TV_REMOTE_APP_TAG, "TV Remote app stopped");
    return 0;
}
