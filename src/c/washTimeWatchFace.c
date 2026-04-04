#include <pebble.h>

// Auto-generated message keys (defined in message_keys.auto.c)
extern uint32_t MESSAGE_KEY_TimeFormat;
extern uint32_t MESSAGE_KEY_ShowDate;

// ================================================================
// CONSTANTS
// ================================================================
#define ANIM_MS       50    // 20 FPS for overflow animation only
#define BLINK_HOLD_MS 200   // how long eyes stay closed per blink
#define N_DROPS       10    // water drop particles
#define N_SUDS        3     // rotating suds arcs in door

// Color palette (auto-falls back to B&W on aplite/diorite)
#define CLR_WALL      PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorWhite)
#define CLR_FLOOR     PBL_IF_COLOR_ELSE(GColorWindsorTan, GColorBlack)
#define CLR_GROUND    PBL_IF_COLOR_ELSE(GColorBulgarianRose, GColorBlack)
#define CLR_WATER     PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorBlack)
#define CLR_RIPPLE    PBL_IF_COLOR_ELSE(GColorCeleste, GColorWhite)
#define CLR_INTERIOR  PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorBlack)
#define CLR_SHADOW    PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray)
#define CLR_FRAME     GColorDarkGray
#define CLR_ACCENT    PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define CLR_SHIRT     PBL_IF_COLOR_ELSE(GColorMelon, GColorLightGray)
#define CLR_SOCK_BODY GColorWhite

// Animation frame counts (at ANIM_MS per frame)
#define FR_OPEN       8
#define FR_OVER       14
#define FR_POOL       10
#define FR_RECEDE     14
#define FR_SETTLE     4

// Persistent storage key
#define SETTINGS_KEY  1

// ================================================================
// TYPES
// ================================================================
typedef enum {
    ST_IDLE, ST_OPEN, ST_OVER, ST_POOL, ST_RECEDE, ST_SETTLE
} AnimState;

typedef struct {
    int16_t x, y, vx, vy;
    uint8_t sz;
    bool on;
} Drop;

// Settings persisted to storage
typedef struct {
    int  time_format;  // 0=system, 1=12h, 2=24h
    bool show_date;
} Settings;

// ================================================================
// GLOBALS
// ================================================================
static Window  *s_win;
static Layer   *s_cvs;
static AppTimer *s_idle_tmr, *s_anim_tmr;
static int16_t W, H;

// Settings
static Settings s_settings;

// Animation state
static AnimState s_st = ST_IDLE;
static int s_fr = 0;

// Eye blink (0 = open, 1 = closed)
static int s_blink_fr;

// Eye look direction (pupil offset)
static int8_t s_eye_dx, s_eye_dy;

// Wash cycle phase
static int32_t s_wash_ph = 0;

// Arm sway phase
static int32_t s_arm_ph = 0;

// Door open percentage (0=closed, 100=fully open)
static int s_door_pct = 0;

// Water drops
static Drop s_drops[N_DROPS];

// Puddle current half-width and height
static int16_t s_pw, s_ph;

// Machine shake offset
static int8_t s_sx, s_sy;

// Falling sock state (tumbles out during overflow, recedes back)
static int16_t s_sock_x, s_sock_y;
static int16_t s_sock_vx, s_sock_vy;
static int16_t s_sock_land_x, s_sock_land_y;
static int s_sock_rot;
static bool s_sock_active;

// Time and date buffers
static char s_tbuf[8];
static char s_date_buf[16];

// Whether to show time text in puddle
static bool s_show_time = true;

// RNG state
static uint32_t s_seed = 42;

// ================================================================
// LAYOUT (all positions computed from screen bounds)
// ================================================================
static struct {
    GRect body;
    GPoint door_c;
    int16_t door_r, door_ir;
    GPoint eye_l, eye_r;
    int16_t eye_rad, pup_rad;
    int16_t ground_y;
    GPoint sh_l, elb_l, hnd_l;
    GPoint sh_r, elb_r, hnd_r;
    GPoint leg_lt, leg_lb, leg_rt, leg_rb;
    GRect foot_l, foot_r;
    GPoint knob_l, knob_c, knob_r;
    int16_t puddle_rest_hw, puddle_rest_h;
    int16_t puddle_max_hw, puddle_max_h;
} L;

