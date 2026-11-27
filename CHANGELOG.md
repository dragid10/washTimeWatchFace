# Changelog

## Unreleased

### New
- More info corners: phone battery, when your next calendar event starts, and two of your favorite readings from the Pebble app (Settings, Plugins, Complications).
- Weather now comes from the weather provider you picked in the Pebble app, in your chosen units. Open-Meteo is still used when no weather provider answers.

### Better
- Asks only for what it uses: location for weather, health for steps and heart rate, and calendar for your next event. Each one is optional.
- When a corner can't show its reading, it says why: "Allow" when a permission is off, "Update" when the Pebble app is too old, and "Pick" when you haven't chosen favorite readings. The settings page explains what to do.

The new corners need Pebble app 2.0 or later.

## 1.2.0 - 2026-10-25

### New
- A proper settings page, organized into Time & Date, Extras, Theme and Watch.
- Info corners: show weather, steps, heart rate or battery in the top-left and top-right corners.
- Optional seconds next to the time.
- Color themes: Classic, Midnight, Mint, Lavender and Sunset, or pick your own wall and floor colors.
- Date styles: Sat Oct 25, 10/25 or 25/10.
- Sleepy Mr. Washy: stops all animation during Quiet Time or while you're asleep, to save battery.
- Optional buzz when your phone disconnects.

### Fixed
- The 12/24-hour setting now actually applies.
- Quick View no longer leaves the puddle covering notifications, and the time fits properly on his body.
- The date now sits inside the puddle instead of on the floor.
- 12-hour time no longer shows a leading zero (9:30, not 09:30).
- The door handle no longer shakes extra, and the sock now tumbles as it falls.

### Better
- Uses less battery: Mr. Washy blinks every 5 to 9 seconds instead of every 2 to 4.
- Your existing settings carry over from 1.1.

Weather is provided by Open-Meteo using your phone's location.

## 1.1.0 - 2026-04-04

- Fixed the date clipping and overlapping the time on round displays.
- Added Quick View support (the machine stays full size and the puddle adapts), with the time shown on his body when there's no room for the puddle.

## 1.0.0 - 2026-04-04

- First release as Mr. Washy: an animated washing machine whose door opens each minute and overflows to show the time.
- Blinking, looking-around eyes, tumbling laundry and a falling sock.
- Time format and date settings.
- Support for all Pebble watches (Aplite, Basalt, Chalk, Diorite, Emery, Flint and Gabbro).
