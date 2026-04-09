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
    uint8_t pressed_keys;  /**< Bitmask of physically held d-pad/ok keys. */
} TvRemoteRemoteModel;

/* Bitmask bits for pressed_keys */
#define KEY_BIT_UP    (1u << 0)
#define KEY_BIT_DOWN  (1u << 1)
#define KEY_BIT_LEFT  (1u << 2)
#define KEY_BIT_RIGHT (1u << 3)
#define KEY_BIT_OK    (1u << 4)

static inline uint8_t key_to_bit(InputKey k) {
    switch(k) {
    case InputKeyUp:    return KEY_BIT_UP;
    case InputKeyDown:  return KEY_BIT_DOWN;
    case InputKeyLeft:  return KEY_BIT_LEFT;
    case InputKeyRight: return KEY_BIT_RIGHT;
    case InputKeyOk:    return KEY_BIT_OK;
    default:            return 0;
    }
}

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

/** True when the key should be drawn filled (sending IR or physically held). */
static inline bool is_dir_active(const TvRemoteRemoteModel* m, InputKey dir) {
    /* Physical hold check */
    if(m->pressed_keys & key_to_bit(dir)) return true;
    /* IR-sending check (covers hold actions mapped back to the physical key) */
    int8_t b = m->active_button;
    switch(dir) {
    case InputKeyUp:    return b == (int8_t)TvButtonUp    || b == (int8_t)TvButtonVolUp;
    case InputKeyDown:  return b == (int8_t)TvButtonDown  || b == (int8_t)TvButtonVolDn;
    case InputKeyLeft:  return b == (int8_t)TvButtonLeft  || b == (int8_t)TvButtonChDn;
    case InputKeyRight: return b == (int8_t)TvButtonRight || b == (int8_t)TvButtonChUp;
    case InputKeyOk:    return b == (int8_t)TvButtonOk    || b == (int8_t)TvButtonHome;
    default:            return false;
    }
}

static void tv_remote_remote_draw_callback(Canvas* canvas, void* model_void) {
    TvRemoteRemoteModel* model = model_void;
    canvas_clear(canvas);

    // Center of d-pad
    const int cx = DISP_W / 2;
    const int cy = DISP_H / 2 - 8;
    const int arrow_len = 14;
    const int arrow_w = 7;
    const int dot_r = 4;

    canvas_set_font(canvas, FontSecondary);

    // Arrow geometry
    const size_t ab = (size_t)(arrow_w * 2); // triangle base
    const size_t ah = (size_t)(arrow_len - 4); // triangle height
    // Outline vertices relative to each tip:
    //   Up:    tip (cx, cy-al), base corners (cx±aw, cy-al+ah)
    //   Down:  tip (cx, cy+al), base corners (cx±aw, cy+al-ah)
    //   Left:  tip (cx-al, cy), base corners (cx-al+ah, cy±aw)
    //   Right: tip (cx+al, cy), base corners (cx+al-ah, cy±aw)

#define DRAW_ARROW_OUTLINE_UP() do { \
    canvas_draw_line(canvas, cx, cy - arrow_len, cx - arrow_w, cy - arrow_len + (int)ah); \
    canvas_draw_line(canvas, cx, cy - arrow_len, cx + arrow_w, cy - arrow_len + (int)ah); \
    canvas_draw_line(canvas, cx - arrow_w, cy - arrow_len + (int)ah, cx + arrow_w, cy - arrow_len + (int)ah); \
} while(0)
#define DRAW_ARROW_OUTLINE_DOWN() do { \
    canvas_draw_line(canvas, cx, cy + arrow_len, cx - arrow_w, cy + arrow_len - (int)ah); \
    canvas_draw_line(canvas, cx, cy + arrow_len, cx + arrow_w, cy + arrow_len - (int)ah); \
    canvas_draw_line(canvas, cx - arrow_w, cy + arrow_len - (int)ah, cx + arrow_w, cy + arrow_len - (int)ah); \
} while(0)
#define DRAW_ARROW_OUTLINE_LEFT() do { \
    canvas_draw_line(canvas, cx - arrow_len, cy, cx - arrow_len + (int)ah, cy - arrow_w); \
    canvas_draw_line(canvas, cx - arrow_len, cy, cx - arrow_len + (int)ah, cy + arrow_w); \
    canvas_draw_line(canvas, cx - arrow_len + (int)ah, cy - arrow_w, cx - arrow_len + (int)ah, cy + arrow_w); \
} while(0)
#define DRAW_ARROW_OUTLINE_RIGHT() do { \
    canvas_draw_line(canvas, cx + arrow_len, cy, cx + arrow_len - (int)ah, cy - arrow_w); \
    canvas_draw_line(canvas, cx + arrow_len, cy, cx + arrow_len - (int)ah, cy + arrow_w); \
    canvas_draw_line(canvas, cx + arrow_len - (int)ah, cy - arrow_w, cx + arrow_len - (int)ah, cy + arrow_w); \
} while(0)

    // Up
    if(is_dir_active(model, InputKeyUp)) {
        canvas_draw_triangle(canvas, cx, cy - arrow_len, ab, ah, CanvasDirectionBottomToTop);
    } else {
        DRAW_ARROW_OUTLINE_UP();
    }
    // Down
    if(is_dir_active(model, InputKeyDown)) {
        canvas_draw_triangle(canvas, cx, cy + arrow_len, ab, ah, CanvasDirectionTopToBottom);
    } else {
        DRAW_ARROW_OUTLINE_DOWN();
    }
    // Left
    if(is_dir_active(model, InputKeyLeft)) {
        canvas_draw_triangle(canvas, cx - arrow_len, cy, ab, ah, CanvasDirectionRightToLeft);
    } else {
        DRAW_ARROW_OUTLINE_LEFT();
    }
    // Right
    if(is_dir_active(model, InputKeyRight)) {
        canvas_draw_triangle(canvas, cx + arrow_len, cy, ab, ah, CanvasDirectionLeftToRight);
    } else {
        DRAW_ARROW_OUTLINE_RIGHT();
    }