static void compute_layout(GRect b) {
    W = b.size.w;
    H = b.size.h;
    int cx = W / 2;

    // Machine body (centered, upper portion)
    int bw = W / 2;
    int bh = H * 46 / 100;
    int bx = cx - bw / 2;
    int by = H * 13 / 100;
    L.body = GRect(bx, by, bw, bh);

    // Door (circular window in body)
    L.door_c = GPoint(cx, by + bh * 40 / 100);
    L.door_r  = bw * 28 / 100;
    L.door_ir = bw * 24 / 100;

    // Eyes (above the body, sitting on top like cartoon eyes)
    int ey = by - H * 5 / 100;
    int es = W * 10 / 100;
    L.eye_l = GPoint(cx - es, ey);
    L.eye_r = GPoint(cx + es, ey);
    L.eye_rad = W * 5 / 100;
    if (L.eye_rad < 6) L.eye_rad = 6;
    L.pup_rad = L.eye_rad / 2;

    // Ground line
    L.ground_y = by + bh + H * 8 / 100;

    // Arms (stick arms extending from body sides)
    L.sh_l  = GPoint(bx, L.door_c.y);
    L.elb_l = GPoint(bx - W * 10 / 100, by + bh * 26 / 100);
    L.hnd_l = GPoint(bx - W * 14 / 100, by + bh * 33 / 100);
    L.sh_r  = GPoint(bx + bw, L.door_c.y);
    L.elb_r = GPoint(bx + bw + W * 10 / 100, by + bh * 26 / 100);
    L.hnd_r = GPoint(bx + bw + W * 14 / 100, by + bh * 33 / 100);

    // Legs
    L.leg_lt = GPoint(bx + bw * 25 / 100, by + bh);
    L.leg_lb = GPoint(bx + bw * 25 / 100, L.ground_y - 4);
    L.leg_rt = GPoint(bx + bw * 75 / 100, by + bh);
    L.leg_rb = GPoint(bx + bw * 75 / 100, L.ground_y - 4);

    // Feet
    int fw = W * 8 / 100;
    if (fw < 12) fw = 12;
    int fh = H * 3 / 100;
    if (fh < 4) fh = 4;
    L.foot_l = GRect(L.leg_lb.x - fw / 2, L.ground_y - fh, fw, fh);
    L.foot_r = GRect(L.leg_rb.x - fw / 2, L.ground_y - fh, fw, fh);

    // Control panel knobs (below door, on body)
    int ky = by + bh * 80 / 100;
    L.knob_l = GPoint(cx - bw * 25 / 100, ky);
    L.knob_c = GPoint(cx, ky);
    L.knob_r = GPoint(cx + bw * 25 / 100, ky);

    // Puddle sizes (larger to fit centered time + optional date)
    L.puddle_rest_hw = W * 40 / 100;
    L.puddle_rest_h  = H * 26 / 100;
    L.puddle_max_hw  = W * 46 / 100;
    L.puddle_max_h   = H * 30 / 100;
}

// ================================================================
// SETTINGS
// ================================================================
static void load_settings(void) {
    s_settings.time_format = 0;
    s_settings.show_date = false;
    persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

static void save_settings(void) {
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

// ================================================================
// UTILITIES
// ================================================================
static int rng(int lo, int hi) {
    s_seed = s_seed * 1103515245u + 12345u;
    return lo + (int)((s_seed >> 16) % (unsigned)(hi - lo + 1));
}

static int16_t isqrt(int32_t n) {
    if (n <= 0) return 0;
    int32_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return (int16_t)x;
}

static int lerp(int from, int to, int t) {
    return from + (to - from) * t / 100;
}

// ================================================================
// DRAWING FUNCTIONS
// ================================================================

static void draw_bg(GContext *ctx) {
    // Wall background
    graphics_context_set_fill_color(ctx, CLR_WALL);
    graphics_fill_rect(ctx, GRect(0, 0, W, L.ground_y), 0, GCornerNone);

    // Floor
    graphics_context_set_fill_color(ctx, CLR_FLOOR);
    graphics_fill_rect(ctx, GRect(0, L.ground_y, W, H - L.ground_y), 0, GCornerNone);

    // Ground line
    graphics_context_set_stroke_color(ctx, CLR_GROUND);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(0, L.ground_y), GPoint(W, L.ground_y));
}

static void draw_puddle(GContext *ctx) {
    if (s_pw <= 0 || s_ph <= 0) return;

    int cx = W / 2;
    int py = L.ground_y + 2;
    GRect pr = GRect(cx - s_pw, py, s_pw * 2, s_ph);

    // Main puddle fill
    graphics_context_set_fill_color(ctx, CLR_WATER);
    graphics_fill_rect(ctx, pr, 4, GCornersAll);

    // Ripple highlights
    graphics_context_set_stroke_color(ctx, CLR_RIPPLE);
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 3; i++) {
        int rx = cx - s_pw / 2 + i * s_pw / 2;
        int rw = s_pw / 4;
        graphics_draw_line(ctx, GPoint(rx, py + 3 + i * 4),
                                GPoint(rx + rw, py + 3 + i * 4));
    }
}

