#include "logic.h"

#include <stdio.h>
#include <string.h>

int16_t wl_isqrt(int32_t n) {
    if (n <= 0) return 0;
    int32_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return (int16_t)x;
}

int wl_lerp(int from, int to, int t_percent) {
    return from + (to - from) * t_percent / 100;
}

void wl_clamp_puddle(int avail_h, int hw, int h, int16_t *out_pw, int16_t *out_ph) {
    if (avail_h < 10) { *out_pw = 0; *out_ph = 0; return; }
    *out_pw = (int16_t)hw;
    *out_ph = (int16_t)(h < avail_h ? h : avail_h);
}

void wl_settings_defaults(Settings *s, uint8_t wall_argb, uint8_t floor_argb) {
    memset(s, 0, sizeof(*s));
    s->version = SETTINGS_VERSION;
    s->slot_left = SLOT_NONE;
    s->slot_right = SLOT_NONE;
    s->sleepy = true;
    s->wall_argb = wall_argb;
    s->floor_argb = floor_argb;
}

void wl_settings_from_v1(Settings *s, int time_format, bool show_date) {
    s->time_format = (uint8_t)time_format;
    s->show_date = show_date;
    wl_settings_sanitize(s);
}

void wl_settings_sanitize(Settings *s) {
    if (s->time_format > 2) s->time_format = 0;
    if (s->date_format > 2) s->date_format = 0;
    if (s->slot_left > SLOT_BATTERY) s->slot_left = SLOT_NONE;
    if (s->slot_right > SLOT_BATTERY) s->slot_right = SLOT_NONE;
    if (s->temp_units > 1) s->temp_units = 0;
    if (s->theme > THEME_CUSTOM) s->theme = 0;
}

void wl_trim_leading_zero(char *buf) {
    if (buf[0] == '0' && buf[1] != '\0') memmove(buf, buf + 1, strlen(buf));
}

void wl_weather_text(const Weather *w, int32_t now, char *buf, size_t len) {
    if (w->updated == 0 || now - w->updated > WEATHER_STALE_S) {
        snprintf(buf, len, "--\xC2\xB0");  // degree sign (UTF-8)
    } else {
        snprintf(buf, len, "%d\xC2\xB0", (int)w->temp);
    }
}

bool wl_steps_text(int32_t steps, char *buf, size_t len) {
    if (steps < 0) return false;
    if (steps >= 10000) {
        snprintf(buf, len, "%d.%dk", (int)(steps / 1000), (int)(steps % 1000 / 100));
    } else {
        snprintf(buf, len, "%d", (int)steps);
    }
    return true;
}

void wl_heart_text(int32_t bpm, char *buf, size_t len) {
    if (bpm > 0) snprintf(buf, len, "%d", (int)bpm);
    else snprintf(buf, len, "--");
}

bool wl_dark_text_on(int r, int g, int b) {
    return r + g + b >= 5;
}
