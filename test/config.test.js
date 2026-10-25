const test = require('node:test');
const assert = require('node:assert');
const pkg = require('../package.json');
const config = require('../src/pkjs/config.js');

// Flatten Clay's nested config into its input items
function items(list, out = []) {
  for (const it of list) {
    if (it.items) items(it.items, out); else out.push(it);
  }
  return out;
}
const inputs = items(config).filter((i) => i.messageKey);
const declared = pkg.pebble.messageKeys;

test('every setting the page sends is declared in package.json', () => {
  for (const i of inputs) {
    assert.ok(declared.includes(i.messageKey), `${i.messageKey} missing from messageKeys`);
  }
});

test('message keys are unique and each one is used', () => {
  assert.strictEqual(new Set(declared).size, declared.length);
  // Weather keys belong to the pkjs <-> watch weather messages, not the page
  const internal = ['WeatherTemp', 'WeatherCode', 'WeatherRequest'];
  for (const k of declared) {
    if (internal.includes(k)) continue;
    assert.ok(inputs.some((i) => i.messageKey === k), `${k} declared but not on the page`);
  }
});

test('no message key is used by two inputs', () => {
  const keys = inputs.map((i) => i.messageKey);
  assert.strictEqual(new Set(keys).size, keys.length);
});

// Regression: Clay sends select/radio values as strings, and the watch
// reads them with atoi(), so they must be numeric strings.
test('select and radio values are numeric strings', () => {
  for (const i of inputs.filter((x) => x.type === 'select' || x.type === 'radiogroup')) {
    for (const o of i.options) {
      assert.strictEqual(typeof o.value, 'string', `${i.messageKey}: ${o.label}`);
      assert.match(o.value, /^\d+$/, `${i.messageKey}: ${o.label}`);
    }
  }
});

test('select defaults are one of the options', () => {
  for (const i of inputs.filter((x) => x.type === 'select' || x.type === 'radiogroup')) {
    assert.ok(i.options.some((o) => o.value === i.defaultValue), `${i.messageKey} default`);
  }
});

test('option values stay inside the ranges the watch accepts', () => {
  const max = { TimeFormat: 2, DateFormat: 2, SlotLeft: 4, SlotRight: 4, TempUnits: 1, Theme: 5 };
  for (const [key, hi] of Object.entries(max)) {
    const input = inputs.find((i) => i.messageKey === key);
    for (const o of input.options) {
      assert.ok(Number(o.value) <= hi, `${key} option ${o.label} = ${o.value} > ${hi}`);
    }
  }
});

test('toggles default to booleans and colors to hex', () => {
  for (const i of inputs.filter((x) => x.type === 'toggle')) {
    assert.strictEqual(typeof i.defaultValue, 'boolean', i.messageKey);
  }
  for (const i of inputs.filter((x) => x.type === 'color')) {
    assert.match(i.defaultValue, /^[0-9A-F]{6}$/, i.messageKey);
  }
});

test('Sleepy mode defaults on (matches the watch default)', () => {
  assert.strictEqual(inputs.find((i) => i.messageKey === 'SleepyMode').defaultValue, true);
});
