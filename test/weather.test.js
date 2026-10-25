const test = require('node:test');
const assert = require('node:assert');
const w = require('../src/pkjs/weather.js');

test('WMO codes map to icon categories', () => {
  assert.strictEqual(w.wmoToCategory(0), 0);   // clear
  assert.strictEqual(w.wmoToCategory(1), 0);
  assert.strictEqual(w.wmoToCategory(2), 1);   // cloudy
  assert.strictEqual(w.wmoToCategory(3), 1);
  assert.strictEqual(w.wmoToCategory(45), 5);  // fog
  assert.strictEqual(w.wmoToCategory(48), 5);
  assert.strictEqual(w.wmoToCategory(53), 2);  // drizzle -> rain
  assert.strictEqual(w.wmoToCategory(65), 2);
  assert.strictEqual(w.wmoToCategory(81), 2);  // showers
  assert.strictEqual(w.wmoToCategory(73), 3);  // snow
  assert.strictEqual(w.wmoToCategory(86), 3);
  assert.strictEqual(w.wmoToCategory(95), 4);  // thunderstorm
  assert.strictEqual(w.wmoToCategory(99), 4);
});

test('URL carries location and unit', () => {
  const f = w.weatherUrl(40.71234, -74.00601, false);
  assert.match(f, /latitude=40\.712&longitude=-74\.006/);
  assert.match(f, /temperature_unit=fahrenheit/);
  assert.match(w.weatherUrl(1, 2, true), /temperature_unit=celsius/);
});

test('parseWeather rounds temperature and maps code', () => {
  assert.deepStrictEqual(
    w.parseWeather({ current: { temperature_2m: 71.6, weather_code: 61 } }),
    { WeatherTemp: 72, WeatherCode: 2 });
  assert.deepStrictEqual(
    w.parseWeather({ current: { temperature_2m: -0.4, weather_code: 0 } }),
    { WeatherTemp: -0, WeatherCode: 0 });
});

test('parseWeather rejects bad responses', () => {
  assert.strictEqual(w.parseWeather(null), null);
  assert.strictEqual(w.parseWeather({}), null);
  assert.strictEqual(w.parseWeather({ current: {} }), null);
});
