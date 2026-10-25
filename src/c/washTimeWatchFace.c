#include <pebble.h>

// Auto-generated message keys (defined in message_keys.auto.c)
extern uint32_t MESSAGE_KEY_TimeFormat;
extern uint32_t MESSAGE_KEY_ShowDate;
extern uint32_t MESSAGE_KEY_DateFormat;
extern uint32_t MESSAGE_KEY_ShowSeconds;
extern uint32_t MESSAGE_KEY_SlotLeft;
extern uint32_t MESSAGE_KEY_SlotRight;
extern uint32_t MESSAGE_KEY_TempUnits;
extern uint32_t MESSAGE_KEY_BtVibrate;
extern uint32_t MESSAGE_KEY_SleepyMode;
extern uint32_t MESSAGE_KEY_Theme;
extern uint32_t MESSAGE_KEY_WallColor;
extern uint32_t MESSAGE_KEY_FloorColor;
extern uint32_t MESSAGE_KEY_WeatherTemp;
extern uint32_t MESSAGE_KEY_WeatherCode;
extern uint32_t MESSAGE_KEY_WeatherRequest;

// ================================================================
// CONSTANTS
// ================================================================
#define ANIM_MS       50    // 20 FPS for overflow animation only
#define BLINK_HOLD_MS 200   // how long eyes stay closed per blink
#define BLINK_MIN_MS  5000  // idle gap between blinks (each blink = 2 redraws)
#define BLINK_MAX_MS  9000
#define N_DROPS       10    // water drop particles
#define N_SUDS        3     // rotating suds arcs in door

// Theme colors (set by apply_theme(); B&W on aplite/diorite/flint)
#define CLR_WALL      s_theme.wall
#define CLR_FLOOR     s_theme.floor
#define CLR_GROUND    s_theme.ground
#define CLR_WATER     s_theme.water
#define CLR_RIPPLE    s_theme.ripple
#define CLR_INTERIOR  s_theme.interior
#define CLR_SLOT_TEXT s_theme.slot_text
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

// Persistent storage keys
#define SETTINGS_KEY_V1 1   // original {time_format, show_date} struct
#define SETTINGS_KEY    2   // current Settings struct (versioned)
#define WEATHER_KEY     3
#define SETTINGS_VERSION 2

// Info slot contents (SlotLeft / SlotRight)
enum { SLOT_NONE, SLOT_WEATHER, SLOT_STEPS, SLOT_HEART, SLOT_BATTERY, SLOT_HEART_LIVE };

#define HR_LIVE_PERIOD_S 5   // requested sensor interval for live heart rate

// Weather condition categories (mapped from WMO codes in pkjs)
enum { WX_CLEAR, WX_CLOUDY, WX_RAIN, WX_SNOW, WX_STORM, WX_FOG };

#define THEME_CUSTOM      5
#define WEATHER_STALE_S   (3 * 60 * 60)  // show "--" after 3h without data
#define WEATHER_EVERY_MIN 30

// ================================================================
// TYPES
// ================================================================
// Animation state machine:
// IDLE → OPEN (door opens) → OVER (water overflows) → POOL (time appears)
// → RECEDE (door closes, puddle shrinks) → SETTLE → IDLE
typedef enum {
    ST_IDLE,    // Resting: wash cycle spinning, eyes blinking
    ST_OPEN,    // Door arc opening, machine shaking
    ST_OVER,    // Water drops falling from door, puddle growing
    ST_POOL,    // Water pooled at max, time visible
    ST_RECEDE,  // Door closing, puddle shrinking, sock sliding back
    ST_SETTLE   // Brief pause before returning to idle
} AnimState;

// Water drop particle (falls from door during overflow)
typedef struct {
    int16_t x, y;    // position
    int16_t vx, vy;  // velocity (vy increases with gravity)
    uint8_t sz;      // radius
    bool on;         // active flag
} Drop;

// Settings persisted to storage. Bump SETTINGS_VERSION when changing.
typedef struct {
    uint8_t version;
    uint8_t time_format;   // 0=system, 1=12h, 2=24h
    bool    show_date;
    uint8_t date_format;   // 0="Sat Oct 25", 1="10/25", 2="25/10"
    bool    show_seconds;
    uint8_t slot_left;     // SLOT_*
    uint8_t slot_right;
    uint8_t temp_units;    // 0=F, 1=C (conversion happens in pkjs)
    bool    bt_vibrate;
    bool    sleepy;        // freeze animation in Quiet Time / sleep
    uint8_t theme;         // 0-4 presets, THEME_CUSTOM uses colors below
    uint8_t wall_argb;
    uint8_t floor_argb;
} Settings;

