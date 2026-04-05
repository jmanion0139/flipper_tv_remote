/**
 * @file views/tv_remote_remote.c
 * @brief Remote control view implementation.
 *
 * Layout (128×64 screen, 4 cols × 3 rows of 32×20 px cells):
 *
 *   Row 0:  [PWR] [MUT] [V+] [V-]
 *   Row 1:  [C+]  [C-]  [↑]  [↓]
 *   Row 2:  [←]   [→]   [OK] [BCK]
 *
 * Navigation:
 *   D-pad Up/Down/Left/Right  – move cursor
 *   OK (press)                – start sending the selected IR signal
 *   OK (release)              – stop sending
 *   Back                      – return to main menu
 *
 * Button mapping shortcuts (direct physical button → IR action):
 *   Up    → Vol_up     Right → Ch_up
 *   Down  → Vol_dn     Left  → Ch_dn
 *   OK    → OK         Back  → (navigate back)
 *
 * When a button is not learned its cell is shown dimmed with "--".
 */

#include "tv_remote_remote.h"
#include "../flipper_tv_remote.h"

#include <gui/elements.h>

/* ---- Layout constants ---- */

#define CELL_W 32
#define CELL_H 18
#define COLS 4
#define ROWS 3
#define GRID_X 0
#define GRID_Y 10 /* leaves 10 px for the title bar at top */

/* Abbreviated labels displayed in each cell (same order as TvButton enum). */
static const char* const cell_labels[TV_BUTTON_COUNT] = {
    "PWR", /* TvButtonPower  */
    "MUT", /* TvButtonMute   */
    "V+",  /* TvButtonVolUp  */
    "V-",  /* TvButtonVolDn  */
    "C+",  /* TvButtonChUp   */
    "C-",  /* TvButtonChDn   */
    "\x18", /* TvButtonUp    (↑, code 0x18 in 5x7 font) */
    "\x19", /* TvButtonDown  (↓) */
    "\x1B", /* TvButtonLeft  (←) */
    "\x1A", /* TvButtonRight (→) */
    "OK",  /* TvButtonOk     */
    "BCK", /* TvButtonBack   */
};

/* ---- View model ---- */

typedef struct {
    uint8_t selected; /**< Currently highlighted cell (0 .. TV_BUTTON_COUNT-1). */
    bool learned[TV_BUTTON_COUNT]; /**< Snapshot – which buttons have signals. */
    bool sending; /**< True while IR is being transmitted. */
} TvRemoteRemoteModel;

/* ---- IR worker TX callback ---- */

static InfraredStatus tv_remote_tx_callback(void* context, InfraredWorker* instance) {
    TvRemoteApp* app = context;

    if(!app->tx_active) {
        return InfraredStatusDone;
    }

    uint8_t idx = app->remote_selected;
    if(idx < TV_BUTTON_COUNT && app->buttons[idx].learned) {
        infrared_worker_tx_set_signal_to_worker(instance, app->buttons[idx].signal);
        return InfraredStatusOk;
    }
    return InfraredStatusDone;
}

/* ---- TX start / stop helpers ---- */

static void tv_remote_tx_start(TvRemoteApp* app, uint8_t button_index) {
    if(app->worker_active) return;
    if(!app->buttons[button_index].learned) return;

    app->remote_selected = button_index;
    app->tx_active = true;
    app->worker = infrared_worker_alloc();
    infrared_worker_tx_set_get_signal_callback(app->worker, tv_remote_tx_callback, app);
    infrared_worker_tx_start(app->worker);
    app->worker_active = true;

    notification_message(app->notifications, &sequence_blink_green_10);
}

static void tv_remote_tx_stop(TvRemoteApp* app) {
    if(!app->worker_active || !app->tx_active) return;
    infrared_worker_tx_stop(app->worker);
    infrared_worker_free(app->worker);
    app->worker = NULL;
    app->worker_active = false;
    app->tx_active = false;
}

/* ---- Draw callback ---- */

