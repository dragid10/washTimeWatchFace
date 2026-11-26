const test = require('node:test');
const assert = require('node:assert');
const p = require('../src/pkjs/plugins.js');

test('slotsUsed reads Clay string values', () => {
  assert.deepStrictEqual(p.slotsUsed({ SlotLeft: '1', SlotRight: '7' }), { 1: true, 7: true });
  assert.deepStrictEqual(p.slotsUsed({ SlotLeft: '0' }), {});
  assert.deepStrictEqual(p.slotsUsed({}), {});
});

test('condition codes map to icon categories', () => {
  assert.strictEqual(p.conditionToCategory('sun'), 0);
  assert.strictEqual(p.conditionToCategory('partly_cloudy'), 1);
  assert.strictEqual(p.conditionToCategory('cloudy'), 1);
  assert.strictEqual(p.conditionToCategory('light_rain'), 2);
  assert.strictEqual(p.conditionToCategory('heavy_rain'), 2);
  assert.strictEqual(p.conditionToCategory('light_snow'), 3);
  assert.strictEqual(p.conditionToCategory('rain_and_snow'), 3);
  assert.strictEqual(p.conditionToCategory('unknown'), 1);
  assert.strictEqual(p.conditionToCategory(undefined), 1);
});

test('temperatures convert to the face setting', () => {
  assert.strictEqual(p.convertTemp({ value: 20, unit: '°C' }, true), 20);
  assert.strictEqual(p.convertTemp({ value: 20, unit: '°C' }, false), 68);
  assert.strictEqual(p.convertTemp({ value: 68, unit: '°F' }, true), 20);
  assert.strictEqual(p.convertTemp({ value: 71.6, unit: '°F' }, false), 72);
  assert.strictEqual(p.convertTemp(null, false), null);
});

test('weatherMessage uses the first location', () => {
  const instances = [
    { instanceId: 'a', properties: {
      temperature: { numericValue: { value: 10, unit: '°C' }, shortText: { text: '10°C' } },
      condition_code: { longText: { text: 'light_rain' } } } },
    { instanceId: 'b', properties: { temperature: { numericValue: { value: 30, unit: '°C' } } } }
  ];
  assert.deepStrictEqual(p.weatherMessage(instances, true), { WeatherTemp: 10, WeatherCode: 2 });
  assert.deepStrictEqual(p.weatherMessage(instances, false), { WeatherTemp: 50, WeatherCode: 2 });
  assert.strictEqual(p.weatherMessage([], true), null);
  assert.strictEqual(p.weatherMessage([{ properties: {} }], true), null);  // no temperature yet
});

test('phoneBattery reads numericValue, then text, else -1', () => {
  assert.strictEqual(p.phoneBattery([{ properties: { battery_level: { numericValue: { value: 87 } } } }]), 87);
  assert.strictEqual(p.phoneBattery([{ properties: { battery_level: { shortText: { text: '40%' } } } }]), 40);
  assert.strictEqual(p.phoneBattery([{ properties: {} }]), -1);
  assert.strictEqual(p.phoneBattery([]), -1);
});

test('nextEventStart skips finished events and picks the soonest', () => {
  const ev = (start, end) => ({ properties: { starts_at: { timestamp: { value: start } },
                                               ends_at: { timestamp: { value: end } } } });
  assert.strictEqual(p.nextEventStart([ev(500, 900), ev(100, 200), ev(300, 400)], 250), 300);
  assert.strictEqual(p.nextEventStart([ev(100, 200)], 250), 0);
  assert.strictEqual(p.nextEventStart([ev(200, 400)], 250), 200);  // in progress
  assert.strictEqual(p.nextEventStart([], 250), 0);
});

test('favoriteReading decodes a preferences/complications slot', () => {
  const instances = [{ instanceId: '0', properties: {
    category: { shortText: { text: 'phone' } }, item: { shortText: { text: 'phone_state' } },
    property: { shortText: { text: 'battery_level' } }, index: { numericValue: { value: 0 } } } }];
  assert.deepStrictEqual(p.favoriteReading(instances, 0),
    { category: 'phone', item: 'phone_state', property: 'battery_level', index: 0 });
  assert.strictEqual(p.favoriteReading(instances, 1), null);
});

test('readingText prefers shortText and fits the watch buffer', () => {
  assert.strictEqual(p.readingText({ shortText: { text: '72°F' }, numericValue: { value: 72 } }), '72°F');
  assert.strictEqual(p.readingText({ numericValue: { value: 63.7, unit: '%' } }), '64%');
  assert.strictEqual(p.readingText({ boolean: { value: true } }), 'On');
  assert.strictEqual(p.readingText(null), '');
  assert.strictEqual(p.readingText({ longText: { text: 'Partly cloudy all day' } }), 'Partly clou');
  // 11 bytes max, never splitting a multi-byte character
  assert.strictEqual(p.fitText('°°°°°°°'), '°°°°°');
  assert.strictEqual(p.fitText('ab😀😀😀'), 'ab😀😀');
});