// Last weather received from the phone (persisted)
typedef struct {
    int16_t temp;
    uint8_t code;          // WX_*
    int32_t updated;       // time() of last update, 0 = never
} Weather;

// ================================================================
// GLOBALS
// ================================================================
static Window  *s_win;
static Layer   *s_cvs;
static AppTimer *s_idle_tmr, *s_anim_tmr;
static int16_t W, H;

// Settings
static Settings s_settings;

// Active theme colors
static struct {
    GColor wall, floor, ground, water, ripple, interior, slot_text;
} s_theme;

// Info slot data
static Weather s_weather;
static int32_t s_steps = -1;     // -1 = unavailable
static int32_t s_heart = 0;      // 0 = no reading
static uint8_t s_battery = 100;

// Sleepy mode currently in effect (animation frozen)
static bool s_sleeping = false;
// Tick unit currently subscribed
static TimeUnits s_tick_unit = 0;

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
static char s_sbuf[4];   // ":SS" when seconds are shown
static char s_date_buf[16];

// Space below the ground line not covered by Quick View
static int16_t s_avail_h;

// Whether to show time text in puddle
static bool s_show_time = true;

// RNG state
static uint32_t s_seed = 42;

// ================================================================
// LAYOUT (all positions computed from screen bounds)
// ================================================================
// All positions derived from screen bounds via ratios (never hardcoded)
static struct {
    GRect body;                          // Machine body rect
    GPoint door_c;                       // Door circle center
    int16_t door_r, door_ir;            // Door outer/inner radius
    GPoint eye_l, eye_r;                // Eye centers
    int16_t eye_rad, pup_rad;           // Eye/pupil radii
    int16_t ground_y;                   // Y of ground line
    GPoint sh_l, elb_l, hnd_l;         // Left arm: shoulder, elbow, hand
    GPoint sh_r, elb_r, hnd_r;         // Right arm
    GPoint leg_lt, leg_lb, leg_rt, leg_rb; // Leg tops and bottoms
    GRect foot_l, foot_r;              // Feet rects
    GPoint knob_l, knob_c, knob_r;     // Control panel knobs
    int16_t puddle_rest_hw, puddle_rest_h;  // Puddle resting size
    int16_t puddle_max_hw, puddle_max_h;    // Puddle max size (during overflow)
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
    s_settings = (Settings) {
        .version = SETTINGS_VERSION,
        .slot_left = SLOT_NONE,
        .slot_right = SLOT_NONE,
        .bt_vibrate = false,
        .sleepy = true,
        .wall_argb = GColorPastelYellowARGB8,
        .floor_argb = GColorWindsorTanARGB8,
    };
    if (persist_exists(SETTINGS_KEY)) {
        Settings saved;
        int n = persist_read_data(SETTINGS_KEY, &saved, sizeof(saved));
        if (n == (int)sizeof(saved) && saved.version == SETTINGS_VERSION) {
            s_settings = saved;
        }
    } else if (persist_exists(SETTINGS_KEY_V1)) {
        // Migrate 1.x settings
        struct { int time_format; bool show_date; } old;
        persist_read_data(SETTINGS_KEY_V1, &old, sizeof(old));
        s_settings.time_format = (uint8_t)old.time_format;
        s_settings.show_date = old.show_date;
    }
    if (persist_exists(WEATHER_KEY)) {
        persist_read_data(WEATHER_KEY, &s_weather, sizeof(s_weather));
    }
}

