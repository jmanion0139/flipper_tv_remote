/**
 * @file flipper_tv_remote.c
 * @brief Flipper TV Remote – main entry point, app lifecycle, and file I/O.
 */

#include "flipper_tv_remote.h"
#include "views/tv_remote_learn.h"
#include "views/tv_remote_remote.h"

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
};

/* ---- Main menu callbacks ---- */

static void tv_remote_main_menu_callback(void* context, uint32_t index) {
    TvRemoteApp* app = context;
    switch(index) {
    case TvRemoteMenuLearn:
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewLearn);
        break;
    case TvRemoteMenuUse:
        view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewRemote);
        break;
    default:
        break;
    }
}

static uint32_t tv_remote_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t tv_remote_back_to_menu_callback(void* context) {
    UNUSED(context);
    return TvRemoteViewMainMenu;
}

/* ---- File I/O ---- */

bool tv_remote_app_save(TvRemoteApp* app) {
    /* Skip save if no buttons have been learned to avoid overwriting a valid file. */
    bool any_learned = false;
    for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
        if(app->buttons[i].learned) {
            any_learned = true;
            break;
        }
    }
    if(!any_learned) {
        FURI_LOG_I(TV_REMOTE_APP_TAG, "No buttons learned – skipping save");
        return true;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, TV_REMOTE_FILE_DIR);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    bool success = false;
    do {
        if(!flipper_format_file_open_always(ff, TV_REMOTE_FILE_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, TV_REMOTE_FILE_HEADER, TV_REMOTE_FILE_VERSION))
            break;

        success = true;
        for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
            if(!app->buttons[i].learned) continue;
            if(!infrared_signal_save(app->buttons[i].signal, ff, tv_remote_button_names[i])) {
                success = false;
                break;
            }
        }
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return success;
}

bool tv_remote_app_load(TvRemoteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* name = furi_string_alloc();
    InfraredSignal* tmp = infrared_signal_alloc();

    bool success = false;
    do {
        if(!flipper_format_buffered_file_open_existing(ff, TV_REMOTE_FILE_PATH)) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        if(!flipper_format_read_header(ff, header, &version)) {
            furi_string_free(header);
            break;
        }
        furi_string_free(header);

        /* Read signals until we can't load any more. */
        while(infrared_signal_load_auto(tmp, ff, name)) {
            const char* name_cstr = furi_string_get_cstr(name);
            for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
                if(strcmp(name_cstr, tv_remote_button_names[i]) == 0) {
                    infrared_signal_set_signal(app->buttons[i].signal, tmp);
                    app->buttons[i].learned = true;
                    break;
                }
            }
        }
        success = true;
    } while(false);

    infrared_signal_free(tmp);
    furi_string_free(name);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return success;
}

/* ---- App lifecycle ---- */

TvRemoteApp* tv_remote_app_alloc(void) {
    TvRemoteApp* app = malloc(sizeof(TvRemoteApp));

    /* Open system records */
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    /* Initialise button storage */
    for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
        app->buttons[i].name = tv_remote_button_names[i];
        app->buttons[i].signal = infrared_signal_alloc();
        app->buttons[i].learned = false;
    }

    app->learn_index = 0;
    app->learn_signal_received = false;
    app->worker = NULL;
    app->worker_active = false;
    app->remote_selected = 0;
    app->tx_active = false;

    /* ViewDispatcher */
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Main menu */
    app->main_menu = submenu_alloc();
    submenu_add_item(
        app->main_menu, "Learn Remote", TvRemoteMenuLearn, tv_remote_main_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Use Remote", TvRemoteMenuUse, tv_remote_main_menu_callback, app);
    View* main_menu_view = submenu_get_view(app->main_menu);
    view_set_previous_callback(main_menu_view, tv_remote_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewMainMenu, main_menu_view);

    /* Learn view */
    app->learn_view = tv_remote_learn_view_alloc(app);
    view_set_previous_callback(app->learn_view, tv_remote_back_to_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewLearn, app->learn_view);

    /* Remote view */
    app->remote_view = tv_remote_remote_view_alloc(app);
    view_set_previous_callback(app->remote_view, tv_remote_back_to_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, TvRemoteViewRemote, app->remote_view);

    return app;
}

void tv_remote_app_free(TvRemoteApp* app) {
    furi_assert(app);

    /* Stop any active worker */
    if(app->worker_active && app->worker != NULL) {
        if(app->tx_active) {
            infrared_worker_tx_stop(app->worker);
        } else {
            infrared_worker_rx_stop(app->worker);
        }
        infrared_worker_free(app->worker);
        app->worker = NULL;
    }

    /* Remove and free views */
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewMainMenu);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewLearn);
    view_dispatcher_remove_view(app->view_dispatcher, TvRemoteViewRemote);

    submenu_free(app->main_menu);
    tv_remote_learn_view_free(app->learn_view);
    tv_remote_remote_view_free(app->remote_view);

    view_dispatcher_free(app->view_dispatcher);

    /* Free button signals */
    for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
        infrared_signal_free(app->buttons[i].signal);
    }

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
}

/* ---- Entry point ---- */

int32_t flipper_tv_remote_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TV_REMOTE_APP_TAG, "Starting TV Remote app");

    TvRemoteApp* app = tv_remote_app_alloc();

    /* Try to load any previously saved remote */
    tv_remote_app_load(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, TvRemoteViewMainMenu);
    view_dispatcher_run(app->view_dispatcher);

    /* Save on exit so learned signals are persisted */
    tv_remote_app_save(app);

    tv_remote_app_free(app);
    FURI_LOG_I(TV_REMOTE_APP_TAG, "TV Remote app stopped");
    return 0;
}
