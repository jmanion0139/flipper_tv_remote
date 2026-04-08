/**
 * @file views/tv_remote_remote.c
 * @brief Remote control view – landscape d-pad layout.
 *
 * The display is rotated 90° CCW (ViewOrientationVerticalFlip) so the canvas
 * is 64 wide × 128 tall and the Flipper's d-pad sits on the right side.
 *
 * Physical key → IR action mapping:
 *   Press          Hold
 *   ─────          ────
 *   Up   → Up      Up   → Vol_up
 *   Down → Down    Down → Vol_dn
 *   Left → Left    Left → Ch_dn
 *   Right→ Right   Right→ Ch_up
 *   Ok   → Ok      Ok   → Home
 *   Back → Back    Back → exit app
 *   Back ×2 (double-tap) → Power
 */

#include "tv_remote_remote.h"
#include "../flipper_tv_remote.h"

#include <gui/elements.h>

/* ---- Layout constants (64×128 portrait canvas) ---- */

#define DISP_W 64
#define DISP_H 128

/* Full-width button box (Up / Down) */
#define BOX_FULL_W 50
#define BOX_H      12
#define BOX_FULL_X ((DISP_W - BOX_FULL_W) / 2) /* 7 */

/* Middle-row boxes (Left / Ok / Right) */
#define BOX_SIDE_W 18
#define BOX_MID_W  20
#define BOX_GAP    3
#define BOX_LEFT_X 1
#define BOX_OK_X   (BOX_LEFT_X + BOX_SIDE_W + BOX_GAP)   /* 22 */
#define BOX_RIGHT_X (BOX_OK_X + BOX_MID_W + BOX_GAP)     /* 45 */

/* Vertical positions */
#define TITLE_Y     5
#define SEP1_Y      11
#define UP_Y        15
#define MID_Y       38
#define DOWN_Y      68
#define SEP2_Y      90
#define BACK_Y      94
#define BACK_W      30
#define BACK_X      ((DISP_W - BACK_W) / 2)

/* Double-tap detection window (ms) */
#define DOUBLE_TAP_MS 400

/* Brief IR burst duration for single-shot buttons (ms) */
#define IR_BURST_MS 150

/* ---- Press / hold button mapping ---- */

typedef struct {
    uint8_t press_btn;  /**< TvButton index for short press. */
    uint8_t hold_btn;   /**< TvButton index for long press.  */
} ButtonMapping;

static const ButtonMapping key_map[] = {
    [InputKeyUp]    = {TvButtonUp,    TvButtonVolUp},
    [InputKeyDown]  = {TvButtonDown,  TvButtonVolDn},
    [InputKeyLeft]  = {TvButtonLeft,  TvButtonChDn},
    [InputKeyRight] = {TvButtonRight, TvButtonChUp},
    [InputKeyOk]    = {TvButtonOk,    TvButtonHome},
};

/* ---- View model ---- */

typedef struct {
    int8_t active_button;  /**< TvButton index being sent (-1 = idle). */
    bool active_is_hold;   /**< True when the hold action is active. */
    bool learned[TV_BUTTON_COUNT]; /**< Snapshot – which buttons have signals. */
} TvRemoteRemoteModel;

/* ---- IR worker TX callback ---- */

/*
 * TX callback – runs from the InfraredWorker thread.
 *
 * `app->tx_active` and `app->remote_selected` are written from the UI thread
 * only via tv_remote_tx_start (before the worker starts) and tv_remote_tx_stop
 * (after which the worker receives InfraredStatusDone and halts).  The boolean
 * flag `tx_active` therefore acts as a release/acquire barrier: it is set to
 * true before infrared_worker_tx_start and cleared before
 * infrared_worker_tx_stop, making concurrent access safe on ARM Cortex-M.
 */
static InfraredWorkerGetSignalResponse
    tv_remote_tx_callback(void* context, InfraredWorker* instance) {
    TvRemoteApp* app = context;

    if(!app->tx_active) {
        return InfraredWorkerGetSignalResponseStop;
    }

    uint8_t idx = app->remote_selected;
    if(idx < TV_BUTTON_COUNT && app->buttons[idx].learned) {
        TvRemoteIrSignal* sig = &app->buttons[idx].signal;
        if(sig->is_raw) {
            infrared_worker_set_raw_signal(
                instance, sig->timings, sig->timings_size, sig->frequency, sig->duty_cycle);
        } else {
            infrared_worker_set_decoded_signal(instance, &sig->message);
        }
        return InfraredWorkerGetSignalResponseNew;
    }
    return InfraredWorkerGetSignalResponseStop;
}

/* ---- TX start / stop helpers ---- */