static void save_settings(void) {
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

// Dark text on light walls, white on dark ones (sum of 2-bit RGB channels)
static GColor text_for(GColor bg) {
    int lum = bg.r + bg.g + bg.b;
    return lum >= 5 ? GColorBlack : GColorWhite;
}

static void apply_theme(void) {
#if defined(PBL_COLOR)
    // wall, floor, ground, water
    static const uint8_t presets[][4] = {
        { GColorPastelYellowARGB8, GColorWindsorTanARGB8,
          GColorBulgarianRoseARGB8, GColorPictonBlueARGB8 },      // Laundry room
        { GColorOxfordBlueARGB8, GColorDarkGrayARGB8,
          GColorBlackARGB8, GColorVividCeruleanARGB8 },            // Midnight
        { GColorMintGreenARGB8, GColorArmyGreenARGB8,
          GColorDarkGreenARGB8, GColorPictonBlueARGB8 },           // Mint
        { GColorRichBrilliantLavenderARGB8, GColorImperialPurpleARGB8,
          GColorBlackARGB8, GColorVividCeruleanARGB8 },            // Lavender
        { GColorRajahARGB8, GColorBulgarianRoseARGB8,
          GColorBlackARGB8, GColorPictonBlueARGB8 },               // Sunset
    };
    int t = s_settings.theme;
    if (t == THEME_CUSTOM) {
        s_theme.wall   = (GColor){ .argb = s_settings.wall_argb };
        s_theme.floor  = (GColor){ .argb = s_settings.floor_argb };
        s_theme.ground = GColorBlack;
        s_theme.water  = GColorPictonBlue;
    } else {
        if (t >= (int)ARRAY_LENGTH(presets)) t = 0;
        s_theme.wall   = (GColor){ .argb = presets[t][0] };
        s_theme.floor  = (GColor){ .argb = presets[t][1] };
        s_theme.ground = (GColor){ .argb = presets[t][2] };
        s_theme.water  = (GColor){ .argb = presets[t][3] };
    }
    s_theme.ripple    = GColorCeleste;
    s_theme.interior  = GColorOxfordBlue;
    s_theme.slot_text = text_for(s_theme.wall);
#else
    s_theme.wall = GColorWhite;
    s_theme.floor = GColorBlack;
    s_theme.ground = GColorBlack;
    s_theme.water = GColorBlack;
    s_theme.ripple = GColorWhite;
    s_theme.interior = GColorBlack;
    s_theme.slot_text = GColorBlack;
#endif
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

// Set puddle size, clamped to the area Quick View leaves visible.
// Too little room hides the puddle (time moves onto the machine body).
static void set_puddle(int hw, int h) {
    if (s_avail_h < 10) { s_pw = 0; s_ph = 0; return; }
    s_pw = (int16_t)hw;
    s_ph = (int16_t)(h < s_avail_h ? h : s_avail_h);
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
    if (!s_show_time) return;

    if (s_pw <= 0 || s_ph <= 0) {
        // Quick View compact: digital display strip on the machine body,
        // between the door and the body's bottom edge (replaces knobs)
        int top = L.door_c.y + L.door_r + 3;
        int room = L.body.origin.y + L.body.size.h - 2 - top;
        GFont f;
        int fh;
        if (room >= 30) {
            f = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
            fh = 26;
        } else {
            f = fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
            fh = 20;
        }
        int strip_h = fh + 4;
        int sy = top + (room - strip_h) / 2;
        int bx = L.body.origin.x;
        int bw = L.body.size.w;

        graphics_context_set_fill_color(ctx, CLR_WATER);
        graphics_fill_rect(ctx, GRect(bx + 6, sy, bw - 12, strip_h), 3, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, s_tbuf, f,
            GRect(bx, sy - 1, bw, strip_h),
            GTextOverflowModeTrailingEllipsis,
            GTextAlignmentCenter, NULL);
        return;  // no date in compact mode
    }

    // Adaptive fonts: smaller on 144x168 / 180x180 screens
    GFont time_font, date_font;
    int time_h, date_h;
    if (H >= 200) {
        time_font = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
        time_h = 36;
        date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
        date_h = 18;
    } else {
        time_font = fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS);
        time_h = 28;
        date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
        date_h = 14;
    }

    // Center the time (+ optional date line) as one block in the puddle
    bool date = s_settings.show_date && s_date_buf[0];
    int block_h = time_h + (date ? date_h + 2 : 0);
    int y_offset = (s_ph - block_h) / 2;
    if (y_offset < 0) y_offset = 0;
    int time_y = L.ground_y + 2 + y_offset;

    // Time (+ optional smaller ":SS"), centered together as one run
    GRect tr = GRect(0, time_y, W, time_h + 4);
    GTextAlignment align = GTextAlignmentCenter;
    GFont sec_font = NULL;
    GRect sr = GRectZero;
    if (s_sbuf[0]) {
        sec_font = fonts_get_system_font(H >= 200 ? FONT_KEY_LECO_20_BOLD_NUMBERS
                                                  : FONT_KEY_GOTHIC_14_BOLD);
        int sec_h = H >= 200 ? 20 : 14;
        GSize tsz = graphics_text_layout_get_content_size(s_tbuf, time_font,
            GRect(0, 0, W, time_h + 4), GTextOverflowModeWordWrap, GTextAlignmentLeft);
        GSize ssz = graphics_text_layout_get_content_size(s_sbuf, sec_font,
            GRect(0, 0, W, sec_h + 4), GTextOverflowModeWordWrap, GTextAlignmentLeft);
        int x0 = (W - tsz.w - ssz.w - 2) / 2;
        tr = GRect(x0, time_y, tsz.w + 2, time_h + 4);
        sr = GRect(x0 + tsz.w + 2, time_y + time_h - sec_h - 2, ssz.w + 2, sec_h + 4);
        align = GTextAlignmentLeft;
    }

    // Text shadow
    graphics_context_set_text_color(ctx, CLR_INTERIOR);
    graphics_draw_text(ctx, s_tbuf, time_font, GRect(tr.origin.x + 1, tr.origin.y + 1,
        tr.size.w, tr.size.h), GTextOverflowModeFill, align, NULL);
    if (sec_font) {
        graphics_draw_text(ctx, s_sbuf, sec_font, GRect(sr.origin.x + 1, sr.origin.y + 1,
            sr.size.w, sr.size.h), GTextOverflowModeFill, align, NULL);
    }

    // Time text
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_tbuf, time_font, tr, GTextOverflowModeFill, align, NULL);
    if (sec_font) {
        graphics_draw_text(ctx, s_sbuf, sec_font, sr, GTextOverflowModeFill, align, NULL);
    }

    if (date) {
        graphics_draw_text(ctx, s_date_buf, date_font,
            GRect(0, time_y + time_h, W, date_h + 4),
            GTextOverflowModeTrailingEllipsis,
            GTextAlignmentCenter, NULL);
    }
}