static void draw_time_text(GContext *ctx) {
    if (!s_show_time || s_pw <= 0 || s_ph <= 0) return;

    int py = L.ground_y + 2;

    // Adaptive font: smaller on basalt (144x168)
    GFont time_font;
    int time_h;
    if (H >= 200) {
        time_font = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
        time_h = 36;
    } else {
        time_font = fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS);
        time_h = 28;
    }

    // Center time vertically in puddle
    int y_offset = (s_ph - time_h) / 2;
    if (y_offset < 0) y_offset = 0;
    int time_y = py + y_offset;

    // Text shadow
    graphics_context_set_text_color(ctx, CLR_INTERIOR);
    graphics_draw_text(ctx, s_tbuf, time_font,
        GRect(1, time_y + 1, W, time_h + 4),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter, NULL);

    // Time text
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_tbuf, time_font,
        GRect(0, time_y, W, time_h + 4),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter, NULL);

    // Date text pinned near screen bottom
    if (s_settings.show_date && s_date_buf[0]) {
        int date_y = H - 22;
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, s_date_buf,
            fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(0, date_y, W, 20),
            GTextOverflowModeTrailingEllipsis,
            GTextAlignmentCenter, NULL);
    }
}

static void draw_legs(GContext *ctx) {
    graphics_context_set_stroke_color(ctx, CLR_FRAME);
    graphics_context_set_stroke_width(ctx, 3);

    // Legs connect body bottom to feet (apply shake to top only)
    graphics_draw_line(ctx,
        GPoint(L.leg_lt.x + s_sx, L.leg_lt.y + s_sy),
        GPoint(L.leg_lb.x, L.leg_lb.y));
    graphics_draw_line(ctx,
        GPoint(L.leg_rt.x + s_sx, L.leg_rt.y + s_sy),
        GPoint(L.leg_rb.x, L.leg_rb.y));

    // Feet
    graphics_context_set_fill_color(ctx, CLR_FRAME);
    graphics_fill_rect(ctx, L.foot_l, 2, GCornersAll);
    graphics_fill_rect(ctx, L.foot_r, 2, GCornersAll);
}

// Draw a sock shape at (x,y) center — ~10x14px
static void draw_sock_shape(GContext *ctx, int x, int y, int rot, GColor color) {
    graphics_context_set_fill_color(ctx, color);
    if (rot % 2 == 0) {
        // Vertical: tube going down, foot pointing right
        graphics_fill_rect(ctx, GRect(x - 3, y - 6, 5, 10), 1, GCornersAll);
        graphics_fill_rect(ctx, GRect(x - 2, y + 3, 9, 4), 1, GCornersAll);
    } else {
        // Horizontal: tube going right, foot pointing down
        graphics_fill_rect(ctx, GRect(x - 6, y - 3, 10, 5), 1, GCornersAll);
        graphics_fill_rect(ctx, GRect(x + 3, y - 2, 4, 9), 1, GCornersAll);
    }
}

// Draw a T-shirt shape at (x,y) center — ~14x14px
static void draw_shirt_shape(GContext *ctx, int x, int y, int rot, GColor color) {
    graphics_context_set_fill_color(ctx, color);
    if (rot % 2 == 0) {
        // Upright: wide shoulders/sleeves + narrower body
        graphics_fill_rect(ctx, GRect(x - 7, y - 5, 14, 4), 1, GCornersAll);
        graphics_fill_rect(ctx, GRect(x - 3, y - 2, 7, 9), 1, GCornersAll);
    } else {
        // Sideways: rotated 90
        graphics_fill_rect(ctx, GRect(x - 5, y - 7, 4, 14), 1, GCornersAll);
        graphics_fill_rect(ctx, GRect(x - 2, y - 3, 9, 7), 1, GCornersAll);
    }
}


