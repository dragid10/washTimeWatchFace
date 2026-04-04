# CLAUDE.md

## Overview

WashTime is an animated Pebble watchface featuring an anthropomorphic front-load washing machine character. Each minute, the door opens, water overflows and pools on the ground to display the current time, then recedes. Between minutes, the character blinks and looks around.

**Target platform:** Emery (Pebble Time 2, 200x228 color rectangular display, 64-color)

**SDK reference:** https://developer.repebble.com/sdk/

## Project Structure

```
washTimeWatchFace/
  package.json              # Pebble app manifest (UUID, platforms, messageKeys, Clay dep)
  wscript                   # Waf build configuration (do not modify unless adding worker)
  src/
    c/
      washTimeWatchFace.c   # Complete watchface implementation (single file)
    pkjs/
      index.js              # PebbleKit JS entry — Clay configuration init
      config.js             # Clay config page definition (time format, show date)
  resources/                # Empty — no custom resources (uses system fonts only)
  build/                    # Build output (gitignored)
    washTimeWatchFace.pbw   # Installable watchface bundle
  node_modules/             # npm deps — @rebble/clay (gitignored)
```

## Build & Test Commands

```bash
pebble build                                    # Compile → build/washTimeWatchFace.pbw
pebble install --emulator emery                 # Install in QEMU emulator
pebble screenshot --no-open --emulator emery    # Capture emulator screenshot
pebble logs --emulator emery                    # View runtime logs
pebble install --cloudpebble                    # Deploy to physical watch
pebble login                                    # Auth for app store publishing
pebble publish                                  # Publish to Pebble App Store
```

After modifying `package.json` dependencies, run `npm install` before `pebble build`.

## Architecture

### C Code (washTimeWatchFace.c)

Single-file architecture with these sections:

| Section | Purpose |
|---------|---------|
| Constants & Types | Animation timing, frame counts, `AnimState` enum, `Drop` struct, `Settings` struct |
| Globals | Window, Layer, timers, animation state, eye/arm/wash phases, puddle size, time buffers |
| Layout | `compute_layout()` — derives all positions from `layer_get_bounds()` using ratios |
| Settings | `load_settings()` / `save_settings()` via `persist_read_data` / `persist_write_data` |
| Drawing | 10 draw functions called back-to-front in `canvas_update()` |
| Updates | `update_idle()` — wash cycle, arm sway, blink, eye look-around |
| Timers | Idle timer (200ms/5FPS) for idle animations, Anim timer (50ms/20FPS) for overflow |
| State Machine | `advance_state()` — ST_IDLE → ST_OPEN → ST_OVER → ST_POOL → ST_RECEDE → ST_SETTLE → ST_IDLE |
| AppMessage | `inbox_received_callback()` — receives Clay settings (TimeFormat, ShowDate) |

### Draw Order (painter's algorithm)

```
1. Background (wall + floor + ground line)
2. Puddle (blue rounded rect below ground)
3. Time text (+ optional date, centered in puddle)
4. Legs & feet
5. Machine body (white rounded rect + shadow)
6. Door (blue circle with suds, arc frame, water drain animation)
7. Control knobs
8. Arms (stick arms with white gloved hands)
9. Eyes (sclera + pupil with look-around offset + specular highlight)
10. Water drops (blue circles falling from door)
```

### Animation States

```
IDLE ──(minute tick)──→ OPEN (8fr) → OVER (14fr) → POOL (10fr) → RECEDE (14fr) → SETTLE (4fr) → IDLE
                         door opens    drops fall    time shows    door closes     animation ends
                         shake on      puddle grows  puddle max    puddle shrinks  idle timer restarts
```

- **Idle timer** (200ms): runs continuously for wash cycle rotation, eye blink/look-around, arm sway
- **Anim timer** (50ms): runs only during overflow animation, then stops

### Clay Configuration

Settings page with two options:
- **Time Format**: System Default / 12-Hour / 24-Hour (select, messageKey: `TimeFormat`)
- **Show Date**: toggle (messageKey: `ShowDate`)

Settings are persisted via `persist_write_data` with key `SETTINGS_KEY=1`.

## Key Constraints

- **No floating point** — use `sin_lookup()` / `cos_lookup()` with `TRIG_MAX_ANGLE` / `TRIG_MAX_RATIO`
- **MINUTE_UNIT only** for `tick_timer_service_subscribe` — never SECOND_UNIT
- **Dynamic bounds** — all positions derived from `layer_get_bounds()`, never hardcoded pixel values
- **Clean resources** — all timers cancelled and layers destroyed in `window_unload()`
- **Register before open** — `app_message_register_inbox_received()` must be called before `app_message_open()`
- **Message keys** — declared as `extern uint32_t MESSAGE_KEY_*` (auto-generated in `message_keys.auto.c`)

## Emery Screen Reference

| Property | Value |
|----------|-------|
| Resolution | 200 x 228 px |
| Shape | Rectangular |
| Colors | 64-color |
| Center | (100, 114) |

## Layout Positions (computed for Emery)

| Element | Position | Notes |
|---------|----------|-------|
| Eyes | Y~18, spread 20px from center | radius 10, pupil radius 5 |
| Body | GRect(50, 29, 100, 104) | white, corner radius 6 |
| Door | center (100, 70), r=28/ir=24 | blue circle, gray frame |
| Knobs | Y~112 | two gray + one red (power) |
| Arms | shoulders at body edges, Y~70 | sway ±5px via sin_lookup |
| Legs | Y=133 to Y~147 | dark gray, 3px stroke |
| Ground | Y=151 | brown floor below |
| Puddle | Y=153, hw=80, h=59 (rest) | blue, holds time text |