// ---- Info slots (top corners of the wall, above the arms) ----

#define ICON_W 12

static void draw_icon_weather(GContext *ctx, int x, int y, int code) {
    // (x, y) = top-left of a 12x12 box
    GColor fg = CLR_SLOT_TEXT;
    graphics_context_set_fill_color(ctx, fg);
    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, 1);
    if (code == WX_CLEAR) {
        GPoint c = GPoint(x + 6, y + 6);
        graphics_fill_circle(ctx, c, 3);
        for (int i = 0; i < 8; i++) {
            int32_t a = i * TRIG_MAX_ANGLE / 8;
            int dx = sin_lookup(a) * 6 / TRIG_MAX_RATIO;
            int dy = cos_lookup(a) * 6 / TRIG_MAX_RATIO;
            graphics_draw_pixel(ctx, GPoint(c.x + dx, c.y - dy));
        }
        return;
    }
    if (code == WX_FOG) {
        for (int i = 0; i < 3; i++) {
            graphics_draw_line(ctx, GPoint(x + 1 + (i % 2), y + 3 + i * 3),
                                    GPoint(x + 10 - (i % 2), y + 3 + i * 3));
        }
        return;
    }
    // Cloud body shared by cloudy / rain / snow / storm
    graphics_fill_circle(ctx, GPoint(x + 4, y + 5), 3);
    graphics_fill_circle(ctx, GPoint(x + 7, y + 4), 3);
    graphics_fill_rect(ctx, GRect(x + 1, y + 5, 10, 3), 1, GCornersAll);
    if (code == WX_RAIN) {
        for (int i = 0; i < 3; i++) {
            graphics_draw_line(ctx, GPoint(x + 3 + i * 3, y + 9), GPoint(x + 2 + i * 3, y + 11));
        }
    } else if (code == WX_SNOW) {
        for (int i = 0; i < 3; i++) graphics_draw_pixel(ctx, GPoint(x + 3 + i * 3, y + 10 + (i % 2)));
    } else if (code == WX_STORM) {
        graphics_draw_line(ctx, GPoint(x + 7, y + 8), GPoint(x + 5, y + 10));
        graphics_draw_line(ctx, GPoint(x + 5, y + 10), GPoint(x + 7, y + 10));
        graphics_draw_line(ctx, GPoint(x + 7, y + 10), GPoint(x + 5, y + 12));
    }
}

static void draw_icon_steps(GContext *ctx, int x, int y) {
    // Two footprints
    graphics_context_set_fill_color(ctx, CLR_SLOT_TEXT);
    graphics_fill_rect(ctx, GRect(x + 1, y + 1, 4, 6), 2, GCornersAll);
    graphics_fill_rect(ctx, GRect(x + 1, y + 8, 4, 2), 1, GCornersAll);
    graphics_fill_rect(ctx, GRect(x + 7, y + 3, 4, 6), 2, GCornersAll);
    graphics_fill_rect(ctx, GRect(x + 7, y + 10, 4, 2), 1, GCornersAll);
}