// Draw a sock with red cuff and toe (used for both in-door and falling)
static void draw_sock_with_stripe(GContext *ctx, int x, int y) {
    draw_sock_shape(ctx, x, y, 0, GColorWhite);
    graphics_context_set_fill_color(ctx, CLR_ACCENT);
    // Red cuff (top of tube)
    graphics_fill_rect(ctx, GRect(x - 3, y - 6, 5, 3), 0, GCornerNone);
    // Red toe (tip of foot)
    graphics_fill_rect(ctx, GRect(x + 4, y + 3, 3, 4), 0, GCornerNone);
}

// Draw the falling sock
static void draw_falling_sock(GContext *ctx) {
    if (!s_sock_active) return;
    draw_sock_with_stripe(ctx, s_sock_x, s_sock_y);
}

static void draw_body(GContext *ctx) {
    GRect br = L.body;
    br.origin.x += s_sx;
    br.origin.y += s_sy;

    // Shadow
    graphics_context_set_fill_color(ctx, CLR_SHADOW);
    graphics_fill_rect(ctx, GRect(br.origin.x + 2, br.origin.y + 2,
                                   br.size.w, br.size.h), 6, GCornersAll);

    // Main body
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, br, 6, GCornersAll);

    // Outline
    graphics_context_set_stroke_color(ctx, CLR_SHADOW);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_round_rect(ctx, br, 6);

}

static void draw_door(GContext *ctx) {
    int cx = L.door_c.x + s_sx;
    int cy = L.door_c.y + s_sy;
    int ir = L.door_ir;
    int or_ = L.door_r;

    // Fill door interior
    if (s_door_pct > 0) {
        // Dark interior visible through open mouth
        graphics_context_set_fill_color(ctx, CLR_INTERIOR);
        graphics_fill_circle(ctx, GPoint(cx, cy), ir);

        // Partial water fill (draining from top)
        int water_h = ir * 2 * (100 - s_door_pct) / 100;
        if (water_h > 0) {
            graphics_context_set_fill_color(ctx, CLR_WATER);
            int top = cy - ir;
            int bot = top + water_h;
            if (bot > cy + ir) bot = cy + ir;
            for (int y = top; y < bot; y++) {
                int dy = y - cy;
                int32_t val = (int32_t)ir * ir - (int32_t)dy * dy;
                if (val <= 0) continue;
                int dx = isqrt(val);
                graphics_draw_line(ctx, GPoint(cx - dx, y), GPoint(cx + dx, y));
            }
        }
    } else {
        // Fully closed: solid blue water
        graphics_context_set_fill_color(ctx, CLR_WATER);
        graphics_fill_circle(ctx, GPoint(cx, cy), ir);
    }

    // Laundry tumbling inside door (visible when door mostly closed)
    if (s_door_pct < 30) {
        int lr = ir * 5 / 10;  // orbit radius

        // Sock (white with red accents) — orbits at wash phase
        int32_t sock_a = s_wash_ph;
        int sx = cx + (int16_t)((int32_t)sin_lookup(sock_a) * lr / TRIG_MAX_RATIO);
        int sy = cy - (int16_t)((int32_t)cos_lookup(sock_a) * lr / TRIG_MAX_RATIO);
        draw_sock_with_stripe(ctx, sx, sy);

        // Shirt (pink) — opposite side
        int32_t shirt_a = s_wash_ph + TRIG_MAX_ANGLE / 2;
        int hx = cx + (int16_t)((int32_t)sin_lookup(shirt_a) * lr / TRIG_MAX_RATIO);
        int hy = cy - (int16_t)((int32_t)cos_lookup(shirt_a) * lr / TRIG_MAX_RATIO);
        draw_shirt_shape(ctx, hx, hy, 0, CLR_SHIRT);
    }

    // Rotating suds / foam (only visible when door mostly closed)
    if (s_door_pct < 50) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, 2);
        int sud_r = ir * 7 / 10;
        for (int i = 0; i < N_SUDS; i++) {
            int32_t base = s_wash_ph + i * TRIG_MAX_ANGLE / N_SUDS;
            GPoint prev = GPoint(0, 0);
            for (int j = 0; j <= 4; j++) {
                int32_t a = base + j * TRIG_MAX_ANGLE / 20;
                GPoint p;
                p.x = cx + (int16_t)((int32_t)sin_lookup(a) * sud_r / TRIG_MAX_RATIO);
                p.y = cy - (int16_t)((int32_t)cos_lookup(a) * sud_r / TRIG_MAX_RATIO);
                if (j > 0) {
                    graphics_draw_line(ctx, prev, p);
                }
                prev = p;
            }
        }
    }

    // Door frame
    graphics_context_set_stroke_color(ctx, CLR_FRAME);
    graphics_context_set_stroke_width(ctx, 3);
    if (s_door_pct > 0) {
        // Partial circle with gap at bottom (mouth open)
        int gap = 45 * s_door_pct / 100;
        int32_t start = DEG_TO_TRIGANGLE(180 + gap);
        int32_t end   = DEG_TO_TRIGANGLE(180 - gap);
        GRect dr = GRect(cx - or_, cy - or_, or_ * 2, or_ * 2);
        graphics_draw_arc(ctx, dr, GOvalScaleModeFitCircle, start, end);
    } else {
        graphics_draw_circle(ctx, GPoint(cx, cy), or_);
    }

    // Door handle
    graphics_context_set_fill_color(ctx, CLR_FRAME);
    graphics_fill_circle(ctx, GPoint(cx + or_ - 5, cy + s_sy), 2);
}

