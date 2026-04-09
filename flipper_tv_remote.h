/**
 * @file flipper_tv_remote.h
 * @brief Flipper TV Remote - record TV remote buttons and replay them.
 *
 * This app uses the Flipper Zero IR transceiver to capture IR signals from a
 * physical TV remote and then maps them to the Flipper's buttons so the
 * Flipper can act as a TV remote.
 */

#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <input/input.h>
#include <infrared_worker.h>
#include <infrared.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TV_REMOTE_APP_TAG "TvRemote"
#define TV_REMOTE_FILE_HEADER "IR signals file"
#define TV_REMOTE_FILE_VERSION 1
#define TV_REMOTE_FILE_DIR ANY_PATH("infrared")
#define TV_REMOTE_FILE_PATH ANY_PATH("infrared/TV_Remote.ir")

/** Number of buttons the app can learn and replay. */
#define TV_BUTTON_COUNT 14

/** Indices for each learnable TV button. */
typedef enum {
    TvButtonPower = 0,
    TvButtonMute,
    TvButtonVolUp,
    TvButtonVolDn,
    TvButtonChUp,
    TvButtonChDn,
    TvButtonUp,
    TvButtonDown,
    TvButtonLeft,
    TvButtonRight,
    TvButtonOk,
    TvButtonBack,
    TvButtonHome,
    TvButtonPlayPause,
} TvButton;

/** View identifiers used with the ViewDispatcher. */
typedef enum {
    TvRemoteViewMainMenu = 0,
    TvRemoteViewLearn,
    TvRemoteViewRemote,
} TvRemoteViewId;

/** Main menu item indices. */
typedef enum {
    TvRemoteMenuLearn = 0,
    TvRemoteMenuUse,
} TvRemoteMenuItem;

/** Custom events sent via ViewDispatcher from ISR/worker callbacks. */
typedef enum {
    TvRemoteCustomEventSignalReceived = 0,
} TvRemoteCustomEvent;

/** Opaque app context type. */
typedef struct TvRemoteApp TvRemoteApp;

/** Stored IR signal: either a decoded (parsed) message or raw timings. */
typedef struct {
    bool is_raw;
    InfraredMessage message; /**< Valid when is_raw == false. */
    uint32_t* timings;       /**< Heap-allocated raw timing array; NULL when unused. */
    size_t timings_size;
    uint32_t frequency;
    float duty_cycle;
} TvRemoteIrSignal;

/** Per-button state: name, learned flag, and stored IR signal. */
typedef struct {
    const char* name;  /**< Human-readable button name (stored in .ir file). */
    TvRemoteIrSignal signal; /**< Stored IR signal data. */
    bool learned; /**< True when a signal has been successfully recorded. */
} TvRemoteButton;

/** Application state. */
struct TvRemoteApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;

    /* Views */
    Submenu* main_menu;
    View* learn_view;
    View* remote_view;

    /* Button storage */
    TvRemoteButton buttons[TV_BUTTON_COUNT];

    /* Learning state */
    uint8_t learn_index; /**< Index of the button currently being learned. */
    bool learn_signal_received; /**< Set when IR callback fires during learn. */

    /* IR worker */
    InfraredWorker* worker;
    bool worker_active;

    /* TX state (for remote view button hold) */
    uint8_t remote_selected; /**< Button index highlighted in the remote view. */
    bool tx_active; /**< True while sending IR in remote view. */
    uint8_t remote_pressed_keys; /**< Bitmask of physically held d-pad/ok keys. */
    bool remote_held_long; /**< True once InputTypeLong fires (alt action started). */

    /* Back-button double-tap detection */
    uint32_t last_back_tick; /**< furi_get_tick() of last Back short press. */
};

/** Canonical button names (must match .ir file entries). */
extern const char* const tv_remote_button_names[TV_BUTTON_COUNT];

/* ---- App lifecycle ---- */
TvRemoteApp* tv_remote_app_alloc(void);
void tv_remote_app_free(TvRemoteApp* app);

/* ---- File I/O ---- */
bool tv_remote_app_save(TvRemoteApp* app);
bool tv_remote_app_load(TvRemoteApp* app);

/* ---- Entry point ---- */
int32_t flipper_tv_remote_app(void* p);