static void draw_icon_heart(GContext *ctx, int x, int y) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, CLR_SLOT_TEXT));
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, CLR_SLOT_TEXT));
    graphics_fill_circle(ctx, GPoint(x + 3, y + 4), 3);
    graphics_fill_circle(ctx, GPoint(x + 8, y + 4), 3);
    // Taper to a point: rows shrinking toward the bottom tip
    for (int r = 0; r < 6; r++) {
        graphics_draw_line(ctx, GPoint(x + r, y + 5 + r), GPoint(x + 11 - r, y + 5 + r));
    }
}

static void draw_icon_battery(GContext *ctx, int x, int y) {
    GColor fg = CLR_SLOT_TEXT;
    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, GRect(x, y + 3, 10, 7));
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(x + 10, y + 5, 2, 3), 0, GCornerNone);
    int fill = 8 * s_battery / 100;
    if (fill < 1) fill = 1;
#if defined(PBL_COLOR)
    if (s_battery <= 20) graphics_context_set_fill_color(ctx, GColorRed);
#endif
    graphics_fill_rect(ctx, GRect(x + 1, y + 4, fill, 5), 0, GCornerNone);
}

// Format a slot's value; returns false if there is nothing to show
static bool slot_text(int slot, char *buf, size_t len) {
    switch (slot) {
        case SLOT_WEATHER:
            if (s_weather.updated == 0 ||
                time(NULL) - s_weather.updated > WEATHER_STALE_S) {
                snprintf(buf, len, "--°");
            } else {
                snprintf(buf, len, "%d°", s_weather.temp);
            }
            return true;
        case SLOT_STEPS:
            if (s_steps < 0) return false;
            if (s_steps >= 10000) {
                snprintf(buf, len, "%d.%dk", (int)(s_steps / 1000), (int)(s_steps % 1000 / 100));
            } else {
                snprintf(buf, len, "%d", (int)s_steps);
            }
            return true;
        case SLOT_HEART:
        case SLOT_HEART_LIVE:
#if defined(PBL_HEALTH)
            if (s_heart > 0) snprintf(buf, len, "%d", (int)s_heart);
            else snprintf(buf, len, "--");
            return true;
#else
            return false;
#endif
        case SLOT_BATTERY:
            snprintf(buf, len, "%d%%", s_battery);
            return true;
        default:
            return false;
    }
}