static void tv_remote_remote_draw_callback(Canvas* canvas, void* model_void) {
    TvRemoteRemoteModel* model = model_void;
    canvas_clear(canvas);

    /* Title bar – show ">>> SENDING <<<" while transmitting, otherwise app name */
    canvas_set_font(canvas, FontSecondary);
    if(model->sending) {
        canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, ">>> SENDING >>>");
    } else {
        canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "TV Remote");
    }

    /* Separator line between title and grid */
    canvas_draw_line(canvas, 0, 9, 127, 9);

    /* Draw 4×3 button grid */
    for(uint8_t i = 0; i < TV_BUTTON_COUNT; i++) {
        uint8_t col = i % COLS;
        uint8_t row = i / COLS;
        int x = GRID_X + col * CELL_W;
        int y = GRID_Y + row * CELL_H;

        bool selected = (i == model->selected);
        bool learned = model->learned[i];

        if(selected) {
            canvas_draw_rbox(canvas, x + 1, y + 1, CELL_W - 2, CELL_H - 2, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x + 1, y + 1, CELL_W - 2, CELL_H - 2, 2);
        }

        const char* label = learned ? cell_labels[i] : "--";
        canvas_draw_str_aligned(
            canvas, x + CELL_W / 2, y + CELL_H / 2 - 3, AlignCenter, AlignTop, label);

        if(selected) {
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

/* ---- Helper: update model from app state ---- */

static void tv_remote_remote_update_model(TvRemoteApp* app) {
    with_view_model(
        app->remote_view,
        TvRemoteRemoteModel * model,
        {
            model->selected = app->remote_selected;
            model->sending = app->tx_active;
            for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
                model->learned[i] = app->buttons[i].learned;
            }
        },
        true);
}

/* ---- Input callback ---- */

static bool tv_remote_remote_input_callback(InputEvent* event, void* context) {
    TvRemoteApp* app = context;

    /* Handle OK press/release for the selected button (IR send) */
    if(event->key == InputKeyOk) {
        if(event->type == InputTypePress) {
            tv_remote_tx_start(app, app->remote_selected);
            tv_remote_remote_update_model(app);
            return true;
        } else if(event->type == InputTypeRelease) {
            tv_remote_tx_stop(app);
            tv_remote_remote_update_model(app);
            return true;
        }
        return false;
    }

    /* Only handle short presses for navigation */
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    int8_t delta = 0;
    switch(event->key) {
    case InputKeyUp:
        delta = -COLS;
        break;
    case InputKeyDown:
        delta = +COLS;
        break;
    case InputKeyLeft:
        delta = -1;
        break;
    case InputKeyRight:
        delta = +1;
        break;
    case InputKeyBack:
        /* Let ViewDispatcher handle Back (goes to previous view). */
        return false;
    default:
        return false;
    }

    int8_t next = (int8_t)app->remote_selected + delta;
    if(next < 0) next = 0;
    if(next >= (int8_t)TV_BUTTON_COUNT) next = TV_BUTTON_COUNT - 1;
    app->remote_selected = (uint8_t)next;
    tv_remote_remote_update_model(app);
    return true;
}

/* ---- Enter / exit callbacks ---- */

static void tv_remote_remote_enter_callback(void* context) {
    TvRemoteApp* app = context;
    app->tx_active = false;
    tv_remote_remote_update_model(app);
}

static void tv_remote_remote_exit_callback(void* context) {
    TvRemoteApp* app = context;
    tv_remote_tx_stop(app);
}

/* ---- Public API ---- */

View* tv_remote_remote_view_alloc(TvRemoteApp* app) {
    View* view = view_alloc();
    view_set_context(view, app);
    view_allocate_model(view, ViewModelTypeLocking, sizeof(TvRemoteRemoteModel));
    view_set_draw_callback(view, tv_remote_remote_draw_callback);
    view_set_input_callback(view, tv_remote_remote_input_callback);
    view_set_enter_callback(view, tv_remote_remote_enter_callback);
    view_set_exit_callback(view, tv_remote_remote_exit_callback);

    with_view_model(
        view,
        TvRemoteRemoteModel * model,
        {
            model->selected = 0;
            model->sending = false;
            for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
                model->learned[i] = false;
            }
        },
        true);

    return view;
}

void tv_remote_remote_view_free(View* view) {
    view_free(view);
}
