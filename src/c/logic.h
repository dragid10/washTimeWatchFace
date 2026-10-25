// Pure logic with no Pebble SDK dependencies, so it can be unit tested on a
// desktop with plain gcc (see test/c and scripts/test-c.sh).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Info slot contents (SlotLeft / SlotRight)
enum { SLOT_NONE, SLOT_WEATHER, SLOT_STEPS, SLOT_HEART, SLOT_BATTERY };

// Weather condition categories (mapped from WMO codes in pkjs)
enum { WX_CLEAR, WX_CLOUDY, WX_RAIN, WX_SNOW, WX_STORM, WX_FOG };

#define SETTINGS_VERSION 2
#define THEME_CUSTOM     5
#define WEATHER_STALE_S  (3 * 60 * 60)  // show "--" after 3h without data

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

// Math
int16_t wl_isqrt(int32_t n);
int wl_lerp(int from, int to, int t_percent);

// Quick View: clamp a puddle to the visible height. Too little room
// (avail_h < 10) hides the puddle entirely (both outputs 0).
void wl_clamp_puddle(int avail_h, int hw, int h, int16_t *out_pw, int16_t *out_ph);

// Settings
void wl_settings_defaults(Settings *s, uint8_t wall_argb, uint8_t floor_argb);
// Apply 1.x settings ({int time_format; bool show_date}) onto defaults
void wl_settings_from_v1(Settings *s, int time_format, bool show_date);
// Clamp out-of-range values read from storage or Clay
void wl_settings_sanitize(Settings *s);

// Text formatting. All write a NUL-terminated string into buf.
void wl_trim_leading_zero(char *buf);                       // "09:30" -> "9:30"
// Weather text; "--°" if never updated or older than WEATHER_STALE_S
void wl_weather_text(const Weather *w, int32_t now, char *buf, size_t len);
// Steps; 9999 -> "9999", 12345 -> "12.3k". Returns false if unavailable (<0)
bool wl_steps_text(int32_t steps, char *buf, size_t len);
void wl_heart_text(int32_t bpm, char *buf, size_t len);    // "--" when no reading

// Theme: true when text on this background (2-bit r,g,b 0..3) should be dark
bool wl_dark_text_on(int r, int g, int b);