// Draw one slot as [icon][text], left- or right-aligned inside `box`
static void draw_slot(GContext *ctx, int slot, GRect box, bool right) {
    char buf[12];
    if (!slot_text(slot, buf, sizeof(buf))) return;

    GFont f = fonts_get_system_font(H >= 200 ? FONT_KEY_GOTHIC_18_BOLD
                                             : FONT_KEY_GOTHIC_14_BOLD);
    GSize tsz = graphics_text_layout_get_content_size(buf, f,
        GRect(0, 0, box.size.w, box.size.h), GTextOverflowModeFill, GTextAlignmentLeft);
    int total = ICON_W + 2 + tsz.w;
    if (total > box.size.w) total = box.size.w;
    int x = right ? box.origin.x + box.size.w - total : box.origin.x;
    int icon_y = box.origin.y + (box.size.h - 12) / 2 + 1;

    switch (slot) {
        case SLOT_WEATHER: draw_icon_weather(ctx, x, icon_y, s_weather.code); break;
        case SLOT_STEPS:   draw_icon_steps(ctx, x, icon_y); break;
        case SLOT_HEART:
        case SLOT_HEART_LIVE: draw_icon_heart(ctx, x, icon_y); break;
        case SLOT_BATTERY: draw_icon_battery(ctx, x, icon_y); break;
    }

    graphics_context_set_text_color(ctx, CLR_SLOT_TEXT);
    // Gothic glyphs sit low in their box; nudge up so text centers on the icon
    int fh = H >= 200 ? 18 : 14;
    graphics_draw_text(ctx, buf, f,
        GRect(x + ICON_W + 2, box.origin.y + (box.size.h - fh) / 2 - fh / 4,
              box.size.w - ICON_W - 2, fh + 4),
        GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static void draw_slots(GContext *ctx) {
#if defined(PBL_ROUND)
    // Round screens clip the top corners; sit lower and further in
    int y = H * 14 / 100;
    int inset = W * 12 / 100;
#else
    int y = 3;
    int inset = 3;
#endif
    // Each slot spans from the screen edge to just short of the nearest eye
    int w = L.eye_l.x - L.eye_rad - 3 - inset;
    int h = H >= 200 ? 20 : 16;
    if (w < 20) return;
    draw_slot(ctx, s_settings.slot_left, GRect(inset, y, w, h), false);
    draw_slot(ctx, s_settings.slot_right, GRect(W - inset - w, y, w, h), true);
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
static void draw_sock_with_stripe(GContext *ctx, int x, int y, int rot) {
    draw_sock_shape(ctx, x, y, rot, GColorWhite);
    graphics_context_set_fill_color(ctx, CLR_ACCENT);
    if (rot % 2 == 0) {
        // Red cuff (top of tube), red toe (tip of foot)
        graphics_fill_rect(ctx, GRect(x - 3, y - 6, 5, 3), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(x + 4, y + 3, 3, 4), 0, GCornerNone);
    } else {
        // Sideways: cuff at left end, toe at bottom of foot
        graphics_fill_rect(ctx, GRect(x - 6, y - 3, 3, 5), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(x + 3, y + 4, 4, 3), 0, GCornerNone);
    }
}

// Draw the falling sock
static void draw_falling_sock(GContext *ctx) {
    if (!s_sock_active) return;
    draw_sock_with_stripe(ctx, s_sock_x, s_sock_y, s_sock_rot / 2);
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

        // Partial water fill (drains as door opens).
        // Draws horizontal lines clipped to the circle using x²+y²=r²
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
        draw_sock_with_stripe(ctx, sx, sy, 0);

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
    graphics_fill_circle(ctx, GPoint(cx + or_ - 5, cy), 2);
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
    bool compact = (s_pw <= 0 || s_ph <= 0);
    draw_bg(ctx);
    draw_slots(ctx);
    draw_puddle(ctx);
    if (!compact) draw_time_text(ctx);
    draw_falling_sock(ctx);
    draw_legs(ctx);
    draw_body(ctx);
    draw_door(ctx);
    if (compact) draw_time_text(ctx);  // display strip replaces the knobs
    else draw_knobs(ctx);
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

// Schedule next blink in 5-9 seconds (battery-efficient: no continuous timer)
static void schedule_blink(void) {
    if (s_idle_tmr) { app_timer_cancel(s_idle_tmr); s_idle_tmr = NULL; }
    if (s_sleeping) return;  // frozen: no idle redraws at all
    s_idle_tmr = app_timer_register(rng(BLINK_MIN_MS, BLINK_MAX_MS), blink_start_cb, NULL);
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
            set_puddle(lerp(L.puddle_rest_hw, L.puddle_max_hw, s_fr * 100 / FR_OVER),
                       lerp(L.puddle_rest_h, L.puddle_max_h, s_fr * 100 / FR_OVER));
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
                // Land on the ground (stops tumbling)
                if (s_sock_y >= L.ground_y + 8) {
                    s_sock_y = L.ground_y + 8;
                    s_sock_vy = 0;
                    s_sock_vx = 0;
                    s_sock_land_x = s_sock_x;
                    s_sock_land_y = s_sock_y;
                } else {
                    s_sock_rot++;
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
            set_puddle(L.puddle_max_hw, L.puddle_max_h);
            if (s_fr >= FR_POOL) {
                s_st = ST_RECEDE;
                s_fr = 0;
            }
            break;

        case ST_RECEDE:
            s_door_pct = 100 - (100 * s_fr / FR_RECEDE);
            if (s_door_pct < 0) s_door_pct = 0;
            set_puddle(lerp(L.puddle_max_hw, L.puddle_rest_hw, s_fr * 100 / FR_RECEDE),
                       lerp(L.puddle_max_h, L.puddle_rest_h, s_fr * 100 / FR_RECEDE));
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
            set_puddle(L.puddle_rest_hw, L.puddle_rest_h);
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
    // 12-hour: "9:30", not "09:30"
    if (!use_24h && s_tbuf[0] == '0') {
        memmove(s_tbuf, s_tbuf + 1, strlen(s_tbuf));
    }

    if (s_settings.show_seconds && !s_sleeping) {
        strftime(s_sbuf, sizeof(s_sbuf), ":%S", tm);
    } else {
        s_sbuf[0] = '\0';
    }

    if (s_settings.show_date) {
        static const char *const fmts[] = { "%a %b %d", "%m/%d", "%d/%m" };
        int df = s_settings.date_format < ARRAY_LENGTH(fmts) ? s_settings.date_format : 0;
        strftime(s_date_buf, sizeof(s_date_buf), fmts[df], tm);
    } else {
        s_date_buf[0] = '\0';
    }
}

// ================================================================
// INFO SOURCES (health, battery, weather, sleep)
// ================================================================
static bool slot_used(int slot) {
    return s_settings.slot_left == slot || s_settings.slot_right == slot;
}

static void update_health(void) {
#if defined(PBL_HEALTH)
    time_t now = time(NULL);
    time_t start = time_start_of_today();
    if (health_service_metric_accessible(HealthMetricStepCount, start, now)
            & HealthServiceAccessibilityMaskAvailable) {
        s_steps = health_service_sum_today(HealthMetricStepCount);
    }
    if (health_service_metric_accessible(HealthMetricHeartRateBPM, now, now)
            & HealthServiceAccessibilityMaskAvailable) {
        s_heart = health_service_peek_current_value(HealthMetricHeartRateBPM);
    }
#endif
}

static bool heart_slot_used(void) {
    return slot_used(SLOT_HEART) || slot_used(SLOT_HEART_LIVE);
}

// Live mode asks the system to sample the sensor often (costs battery), and
// only while awake. Otherwise hand control back to the system's own schedule.
static void update_hr_sampling(void) {
#if defined(PBL_HEALTH)
    bool live = slot_used(SLOT_HEART_LIVE) && !s_sleeping;
    health_service_set_heart_rate_sample_period(live ? HR_LIVE_PERIOD_S : 0);
#endif
}

static bool is_asleep(void) {
#if defined(PBL_HEALTH)
    HealthActivityMask act = health_service_peek_current_activities();
    if (act & (HealthActivitySleep | HealthActivityRestfulSleep)) return true;
#endif
    return quiet_time_is_active();
}

static void request_weather(void) {
    if (!slot_used(SLOT_WEATHER)) return;
    DictionaryIterator *it;
    if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
    dict_write_uint8(it, MESSAGE_KEY_WeatherRequest, 1);
    app_message_outbox_send();
}

static void battery_handler(BatteryChargeState st) {
    s_battery = st.charge_percent;
    if (slot_used(SLOT_BATTERY)) layer_mark_dirty(s_cvs);
}

static void bt_handler(bool connected) {
    if (!connected && s_settings.bt_vibrate && !quiet_time_is_active()) {
        vibes_double_pulse();
    }
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
    if (event == HealthEventSleepUpdate) return;  // re-checked each minute
    update_health();
    if (slot_used(SLOT_STEPS) || heart_slot_used()) layer_mark_dirty(s_cvs);
}
#endif

static void tick_handler(struct tm *tick_time, TimeUnits changed);

// Seconds need SECOND_UNIT ticks; otherwise stay on MINUTE_UNIT for battery
static void update_tick_subscription(void) {
    TimeUnits want = (s_settings.show_seconds && !s_sleeping) ? SECOND_UNIT : MINUTE_UNIT;
    if (want != s_tick_unit) {
        s_tick_unit = want;
        tick_timer_service_subscribe(want, tick_handler);
    }
}

// Enter/leave sleepy mode (Quiet Time or sleep detected, if enabled)
static void update_sleeping(void) {
    bool sleeping = s_settings.sleepy && is_asleep();
    if (sleeping == s_sleeping) return;
    s_sleeping = sleeping;
    if (sleeping) {
        // Freeze in the resting pose: eyes open, door closed, time visible
        if (s_idle_tmr) { app_timer_cancel(s_idle_tmr); s_idle_tmr = NULL; }
        s_blink_fr = 0;
    } else if (s_st == ST_IDLE) {
        schedule_blink();
    }
    update_tick_subscription();
    update_hr_sampling();
    update_time();
    layer_mark_dirty(s_cvs);
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
    if (!(changed & MINUTE_UNIT)) {
        // Seconds-only tick: just repaint the time
        if (s_st == ST_IDLE) layer_mark_dirty(s_cvs);
        return;
    }

    update_sleeping();
    if (tick_time->tm_min % WEATHER_EVERY_MIN == 0) request_weather();
    if (s_sleeping) {
        layer_mark_dirty(s_cvs);
        return;
    }

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
// Clay sends select values as strings ("0", "1"); toggles/colors as ints
static int tuple_int(Tuple *t) {
    return (t->type == TUPLE_CSTRING) ? atoi(t->value->cstring) : (int)t->value->int32;
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    // Weather update from pkjs
    Tuple *wt = dict_find(iterator, MESSAGE_KEY_WeatherTemp);
    Tuple *wc = dict_find(iterator, MESSAGE_KEY_WeatherCode);
    if (wt && wc) {
        s_weather.temp = (int16_t)tuple_int(wt);
        s_weather.code = (uint8_t)tuple_int(wc);
        s_weather.updated = (int32_t)time(NULL);
        persist_write_data(WEATHER_KEY, &s_weather, sizeof(s_weather));
        layer_mark_dirty(s_cvs);
        return;
    }

    // Settings from Clay
    Tuple *t;
    bool any = false;
#define READ(key, field) \
    if ((t = dict_find(iterator, MESSAGE_KEY_##key))) { s_settings.field = tuple_int(t); any = true; }
    READ(TimeFormat, time_format)
    READ(ShowDate, show_date)
    READ(DateFormat, date_format)
    READ(ShowSeconds, show_seconds)
    READ(SlotLeft, slot_left)
    READ(SlotRight, slot_right)
    READ(TempUnits, temp_units)
    READ(BtVibrate, bt_vibrate)
    READ(SleepyMode, sleepy)
    READ(Theme, theme)
#undef READ
    // Clay color pickers send 0xRRGGBB; convert to the watch's 8-bit color
    if ((t = dict_find(iterator, MESSAGE_KEY_WallColor))) {
        s_settings.wall_argb = GColorFromHEX(t->value->int32).argb;
        any = true;
    }
    if ((t = dict_find(iterator, MESSAGE_KEY_FloorColor))) {
        s_settings.floor_argb = GColorFromHEX(t->value->int32).argb;
        any = true;
    }
    if (!any) return;

    save_settings();
    apply_theme();
    update_health();
    update_sleeping();
    update_tick_subscription();
    update_hr_sampling();
    update_time();
    layer_mark_dirty(s_cvs);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

// ================================================================
// QUICK VIEW (unobstructed area changes)
// ================================================================
// Machine stays full size; only puddle/time area adapts
static void unobstructed_change(AnimationProgress progress, void *context) {
    Layer *wl = window_get_root_layer(s_win);
    GRect ub = layer_get_unobstructed_bounds(wl);
    s_avail_h = ub.size.h - L.ground_y - 4;  // space below ground line
    // Mid-animation frames clamp themselves via set_puddle()
    if (s_st == ST_IDLE) set_puddle(L.puddle_rest_hw, L.puddle_rest_h);
    layer_mark_dirty(s_cvs);
}

// ================================================================
// WINDOW HANDLERS
// ================================================================
static void window_load(Window *window) {
    Layer *wl = window_get_root_layer(window);
    GRect full_bounds = layer_get_bounds(wl);

    compute_layout(full_bounds);  // machine layout always uses full screen

    s_cvs = layer_create(full_bounds);
    layer_set_update_proc(s_cvs, canvas_update);
    layer_add_child(wl, s_cvs);

    // Initialize state
    s_st = ST_IDLE;
    s_blink_fr = 0;
    s_eye_dx = 0; s_eye_dy = 0;
    s_door_pct = 0;
    s_sx = 0;
    s_sy = 0;
    s_show_time = true;
    s_sock_active = false;
    for (int i = 0; i < N_DROPS; i++) s_drops[i].on = false;

    // Subscribe to Quick View / timeline peek changes, then apply the
    // current state (sets the puddle size; Quick View may already be up)
    UnobstructedAreaHandlers ua_handlers = {
        .change = unobstructed_change,
    };
    unobstructed_area_service_subscribe(ua_handlers, NULL);
    unobstructed_change(0, NULL);

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
    apply_theme();
    s_battery = battery_state_service_peek().charge_percent;
    update_health();
    s_sleeping = s_settings.sleepy && is_asleep();
    update_hr_sampling();

    s_win = window_create();
    window_set_background_color(s_win, GColorBlack);
    window_set_window_handlers(s_win, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_win, true);

    update_tick_subscription();
    battery_state_service_subscribe(battery_handler);
    connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = bt_handler,
    });
#if defined(PBL_HEALTH)
    health_service_events_subscribe(health_handler, NULL);
#endif

    // Register AppMessage for Clay settings
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_open(256, 256);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    connection_service_unsubscribe();
#if defined(PBL_HEALTH)
    health_service_set_heart_rate_sample_period(0);  // release live sampling
    health_service_events_unsubscribe();
#endif
    if (s_win) { window_destroy(s_win); s_win = NULL; }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