static void draw_knobs(GContext *ctx) {
    graphics_context_set_fill_color(ctx, CLR_FRAME);
    graphics_fill_circle(ctx, GPoint(L.knob_l.x + s_sx, L.knob_l.y + s_sy), 4);
    graphics_fill_circle(ctx, GPoint(L.knob_r.x + s_sx, L.knob_r.y + s_sy), 4);

    // Power button (red)
    graphics_context_set_fill_color(ctx, CLR_ACCENT);
    graphics_fill_circle(ctx, GPoint(L.knob_c.x + s_sx, L.knob_c.y + s_sy), 3);
}

static void draw_arms(GContext *ctx) {
    int sway = (int)((int32_t)sin_lookup(s_arm_ph) * 5 / TRIG_MAX_RATIO);

    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 3);

    // Left arm
    GPoint sl = GPoint(L.sh_l.x + s_sx, L.sh_l.y + s_sy);
    GPoint el = GPoint(L.elb_l.x, L.elb_l.y + sway);
    GPoint hl = GPoint(L.hnd_l.x, L.hnd_l.y + sway);
    graphics_draw_line(ctx, sl, el);
    graphics_draw_line(ctx, el, hl);

    // Left hand (white glove)
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, hl, 4);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, hl, 4);

    // Right arm
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 3);
    GPoint sr = GPoint(L.sh_r.x + s_sx, L.sh_r.y + s_sy);
    GPoint er = GPoint(L.elb_r.x, L.elb_r.y - sway);
    GPoint hr = GPoint(L.hnd_r.x, L.hnd_r.y - sway);
    graphics_draw_line(ctx, sr, er);
    graphics_draw_line(ctx, er, hr);

    // Right hand (white glove)
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, hr, 4);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, hr, 4);
}

static void draw_one_eye(GContext *ctx, GPoint pos, bool closed) {
    if (!closed) {
        // White sclera
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, pos, L.eye_rad);
        // Outline
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_circle(ctx, pos, L.eye_rad);
        // Pupil (offset by look direction)
        int px = pos.x + s_eye_dx;
        int py = pos.y + s_eye_dy;
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_circle(ctx, GPoint(px, py), L.pup_rad);
        // Specular highlight (follows pupil)
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, GPoint(px - 2, py - 2), 2);
    } else {
        // Closed eye: horizontal line
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_line(ctx,
            GPoint(pos.x - L.eye_rad + 2, pos.y),
            GPoint(pos.x + L.eye_rad - 2, pos.y));
    }
}

static void draw_eyes(GContext *ctx) {
    bool closed = (s_blink_fr > 0);
    draw_one_eye(ctx, GPoint(L.eye_l.x + s_sx, L.eye_l.y + s_sy), closed);
    draw_one_eye(ctx, GPoint(L.eye_r.x + s_sx, L.eye_r.y + s_sy), closed);
}