static void tv_remote_tx_start(TvRemoteApp* app, uint8_t button_index) {
    if(app->worker_active) return;
    if(button_index >= TV_BUTTON_COUNT) return;
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

/** Return true when @p btn is the one currently being transmitted. */
static inline bool is_active(const TvRemoteRemoteModel* m, uint8_t btn) {
    return m->active_button == (int8_t)btn;
}

/** Draw a rounded-rect button cell; invert colours when active. */
static void draw_btn(
    Canvas* canvas,
    const TvRemoteRemoteModel* model,
    int x,
    int y,
    int w,
    int h,
    uint8_t btn,
    const char* label) {
    bool active = is_active(model, btn);
    bool learned = model->learned[btn];

    if(active) {
        canvas_draw_rbox(canvas, x, y, w, h, 2);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, w, h, 2);
    }

    const char* text = learned ? label : "--";
    canvas_draw_str_aligned(canvas, x + w / 2, y + h / 2 + 1, AlignCenter, AlignCenter, text);

    if(active) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void tv_remote_remote_draw_callback(Canvas* canvas, void* model_void) {
    TvRemoteRemoteModel* model = model_void;
    canvas_clear(canvas);

    /* ── Title bar ── */
    canvas_set_font(canvas, FontPrimary);
    if(model->active_button >= 0) {
        canvas_draw_str_aligned(canvas, DISP_W / 2, TITLE_Y, AlignCenter, AlignCenter, "SENDING");
    } else {
        canvas_draw_str_aligned(
            canvas, DISP_W / 2, TITLE_Y, AlignCenter, AlignCenter, "TV Remote");
    }
    canvas_draw_line(canvas, 0, SEP1_Y, DISP_W - 1, SEP1_Y);

    canvas_set_font(canvas, FontSecondary);

    /* ── Up button ── */
    draw_btn(canvas, model, BOX_FULL_X, UP_Y, BOX_FULL_W, BOX_H, TvButtonUp, "Up");
    canvas_draw_str_aligned(
        canvas, DISP_W / 2, UP_Y + BOX_H + 5, AlignCenter, AlignCenter, "Vol. Up [HOLD]");

    /* ── Middle row: Left / Ok / Right ── */
    draw_btn(canvas, model, BOX_LEFT_X, MID_Y, BOX_SIDE_W, BOX_H, TvButtonLeft, "Left");
    draw_btn(canvas, model, BOX_OK_X, MID_Y, BOX_MID_W, BOX_H, TvButtonOk, "Ok");
    draw_btn(canvas, model, BOX_RIGHT_X, MID_Y, BOX_SIDE_W, BOX_H, TvButtonRight, "Right");

    /* Hold labels under each middle button */
    int hold_y = MID_Y + BOX_H + 4;
    canvas_draw_str_aligned(
        canvas, BOX_LEFT_X + BOX_SIDE_W / 2, hold_y, AlignCenter, AlignCenter, "Ch Dn");
    canvas_draw_str_aligned(
        canvas, BOX_LEFT_X + BOX_SIDE_W / 2, hold_y + 8, AlignCenter, AlignCenter, "[HOLD]");

    canvas_draw_str_aligned(
        canvas, BOX_OK_X + BOX_MID_W / 2, hold_y, AlignCenter, AlignCenter, "Home");
    canvas_draw_str_aligned(
        canvas, BOX_OK_X + BOX_MID_W / 2, hold_y + 8, AlignCenter, AlignCenter, "[HOLD]");

    canvas_draw_str_aligned(
        canvas, BOX_RIGHT_X + BOX_SIDE_W / 2, hold_y, AlignCenter, AlignCenter, "Ch Up");
    canvas_draw_str_aligned(
        canvas, BOX_RIGHT_X + BOX_SIDE_W / 2, hold_y + 8, AlignCenter, AlignCenter, "[HOLD]");

    /* ── Down button ── */
    draw_btn(canvas, model, BOX_FULL_X, DOWN_Y, BOX_FULL_W, BOX_H, TvButtonDown, "Down");
    canvas_draw_str_aligned(
        canvas, DISP_W / 2, DOWN_Y + BOX_H + 5, AlignCenter, AlignCenter, "Vol. Dn [HOLD]");

    /* ── Separator ── */
    canvas_draw_line(canvas, 0, SEP2_Y, DISP_W - 1, SEP2_Y);

    /* ── Back button ── */
    draw_btn(canvas, model, BACK_X, BACK_Y, BACK_W, BOX_H, TvButtonBack, "Back");
    canvas_draw_str_aligned(
        canvas, DISP_W / 2, BACK_Y + BOX_H + 5, AlignCenter, AlignCenter, "Hold: Exit");
    canvas_draw_str_aligned(
        canvas, DISP_W / 2, BACK_Y + BOX_H + 13, AlignCenter, AlignCenter, "2xTap: Power");

    /* Show hold labels while a hold action is active */
    if(model->active_button >= 0 && model->active_is_hold) {
        /* Highlight the hold-action label by drawing the active hold-button box */
        uint8_t hb = (uint8_t)model->active_button;
        if(hb == TvButtonVolUp) {
            draw_btn(
                canvas, model, BOX_FULL_X, UP_Y, BOX_FULL_W, BOX_H, TvButtonVolUp, "Vol+");
        } else if(hb == TvButtonVolDn) {
            draw_btn(
                canvas, model, BOX_FULL_X, DOWN_Y, BOX_FULL_W, BOX_H, TvButtonVolDn, "Vol-");
        } else if(hb == TvButtonChDn) {
            draw_btn(
                canvas, model, BOX_LEFT_X, MID_Y, BOX_SIDE_W, BOX_H, TvButtonChDn, "ChDn");
        } else if(hb == TvButtonChUp) {
            draw_btn(
                canvas,
                model,
                BOX_RIGHT_X,
                MID_Y,
                BOX_SIDE_W,
                BOX_H,
                TvButtonChUp,
                "ChUp");
        } else if(hb == TvButtonHome) {
            draw_btn(canvas, model, BOX_OK_X, MID_Y, BOX_MID_W, BOX_H, TvButtonHome, "Home");
        }
    }
}

/* ---- Helper: update model from app state ---- */

static void tv_remote_remote_update_model(TvRemoteApp* app, int8_t active, bool is_hold) {
    with_view_model(
        app->remote_view,
        TvRemoteRemoteModel * model,
        {
            model->active_button = active;
            model->active_is_hold = is_hold;
            for(size_t i = 0; i < TV_BUTTON_COUNT; i++) {
                model->learned[i] = app->buttons[i].learned;
            }
        },
        true);
}

/* ---- Brief IR burst (for Back / Power single-shot) ---- */

static void tv_remote_ir_burst(TvRemoteApp* app, uint8_t button_index) {
    tv_remote_tx_start(app, button_index);
    if(app->tx_active) {
        furi_delay_ms(IR_BURST_MS);
        tv_remote_tx_stop(app);
    }
}

/* ---- Input callback ---- */

static bool tv_remote_remote_input_callback(InputEvent* event, void* context) {
    TvRemoteApp* app = context;

    /* ── Back button: hold to exit, short = Back IR, double-tap = Power ── */
    if(event->key == InputKeyBack) {
        if(event->type == InputTypePress) {
            /* Consume press so ViewDispatcher doesn't navigate away */
            return true;
        }
        if(event->type == InputTypeLong) {
            /* Hold back = exit: stop any active TX, let ViewDispatcher navigate */
            tv_remote_tx_stop(app);
            tv_remote_remote_update_model(app, -1, false);
            return false; /* ViewDispatcher handles exit */
        }
        if(event->type == InputTypeShort) {
            uint32_t now = furi_get_tick();
            if((now - app->last_back_tick) < furi_ms_to_ticks(DOUBLE_TAP_MS)) {
                /* Double-tap → Power */
                tv_remote_ir_burst(app, TvButtonPower);
                tv_remote_remote_update_model(app, -1, false);
                app->last_back_tick = 0; /* reset so a third tap = Back again */
            } else {
                /* Single tap → Back IR */
                tv_remote_ir_burst(app, TvButtonBack);
                tv_remote_remote_update_model(app, -1, false);
                app->last_back_tick = now;
            }
            return true;
        }
        if(event->type == InputTypeRelease) {
            return true; /* consume release */
        }
        return false;
    }

    /* ── D-pad & Ok: press = primary, long = hold action ── */
    if(event->key > InputKeyMAX) return false;
    if(event->key == InputKeyBack) return false; /* handled above */

    /* Only handle keys with mappings */
    if(event->key != InputKeyUp && event->key != InputKeyDown &&
       event->key != InputKeyLeft && event->key != InputKeyRight &&
       event->key != InputKeyOk) {
        return false;
    }

    const ButtonMapping* map = &key_map[event->key];

    if(event->type == InputTypePress) {
        /* Start sending the press action */
        tv_remote_tx_start(app, map->press_btn);
        tv_remote_remote_update_model(app, (int8_t)map->press_btn, false);
        return true;
    }

    if(event->type == InputTypeLong) {
        /* Switch to hold action */
        tv_remote_tx_stop(app);
        tv_remote_tx_start(app, map->hold_btn);
        tv_remote_remote_update_model(app, (int8_t)map->hold_btn, true);
        return true;
    }

    if(event->type == InputTypeRelease) {
        tv_remote_tx_stop(app);
        tv_remote_remote_update_model(app, -1, false);
        return true;
    }

    /* InputTypeRepeat – continue sending (no change) */
    if(event->type == InputTypeRepeat) {
        return true;
    }

    return false;
}

/* ---- Enter / exit callbacks ---- */

static void tv_remote_remote_enter_callback(void* context) {
    TvRemoteApp* app = context;
    app->tx_active = false;
    app->last_back_tick = 0;
    tv_remote_remote_update_model(app, -1, false);
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
    view_set_orientation(view, ViewOrientationVertical);

    with_view_model(
        view,
        TvRemoteRemoteModel * model,
        {
            model->active_button = -1;
            model->active_is_hold = false;
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
