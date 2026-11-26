// Helpers for reading Pebble plugin sources (no Pebble APIs here so they can
// be unit tested in node). See https://developer.repebble.com/guides/plugins/preview/

// Slot values from src/c/logic.h
var SLOT_WEATHER = 1, SLOT_PHONE_BATTERY = 5, SLOT_NEXT_EVENT = 6, SLOT_FAV_1 = 7, SLOT_FAV_2 = 8;

// Watch icon categories (WX_* in src/c/logic.h)
var WX_CLEAR = 0, WX_CLOUDY = 1, WX_RAIN = 2, WX_SNOW = 3, WX_STORM = 4, WX_FOG = 5;

// Longest text the watch keeps for a favorite reading (PLUGIN_TEXT_LEN - 1 bytes)
var MAX_TEXT_BYTES = 11;

function slotsUsed(settings) {
  var used = {};
  [settings.SlotLeft, settings.SlotRight].forEach(function(v) {
    var n = parseInt(v, 10);
    if (n > 0) used[n] = true;
  });
  return used;
}

function text(prop) {
  if (!prop) return null;
  if (prop.shortText && typeof prop.shortText.text === 'string') return prop.shortText.text;
  if (prop.longText && typeof prop.longText.text === 'string') return prop.longText.text;
  return null;
}

// Map a weather plugin's condition_code to the watch's icon categories
function conditionToCategory(code) {
  switch (String(code || '').toLowerCase()) {
    case 'sun': case 'clear': return WX_CLEAR;
    case 'light_rain': case 'heavy_rain': case 'rain': return WX_RAIN;
    case 'light_snow': case 'heavy_snow': case 'rain_and_snow': case 'snow': return WX_SNOW;
    case 'thunderstorm': case 'storm': return WX_STORM;
    case 'fog': return WX_FOG;
    default: return WX_CLOUDY;  // partly_cloudy, cloudy, unknown
  }
}

// Convert a temperature numericValue to whole degrees in the user's chosen unit
function convertTemp(numeric, celsius) {
  if (!numeric || typeof numeric.value !== 'number') return null;
  var v = numeric.value;
  var isF = /F/i.test(numeric.unit || '');
  if (celsius && isF) v = (v - 32) * 5 / 9;
  if (!celsius && !isF) v = v * 9 / 5 + 32;
  return Math.round(v);
}

// weather/location update -> the AppMessage the watch expects, or null
function weatherMessage(instances, celsius) {
  var here = instances && instances[0];
  if (!here || !here.properties) return null;
  var temp = here.properties.temperature;
  var t = convertTemp(temp && temp.numericValue, celsius);
  if (t === null) return null;
  return {
    WeatherTemp: t,
    WeatherCode: conditionToCategory(text(here.properties.condition_code))
  };
}

// phone/phone_state update -> battery percent, or -1 when unknown
function phoneBattery(instances) {
  var phone = instances && instances[0];
  var level = phone && phone.properties && phone.properties.battery_level;
  if (level && level.numericValue && typeof level.numericValue.value === 'number') {
    return Math.max(0, Math.min(100, Math.round(level.numericValue.value)));
  }
  var t = text(level);
  var n = t ? parseInt(t, 10) : NaN;
  return isNaN(n) ? -1 : Math.max(0, Math.min(100, n));
}

function timestampOf(prop) {
  return prop && prop.timestamp && typeof prop.timestamp.value === 'number' ? prop.timestamp.value : 0;
}

// calendar/event update -> start (epoch s) of the next event not yet over, 0 if none
function nextEventStart(instances, nowSec) {
  var best = 0;
  (instances || []).forEach(function(i) {
    var p = i && i.properties;
    if (!p) return;
    var start = timestampOf(p.starts_at);
    if (!start) return;
    var end = timestampOf(p.ends_at) || start;
    if (end < nowSec) return;
    if (!best || start < best) best = start;
  });
  return best;
}

// preferences/complications update -> the reading for favorite n (0-based)
function favoriteReading(instances, n) {
  var slot = instances && instances[n];
  var p = slot && slot.properties;
  if (!p) return null;
  var category = text(p.category), item = text(p.item), property = text(p.property);
  if (!category || !item || !property) return null;
  var index = p.index && p.index.numericValue ? p.index.numericValue.value : 0;
  return { category: category, item: item, property: property, index: index || 0 };
}

function utf8Length(s) {
  return unescape(encodeURIComponent(s)).length;
}

// Cut text to what the watch can hold without splitting a character
function fitText(s) {
  var out = '';
  for (var i = 0; i < s.length; i++) {
    var ch = s.charAt(i);
    var code = s.charCodeAt(i);
    if (code >= 0xD800 && code <= 0xDBFF && i + 1 < s.length) ch += s.charAt(++i);
    if (utf8Length(out + ch) > MAX_TEXT_BYTES) break;
    out += ch;
  }
  return out;
}

// A reading's property -> short text for a slot ('' when nothing to show)
function readingText(prop) {
  if (!prop) return '';
  var t = prop.shortText && prop.shortText.text;
  if (typeof t !== 'string' && prop.numericValue && typeof prop.numericValue.value === 'number') {
    var n = prop.numericValue;
    t = String(Math.round(n.value)) + (n.unit || '');
  }
  if (typeof t !== 'string' && prop.boolean) t = prop.boolean.value ? 'On' : 'Off';
  if (typeof t !== 'string') t = prop.longText && prop.longText.text;
  return typeof t === 'string' ? fitText(t) : '';
}

module.exports = {
  SLOT_WEATHER: SLOT_WEATHER,
  SLOT_PHONE_BATTERY: SLOT_PHONE_BATTERY,
  SLOT_NEXT_EVENT: SLOT_NEXT_EVENT,
  SLOT_FAV_1: SLOT_FAV_1,
  SLOT_FAV_2: SLOT_FAV_2,
  slotsUsed: slotsUsed,
  conditionToCategory: conditionToCategory,
  convertTemp: convertTemp,
  weatherMessage: weatherMessage,
  phoneBattery: phoneBattery,
  nextEventStart: nextEventStart,
  favoriteReading: favoriteReading,
  readingText: readingText,
  fitText: fitText
};
