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

/* Concentric-circle radii (Home < Ok < D-pad < Hold) */
#define R_HOME  7
#define R_OK   14
#define R_DPAD 22
#define R_HOLD 30

/* Diagonal intercepts at 45° for divider lines: round(r * 0.707). */
#define D_OK   10
#define D_HOLD 21

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
    uint8_t pressed_keys;  /**< Bitmask of physically held d-pad/ok/back keys. */
} TvRemoteRemoteModel;

/* Bitmask bits for pressed_keys */
#define KEY_BIT_UP    (1u << 0)
#define KEY_BIT_DOWN  (1u << 1)
#define KEY_BIT_LEFT  (1u << 2)
#define KEY_BIT_RIGHT (1u << 3)
#define KEY_BIT_OK    (1u << 4)
#define KEY_BIT_BACK  (1u << 5)

static inline uint8_t key_to_bit(InputKey k) {
    switch(k) {
    case InputKeyUp:    return KEY_BIT_UP;
    case InputKeyDown:  return KEY_BIT_DOWN;
    case InputKeyLeft:  return KEY_BIT_LEFT;
    case InputKeyRight: return KEY_BIT_RIGHT;
    case InputKeyOk:    return KEY_BIT_OK;
    case InputKeyBack:  return KEY_BIT_BACK;
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

/* ---- Concentric-circle drawing helpers ---- */

/** Quadrant directions for ring fills. */
#define QUAD_TOP    0
#define QUAD_BOTTOM 1
#define QUAD_LEFT   2
#define QUAD_RIGHT  3

/**
 * Fill a wedge-shaped quadrant of a ring (annulus sector).
 * Pixels exactly on the 45° diagonals are skipped, creating natural gap lines.
 */
static void draw_ring_quadrant_filled(
    Canvas* canvas,
    int cx,
    int cy,
    int r_in,
    int r_out,
    uint8_t direction) {
    int ri2 = r_in * r_in;
    int ro2 = r_out * r_out;
    for(int dy = -r_out; dy <= r_out; dy++) {
        for(int dx = -r_out; dx <= r_out; dx++) {
            int d2 = dx * dx + dy * dy;
            if(d2 < ri2 || d2 > ro2) continue;
            bool in_q = false;
            switch(direction) {
            case QUAD_TOP:
                in_q = (dy < 0 && dy * dy > dx * dx);
                break;
            case QUAD_BOTTOM:
                in_q = (dy > 0 && dy * dy > dx * dx);
                break;
            case QUAD_LEFT:
                in_q = (dx < 0 && dx * dx > dy * dy);
                break;
            case QUAD_RIGHT:
                in_q = (dx > 0 && dx * dx > dy * dy);
                break;
            }
            if(in_q) canvas_draw_dot(canvas, cx + dx, cy + dy);
        }
    }
}

/** Fill a full ring (annulus). */
static void draw_ring_filled(Canvas* canvas, int cx, int cy, int r_in, int r_out) {
    int ri2 = r_in * r_in;
    int ro2 = r_out * r_out;
    for(int dy = -r_out; dy <= r_out; dy++) {
        for(int dx = -r_out; dx <= r_out; dx++) {
            int d2 = dx * dx + dy * dy;
            if(d2 >= ri2 && d2 <= ro2) {
                canvas_draw_dot(canvas, cx + dx, cy + dy);
            }
        }
    }
}

/* ---- Draw callback ---- */

static void tv_remote_remote_draw_callback(Canvas* canvas, void* model_void) {
    TvRemoteRemoteModel* model = model_void;
    canvas_clear(canvas);

    const int cx = DISP_W / 2;      /* 32 */
    const int cy = 45;              /* midpoint between top and Back/Power boxes */

    int8_t ab = model->active_button;
    uint8_t pk = model->pressed_keys;
    bool is_hold = model->active_is_hold;

    /* ── Determine which sections are active ── */

    /* Center: Home (hold-Ok) */
    bool home_active = (ab == (int8_t)TvButtonHome);

    /* Ring 2: Ok (press-Ok, but not when hold is active) */
    bool ok_active = (ab == (int8_t)TvButtonOk) ||
                     ((pk & KEY_BIT_OK) && !is_hold);

    /* Ring 3 quadrants: d-pad press actions */
    bool r3_top = (ab == (int8_t)TvButtonUp) ||
                  ((pk & KEY_BIT_UP) && !is_hold);
    bool r3_bottom = (ab == (int8_t)TvButtonDown) ||
                     ((pk & KEY_BIT_DOWN) && !is_hold);
    bool r3_left = (ab == (int8_t)TvButtonLeft) ||
                   ((pk & KEY_BIT_LEFT) && !is_hold);
    bool r3_right = (ab == (int8_t)TvButtonRight) ||
                    ((pk & KEY_BIT_RIGHT) && !is_hold);

    /* Ring 4 quadrants: hold actions */
    bool r4_top    = (ab == (int8_t)TvButtonVolUp);
    bool r4_bottom = (ab == (int8_t)TvButtonVolDn);
    bool r4_left   = (ab == (int8_t)TvButtonChDn);
    bool r4_right  = (ab == (int8_t)TvButtonChUp);

    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, ColorBlack);

    /* ── 1. Fill active sections ── */

    /* Ring 4 (outermost – hold actions) */
    if(r4_top)    draw_ring_quadrant_filled(canvas, cx, cy, R_DPAD, R_HOLD, QUAD_TOP);
    if(r4_bottom) draw_ring_quadrant_filled(canvas, cx, cy, R_DPAD, R_HOLD, QUAD_BOTTOM);
    if(r4_left)   draw_ring_quadrant_filled(canvas, cx, cy, R_DPAD, R_HOLD, QUAD_LEFT);
    if(r4_right)  draw_ring_quadrant_filled(canvas, cx, cy, R_DPAD, R_HOLD, QUAD_RIGHT);

    /* Ring 3 (d-pad press actions) */
    if(r3_top)    draw_ring_quadrant_filled(canvas, cx, cy, R_OK, R_DPAD, QUAD_TOP);
    if(r3_bottom) draw_ring_quadrant_filled(canvas, cx, cy, R_OK, R_DPAD, QUAD_BOTTOM);
    if(r3_left)   draw_ring_quadrant_filled(canvas, cx, cy, R_OK, R_DPAD, QUAD_LEFT);
    if(r3_right)  draw_ring_quadrant_filled(canvas, cx, cy, R_OK, R_DPAD, QUAD_RIGHT);

    /* Ring 2: Ok */
    if(ok_active) draw_ring_filled(canvas, cx, cy, R_HOME, R_OK);

    /* Ring 1: Home */
    if(home_active) canvas_draw_disc(canvas, cx, cy, R_HOME);

    /* ── 2. Circle outlines ── */
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_circle(canvas, cx, cy, R_HOME);
    canvas_draw_circle(canvas, cx, cy, R_OK);
    canvas_draw_circle(canvas, cx, cy, R_DPAD);
    canvas_draw_circle(canvas, cx, cy, R_HOLD);

    /* ── 3. Diagonal dividers (R_OK → R_HOLD at 45°) ── */
    canvas_draw_line(canvas, cx + D_OK, cy - D_OK, cx + D_HOLD, cy - D_HOLD); /* NE */
    canvas_draw_line(canvas, cx - D_OK, cy - D_OK, cx - D_HOLD, cy - D_HOLD); /* NW */
    canvas_draw_line(canvas, cx + D_OK, cy + D_OK, cx + D_HOLD, cy + D_HOLD); /* SE */
    canvas_draw_line(canvas, cx - D_OK, cy + D_OK, cx - D_HOLD, cy + D_HOLD); /* SW */

    /* ── Bottom bar ── */
    const int box_y = 90;
    const int box_h = 24;
    const int box_w = 28;
    const int box_gap = 2;

    /* Back box */
    bool back_active = (ab == (int8_t)TvButtonBack);
    if(back_active) {
        canvas_draw_rbox(canvas, box_gap, box_y, box_w, box_h, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, box_gap, box_y, box_w, box_h, 3);
    }
    canvas_draw_triangle(
        canvas, box_gap + box_w / 2 - 2, box_y + 6, 6, 5, CanvasDirectionRightToLeft);
    canvas_draw_str_aligned(
        canvas, box_gap + box_w / 2, box_y + box_h - 4, AlignCenter, AlignBottom, "Back");
    if(back_active) canvas_set_color(canvas, ColorBlack);

    /* Power box */
    bool power_active = (ab == (int8_t)TvButtonPower);
    const int power_box_x = DISP_W - box_w - box_gap;
    if(power_active) {
        canvas_draw_rbox(canvas, power_box_x, box_y, box_w, box_h, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, power_box_x, box_y, box_w, box_h, 3);
    }
    canvas_draw_str_aligned(
        canvas, power_box_x + box_w / 2, box_y + 5, AlignCenter, AlignTop, "x2");
    canvas_draw_str_aligned(
        canvas, power_box_x + box_w / 2, box_y + box_h - 4, AlignCenter, AlignBottom, "Pwr");
    if(power_active) canvas_set_color(canvas, ColorBlack);

    /* Hint */
    canvas_draw_str_aligned(
        canvas, DISP_W / 2, 118, AlignCenter, AlignTop, "Hold for alt");
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

/* ---- Custom event callback (handles deferred back-tap timer) ---- */

static bool tv_remote_remote_custom_event_callback(void* context, uint32_t event) {
    TvRemoteApp* app = context;
    if(event == TvRemoteCustomEventBackTimeout) {
        if(app->back_pending) {
            app->back_pending = false;
            /* Timer expired – single tap → Back IR */
            tv_remote_remote_update_model(
                app, (int8_t)TvButtonBack, false, app->remote_pressed_keys);
            tv_remote_ir_burst(app, TvButtonBack);
            tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
        }
        return true;
    }
    return false;
}

/* ---- Input callback ---- */

static bool tv_remote_remote_input_callback(InputEvent* event, void* context) {
    TvRemoteApp* app = context;

    /* ── Back button: hold to exit, short = Back IR, double-tap = Power ── */
    if(event->key == InputKeyBack) {
        if(event->type == InputTypePress) {
            /* Consume silently – no visual until action is decided */
            return true;
        }
        if(event->type == InputTypeLong) {
            /* Hold back = exit: cancel any pending tap, let ViewDispatcher navigate */
            if(app->back_pending) {
                app->back_pending = false;
                furi_timer_stop(app->back_timer);
            }
            tv_remote_tx_stop(app);
            tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
            return false; /* ViewDispatcher handles exit */
        }
        if(event->type == InputTypeShort) {
            if(app->back_pending) {
                /* Double-tap within window → Power */
                app->back_pending = false;
                furi_timer_stop(app->back_timer);
                tv_remote_remote_update_model(
                    app, (int8_t)TvButtonPower, false, app->remote_pressed_keys);
                tv_remote_ir_burst(app, TvButtonPower);
                tv_remote_remote_update_model(app, -1, false, app->remote_pressed_keys);
            } else {
                /* First tap – wait for possible double-tap */
                app->back_pending = true;
                furi_timer_start(app->back_timer, furi_ms_to_ticks(DOUBLE_TAP_MS));
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
        /* Consume silently – no visual until action is decided */
        app->remote_held_long = false;
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
            /* Quick tap – show active ring section during burst */
            tv_remote_remote_update_model(
                app, (int8_t)map->press_btn, false, app->remote_pressed_keys);
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
    app->remote_pressed_keys = 0;
    app->remote_held_long = false;
    app->back_pending = false;
    furi_timer_stop(app->back_timer);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, tv_remote_remote_custom_event_callback);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    tv_remote_remote_update_model(app, -1, false, 0);
}

static void tv_remote_remote_exit_callback(void* context) {
    TvRemoteApp* app = context;
    furi_timer_stop(app->back_timer);
    app->back_pending = false;
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