static void draw_drops(GContext *ctx) {
    graphics_context_set_fill_color(ctx, CLR_WATER);
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drops[i].on) continue;
        graphics_fill_circle(ctx,
            GPoint(s_drops[i].x, s_drops[i].y), s_drops[i].sz);
    }
}

// ================================================================
// CANVAS UPDATE PROC
// ================================================================
static void canvas_update(Layer *layer, GContext *ctx) {
    draw_bg(ctx);
    draw_puddle(ctx);
    draw_time_text(ctx);
    draw_falling_sock(ctx);  // drawn here so body covers it when receding
    draw_legs(ctx);
    draw_body(ctx);
    draw_door(ctx);
    draw_knobs(ctx);
    draw_arms(ctx);
    draw_eyes(ctx);
    draw_drops(ctx);
}

// ================================================================
// UPDATE FUNCTIONS (battery-efficient: no continuous idle timer)
// ================================================================

// Spawn water drops from the door opening
static void spawn_drops(void) {
    int cx = L.door_c.x;
    int door_bot = L.door_c.y + L.door_ir;
    int half_w = L.door_ir / 2;
    for (int i = 0; i < N_DROPS; i++) {
        s_drops[i].x  = cx + rng(-half_w, half_w);
        s_drops[i].y  = door_bot + rng(0, 4);
        s_drops[i].vx = rng(-2, 2);
        s_drops[i].vy = rng(1, 4);
        s_drops[i].sz = rng(2, 4);
        s_drops[i].on = true;
    }
}

static void update_drops(void) {
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drops[i].on) continue;
        s_drops[i].x += s_drops[i].vx;
        s_drops[i].y += s_drops[i].vy;
        s_drops[i].vy += 2;  // gravity
        if (s_drops[i].y >= L.ground_y + 5) {
            s_drops[i].on = false;
        }
    }
}

// ================================================================
// TIMER CALLBACKS (forward declared)
// ================================================================
static void blink_start_cb(void *data);
static void anim_timer_cb(void *data);

// Schedule next blink in 2-4 seconds (battery-efficient: no continuous timer)
static void schedule_blink(void) {
    if (s_idle_tmr) { app_timer_cancel(s_idle_tmr); s_idle_tmr = NULL; }
    s_idle_tmr = app_timer_register(rng(2000, 4000), blink_start_cb, NULL);
}

static void start_idle(void) {
    if (s_anim_tmr) { app_timer_cancel(s_anim_tmr); s_anim_tmr = NULL; }
    schedule_blink();
}

static void start_anim(void) {
    if (s_idle_tmr) { app_timer_cancel(s_idle_tmr); s_idle_tmr = NULL; }
    s_anim_tmr = app_timer_register(ANIM_MS, anim_timer_cb, NULL);
}

