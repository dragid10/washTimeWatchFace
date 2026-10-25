// Desktop unit tests for src/c/logic.c. Build and run: scripts/test-c.sh
#include <stdio.h>
#include <string.h>

#include "../../src/c/logic.h"

static int s_checks = 0, s_failed = 0;

#define CHECK(cond) do { \
    s_checks++; \
    if (!(cond)) { s_failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    s_checks++; \
    if (strcmp((actual), (expected)) != 0) { \
        s_failed++; \
        printf("FAIL %s:%d  got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (actual), (expected)); \
    } \
} while (0)

static void test_math(void) {
    CHECK(wl_isqrt(0) == 0);
    CHECK(wl_isqrt(-5) == 0);
    CHECK(wl_isqrt(1) == 1);
    CHECK(wl_isqrt(144) == 12);
    CHECK(wl_isqrt(145) == 12);
    CHECK(wl_isqrt(24 * 24) == 24);
    CHECK(wl_lerp(10, 20, 0) == 10);
    CHECK(wl_lerp(10, 20, 100) == 20);
    CHECK(wl_lerp(10, 20, 50) == 15);
    CHECK(wl_lerp(20, 10, 50) == 15);  // shrinking
}

// Regression: after the minute animation the puddle grew to full size and
// covered Quick View; every size now goes through this clamp.
static void test_quick_view_clamp(void) {
    int16_t pw, ph;
    wl_clamp_puddle(100, 80, 59, &pw, &ph);
    CHECK(pw == 80 && ph == 59);                 // plenty of room
    wl_clamp_puddle(40, 80, 59, &pw, &ph);
    CHECK(pw == 80 && ph == 40);                 // shrunk to fit
    wl_clamp_puddle(10, 80, 59, &pw, &ph);
    CHECK(pw == 80 && ph == 10);                 // smallest visible puddle
    wl_clamp_puddle(9, 80, 59, &pw, &ph);
    CHECK(pw == 0 && ph == 0);                   // hidden: time moves to body
    wl_clamp_puddle(-30, 80, 59, &pw, &ph);
    CHECK(pw == 0 && ph == 0);
}

static void test_settings(void) {
    Settings s;
    wl_settings_defaults(&s, 0xF3, 0xC4);
    CHECK(s.version == SETTINGS_VERSION);
    CHECK(s.sleepy);
    CHECK(!s.show_seconds && !s.show_date && !s.bt_vibrate);
    CHECK(s.slot_left == SLOT_NONE && s.slot_right == SLOT_NONE);
    CHECK(s.wall_argb == 0xF3 && s.floor_argb == 0xC4);

    // 1.x upgrade keeps the old choices, everything else stays default
    wl_settings_from_v1(&s, 2, true);
    CHECK(s.time_format == 2 && s.show_date);
    CHECK(s.sleepy && s.slot_left == SLOT_NONE);

    // Garbage from storage or Clay is pulled back into range
    s.time_format = 9; s.date_format = 9; s.slot_left = 77; s.slot_right = 5;
    s.temp_units = 3; s.theme = 200;
    wl_settings_sanitize(&s);
    CHECK(s.time_format == 0 && s.date_format == 0);
    CHECK(s.slot_left == SLOT_NONE && s.slot_right == SLOT_NONE);
    CHECK(s.temp_units == 0 && s.theme == 0);

    // Valid values are untouched
    s.time_format = 1; s.slot_left = SLOT_BATTERY; s.theme = THEME_CUSTOM;
    wl_settings_sanitize(&s);
    CHECK(s.time_format == 1 && s.slot_left == SLOT_BATTERY && s.theme == THEME_CUSTOM);
}

static void test_time_text(void) {
    char b[8];
    strcpy(b, "09:30"); wl_trim_leading_zero(b); CHECK_STR(b, "9:30");
    strcpy(b, "12:05"); wl_trim_leading_zero(b); CHECK_STR(b, "12:05");
    strcpy(b, "10:00"); wl_trim_leading_zero(b); CHECK_STR(b, "10:00");
    strcpy(b, "0");     wl_trim_leading_zero(b); CHECK_STR(b, "0");
    strcpy(b, "");      wl_trim_leading_zero(b); CHECK_STR(b, "");
}

static void test_weather_text(void) {
    char b[16];
    Weather w = { .temp = 72, .code = WX_CLEAR, .updated = 1000 };
    wl_weather_text(&w, 1000 + 60, b, sizeof(b));
    CHECK_STR(b, "72\xC2\xB0");
    w.temp = -5;
    wl_weather_text(&w, 1000 + 60, b, sizeof(b));
    CHECK_STR(b, "-5\xC2\xB0");
    wl_weather_text(&w, 1000 + WEATHER_STALE_S, b, sizeof(b));      // exactly 3h: still fresh
    CHECK_STR(b, "-5\xC2\xB0");
    wl_weather_text(&w, 1000 + WEATHER_STALE_S + 1, b, sizeof(b));  // stale
    CHECK_STR(b, "--\xC2\xB0");
    w.updated = 0;                                                  // never updated
    wl_weather_text(&w, 5000, b, sizeof(b));
    CHECK_STR(b, "--\xC2\xB0");
}

static void test_steps_and_heart_text(void) {
    char b[12];
    CHECK(!wl_steps_text(-1, b, sizeof(b)));
    CHECK(wl_steps_text(0, b, sizeof(b)));        CHECK_STR(b, "0");
    CHECK(wl_steps_text(9999, b, sizeof(b)));     CHECK_STR(b, "9999");
    CHECK(wl_steps_text(10000, b, sizeof(b)));    CHECK_STR(b, "10.0k");
    CHECK(wl_steps_text(12345, b, sizeof(b)));    CHECK_STR(b, "12.3k");
    CHECK(wl_steps_text(100999, b, sizeof(b)));   CHECK_STR(b, "100.9k");
    wl_heart_text(72, b, sizeof(b));              CHECK_STR(b, "72");
    wl_heart_text(0, b, sizeof(b));               CHECK_STR(b, "--");
    wl_heart_text(-1, b, sizeof(b));              CHECK_STR(b, "--");
    // Output never overruns a small buffer
    char tiny[4];
    wl_steps_text(123456, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 3);
}

static void test_theme_contrast(void) {
    CHECK(wl_dark_text_on(3, 3, 2));   // pastel yellow wall -> dark text
    CHECK(wl_dark_text_on(3, 3, 3));   // white
    CHECK(!wl_dark_text_on(0, 0, 0));  // black
    CHECK(!wl_dark_text_on(0, 0, 1));  // oxford blue
    CHECK(!wl_dark_text_on(1, 1, 1));  // dark gray
}

int main(void) {
    test_math();
    test_quick_view_clamp();
    test_settings();
    test_time_text();
    test_weather_text();
    test_steps_and_heart_text();
    test_theme_contrast();
    printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