#undef DRAW_ARROW_OUTLINE_UP
#undef DRAW_ARROW_OUTLINE_DOWN
#undef DRAW_ARROW_OUTLINE_LEFT
#undef DRAW_ARROW_OUTLINE_RIGHT

    // Ok (filled disc when active, outline circle when idle)
    if(is_dir_active(model, InputKeyOk)) {
        canvas_draw_disc(canvas, cx, cy, (size_t)dot_r);
    } else {
        canvas_draw_circle(canvas, cx, cy, (size_t)dot_r);
    }

    // ── Bottom bar (raised so nothing is cut off) ──
    // icon_y=81: back icon + x2 label
    // label_y=92: "Back" + "Power" text
    // hint_y=105: "Hold for alt"
    const int icon_y  = 81;
    const int label_y = 92;
    const int hint_y  = 105;

    // Inset x-centres for Back (left) and Power (right) labels
    const int back_cx  = 14;
    const int power_cx = DISP_W - 15;

    // Back icon: small left-pointing filled triangle, centred over "Back"
    // canvas_draw_triangle tip x, tip y, base, height, dir
    canvas_draw_triangle(canvas, back_cx - 2, icon_y + 3, 6, 5, CanvasDirectionRightToLeft);
    // x2 above "Power", centred over it
    canvas_draw_str_aligned(canvas, power_cx, icon_y, AlignCenter, AlignTop, "x2");

    // Labels
    canvas_draw_str_aligned(canvas, back_cx, label_y, AlignCenter, AlignTop, "Back");
    canvas_draw_str_aligned(canvas, power_cx, label_y, AlignCenter, AlignTop, "Power");

    // Hint
    canvas_draw_str_aligned(canvas, DISP_W / 2, hint_y, AlignCenter, AlignTop, "Hold for alt");
}

/* ---- Helper: update model from app state ---- */

static void tv_remote_remote_update_model(
    TvRemoteApp* app, int8_t active, bool is_hold, uint8_t pressed_keys) {
    with_view_model(
        app->remote_view,
        TvRemoteRemoteModel * model,
        {
            model->active_button = active;
            model->active_is_hold = is_hold;
            model->pressed_keys = pressed_keys;
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
            tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
            return false; /* ViewDispatcher handles exit */
        }
        if(event->type == InputTypeShort) {
            uint32_t now = furi_get_tick();
            if((now - app->last_back_tick) < furi_ms_to_ticks(DOUBLE_TAP_MS)) {
                /* Double-tap → Power */
                tv_remote_ir_burst(app, TvButtonPower);
                tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
                app->last_back_tick = 0; /* reset so a third tap = Back again */
            } else {
                /* Single tap → Back IR */
                tv_remote_ir_burst(app, TvButtonBack);
                tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
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
        /* Visual feedback only – no IR yet (wait for release or long hold) */
        app->remote_pressed_keys |= key_to_bit(event->key);
        app->remote_held_long = false;
        tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
        return true;
    }

    if(event->type == InputTypeLong) {
        /* Start sending the hold/alt action */
        app->remote_held_long = true;
        tv_remote_tx_start(app, map->hold_btn);
        tv_remote_remote_update_model(app, (int8_t)map->hold_btn, true, app->remote_pressed_keys);
        return true;
    }

    if(event->type == InputTypeRelease) {
        app->remote_pressed_keys &= ~key_to_bit(event->key);
        if(app->remote_held_long) {
            /* Was holding alt action – just stop it */
            tv_remote_tx_stop(app);
        } else {
            /* Quick tap – send a brief burst of the primary action */
            tv_remote_ir_burst(app, map->press_btn);
        }
        app->remote_held_long = false;
        tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
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
    app->remote_pressed_keys = 0;
    app->remote_held_long = false;
    tv_remote_remote_update_model(app, -1, false, 0);
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