// ================================================================
// STATE MACHINE
// ================================================================
static void advance_state(void) {
    s_fr++;

    switch (s_st) {
        case ST_OPEN:
            s_door_pct = 100 * s_fr / FR_OPEN;
            if (s_door_pct > 100) s_door_pct = 100;
            // Machine shakes while opening
            s_sx = (int8_t)rng(-2, 2);
            s_sy = (int8_t)rng(-1, 1);
            if (s_fr >= FR_OPEN) {
                s_st = ST_OVER;
                s_fr = 0;
                spawn_drops();
            }
            break;

        case ST_OVER:
            update_drops();
            s_sx = (int8_t)rng(-2, 2);
            s_sy = (int8_t)rng(-1, 1);
            // Grow puddle toward max
            s_pw = (int16_t)lerp(L.puddle_rest_hw, L.puddle_max_hw,
                                  s_fr * 100 / FR_OVER);
            s_ph = (int16_t)lerp(L.puddle_rest_h, L.puddle_max_h,
                                  s_fr * 100 / FR_OVER);
            // Respawn drops from door opening each few frames
            if (s_fr % 3 == 0) {
                int door_bot = L.door_c.y + L.door_ir;
                for (int i = 0; i < N_DROPS; i++) {
                    if (!s_drops[i].on) {
                        s_drops[i].x  = W / 2 + rng(-L.door_ir / 2, L.door_ir / 2);
                        s_drops[i].y  = door_bot;
                        s_drops[i].vx = rng(-2, 2);
                        s_drops[i].vy = rng(1, 3);
                        s_drops[i].sz = rng(2, 3);
                        s_drops[i].on = true;
                        break;
                    }
                }
            }
            // Sock falls out with gravity, tumbles
            if (s_sock_active) {
                s_sock_x += s_sock_vx;
                s_sock_y += s_sock_vy;
                s_sock_vy += 1;
                s_sock_rot++;
                // Land on the ground
                if (s_sock_y >= L.ground_y + 8) {
                    s_sock_y = L.ground_y + 8;
                    s_sock_vy = 0;
                    s_sock_vx = 0;
                    s_sock_land_x = s_sock_x;
                    s_sock_land_y = s_sock_y;
                }
            }
            if (s_fr >= FR_OVER) {
                s_st = ST_POOL;
                s_fr = 0;
                s_show_time = true;  // time appears in pooled water
            }
            break;

        case ST_POOL:
            update_drops();
            s_sx = 0; s_sy = 0;
            s_pw = L.puddle_max_hw;
            s_ph = L.puddle_max_h;
            if (s_fr >= FR_POOL) {
                s_st = ST_RECEDE;
                s_fr = 0;
            }
            break;

        case ST_RECEDE:
            s_door_pct = 100 - (100 * s_fr / FR_RECEDE);
            if (s_door_pct < 0) s_door_pct = 0;
            s_pw = (int16_t)lerp(L.puddle_max_hw, L.puddle_rest_hw,
                                  s_fr * 100 / FR_RECEDE);
            s_ph = (int16_t)lerp(L.puddle_max_h, L.puddle_rest_h,
                                  s_fr * 100 / FR_RECEDE);
            s_sx = 0; s_sy = 0;
            // Sock slides back under the machine
            if (s_sock_active) {
                int t = s_fr * 100 / FR_RECEDE;
                int target_x = W / 2;
                int target_y = L.body.origin.y + L.body.size.h - 5;
                s_sock_x = (int16_t)lerp(s_sock_land_x, target_x, t);
                s_sock_y = (int16_t)lerp(s_sock_land_y, target_y, t);
            }
            if (s_fr >= FR_RECEDE) {
                s_st = ST_SETTLE;
                s_fr = 0;
                s_door_pct = 0;
                s_sock_active = false;
            }
            break;

        case ST_SETTLE:
            s_pw = L.puddle_rest_hw;
            s_ph = L.puddle_rest_h;
            if (s_fr >= FR_SETTLE) {
                s_st = ST_IDLE;
                s_fr = 0;
                start_idle();
                return;  // don't reschedule anim timer
            }
            break;

        case ST_IDLE:
            break;
    }
}

// ================================================================
// TIMER CALLBACKS
// ================================================================

// Blink end: reopen eyes, schedule next blink
static void blink_end_cb(void *data) {
    s_idle_tmr = NULL;
    s_blink_fr = 0;  // eyes open
    layer_mark_dirty(s_cvs);
    if (s_st == ST_IDLE) schedule_blink();
}

// Blink start: close eyes, nudge wash cycle, change look direction
static void blink_start_cb(void *data) {
    s_idle_tmr = NULL;
    s_blink_fr = 1;  // eyes closed

    // Nudge wash cycle and arm phase (slow idle movement)
    s_wash_ph = (s_wash_ph + TRIG_MAX_ANGLE / 20) % TRIG_MAX_ANGLE;
    s_arm_ph = (s_arm_ph + TRIG_MAX_ANGLE / 16) % TRIG_MAX_ANGLE;

    // Change look direction on each blink
    int max_off = L.eye_rad / 3;
    if (max_off < 2) max_off = 2;
    s_eye_dx = (int8_t)rng(-max_off, max_off);
    s_eye_dy = (int8_t)rng(-max_off, max_off);

    layer_mark_dirty(s_cvs);

    // Schedule eyes to reopen
    s_idle_tmr = app_timer_register(BLINK_HOLD_MS, blink_end_cb, NULL);
}

static void anim_timer_cb(void *data) {
    s_anim_tmr = NULL;
    advance_state();
    // Keep wash cycle spinning during animation
    s_wash_ph = (s_wash_ph + TRIG_MAX_ANGLE / 15) % TRIG_MAX_ANGLE;
    layer_mark_dirty(s_cvs);
    if (s_st != ST_IDLE) {
        s_anim_tmr = app_timer_register(ANIM_MS, anim_timer_cb, NULL);
    }
}

// ================================================================
// TIME HANDLING
// ================================================================
static void update_time(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) return;

    bool use_24h;
    switch (s_settings.time_format) {
        case 1:  use_24h = false; break;
        case 2:  use_24h = true;  break;
        default: use_24h = clock_is_24h_style(); break;
    }

    strftime(s_tbuf, sizeof(s_tbuf),
             use_24h ? "%H:%M" : "%I:%M", tm);

    if (s_settings.show_date) {
        strftime(s_date_buf, sizeof(s_date_buf), "%a %b %d", tm);
    } else {
        s_date_buf[0] = '\0';
    }
}

static void start_minute_anim(void) {
    s_st = ST_OPEN;
    s_fr = 0;
    s_door_pct = 0;
    s_show_time = false;  // hide time during overflow
    for (int i = 0; i < N_DROPS; i++) s_drops[i].on = false;

    // Spawn sock to fall out with the water
    s_sock_x = W / 2 + rng(-5, 5);
    s_sock_y = L.door_c.y + L.door_ir;
    s_sock_vx = rng(-1, 2);
    s_sock_vy = rng(1, 3);
    s_sock_rot = 0;
    s_sock_active = true;

    start_anim();
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
    update_time();
    // Advance wash cycle and arm position per minute
    s_wash_ph = (s_wash_ph + TRIG_MAX_ANGLE / 8) % TRIG_MAX_ANGLE;
    s_arm_ph = (s_arm_ph + TRIG_MAX_ANGLE / 8) % TRIG_MAX_ANGLE;
    // Only trigger animation if idle (prevent double-firing)
    if (s_st == ST_IDLE) {
        start_minute_anim();
    }
}

// ================================================================
// APPMESSAGE (Clay settings)
// ================================================================
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    Tuple *tf_tuple = dict_find(iterator, MESSAGE_KEY_TimeFormat);
    Tuple *sd_tuple = dict_find(iterator, MESSAGE_KEY_ShowDate);

    if (tf_tuple) {
        s_settings.time_format = (int)tf_tuple->value->int32;
    }
    if (sd_tuple) {
        s_settings.show_date = (sd_tuple->value->int32 == 1);
    }

    save_settings();
    update_time();
    layer_mark_dirty(s_cvs);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

// ================================================================
// WINDOW HANDLERS
// ================================================================
static void window_load(Window *window) {
    Layer *wl = window_get_root_layer(window);
    GRect bounds = layer_get_unobstructed_bounds(wl);

    compute_layout(bounds);

    s_cvs = layer_create(layer_get_bounds(wl));  // full screen canvas
    layer_set_update_proc(s_cvs, canvas_update);
    layer_add_child(wl, s_cvs);

    // Initialize state
    s_st = ST_IDLE;
    s_blink_fr = 0;
    s_eye_dx = 0; s_eye_dy = 0;
    s_door_pct = 0;
    s_pw = L.puddle_rest_hw;
    s_ph = L.puddle_rest_h;
    s_sx = 0;
    s_sy = 0;
    s_show_time = true;
    s_sock_active = false;
    for (int i = 0; i < N_DROPS; i++) s_drops[i].on = false;

    update_time();
    start_idle();
}

static void window_unload(Window *window) {
    if (s_idle_tmr) { app_timer_cancel(s_idle_tmr); s_idle_tmr = NULL; }
    if (s_anim_tmr) { app_timer_cancel(s_anim_tmr); s_anim_tmr = NULL; }
    if (s_cvs) { layer_destroy(s_cvs); s_cvs = NULL; }
}

// ================================================================
// LIFECYCLE
// ================================================================
static void init(void) {
    s_seed = (uint32_t)time(NULL);

    // Load saved settings before creating UI
    load_settings();

    s_win = window_create();
    window_set_background_color(s_win, GColorBlack);
    window_set_window_handlers(s_win, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_win, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    // Register AppMessage for Clay settings
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_open(256, 256);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    if (s_win) { window_destroy(s_win); s_win = NULL; }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
