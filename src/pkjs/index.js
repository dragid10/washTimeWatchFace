var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');
var weather = require('./weather');
var plugins = require('./plugins');

// Handle events ourselves so slots refresh right after settings change.
// Rebuilt each time the page opens, so it can say what a corner needs.
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

function savedSettings() {
  try {
    return JSON.parse(localStorage.getItem('clay-settings')) || {};
  } catch (e) {
    return {};
  }
}

function celsius(s) {
  return String(s.TempUnits) === '1';
}

// ---- AppMessage queue: the watch takes one message at a time ----

var outbox = [];
var sending = false;

function send(msg, onDone) {
  outbox.push({ msg: msg, onDone: onDone });
  if (!sending) sendNext();
}

function sendNext() {
  var next = outbox.shift();
  if (!next) { sending = false; return; }
  sending = true;
  Pebble.sendAppMessage(next.msg, function() {
    if (next.onDone) next.onDone();
    sendNext();
  }, function() {
    console.log('Send failed: ' + JSON.stringify(next.msg));
    sendNext();
  });
}

// ---- Weather: from the user's weather plugin, else Open-Meteo ----

var lastPluginWeather = null;   // { msg, at }
var PLUGIN_WEATHER_FRESH_MS = 45 * 60 * 1000;

function fetchOpenMeteo() {
  var s = savedSettings();
  navigator.geolocation.getCurrentPosition(function(pos) {
    var url = weather.weatherUrl(pos.coords.latitude, pos.coords.longitude, celsius(s));
    var xhr = new XMLHttpRequest();
    xhr.onload = function() {
      var msg = null;
      try { msg = weather.parseWeather(JSON.parse(this.responseText)); } catch (e) {}
      if (!msg) { console.log('Weather: bad response ' + this.status); return; }
      send(msg);
    };
    xhr.onerror = function() { console.log('Weather: request failed'); };
    xhr.open('GET', url);
    xhr.send();
  }, function(err) {
    console.log('Weather: location error ' + err.code);
    if (err && err.code === 1) setState(plugins.PS_WEATHER, plugins.PSTATE_ALLOW);  // PERMISSION_DENIED
  }, { timeout: 15000, maximumAge: 30 * 60 * 1000 });
}

function refreshWeather() {
  if (lastPluginWeather && Date.now() - lastPluginWeather.at < PLUGIN_WEATHER_FRESH_MS) {
    send(lastPluginWeather.msg);
  } else {
    fetchOpenMeteo();
  }
}

// ---- Plugin subscriptions ----

var subs = {};   // name -> subscription
// Last plugin readings sent, resent when the watch asks so they don't go stale
// on the watch while a source has nothing new to report
var lastReadings = {};

function sendReading(msg) {
  Object.keys(msg).forEach(function(k) { lastReadings[k] = msg[k]; });
  send(msg);
}
// What each source needs from the user (PSTATE_*), mirrored to the watch
var states = {};

function setState(source, state) {
  if ((states[source] || 0) === state) return;
  states[source] = state;
  sendReading({ PluginStatus: plugins.packStatus(states) });
}

var favSubs = [null, null];
var favKeys = ['FavText1', 'FavText2'];

function pluginsAvailable() {
  return typeof Pebble.subscribeToSource === 'function';
}

function stop(name) {
  if (subs[name]) {
    try { subs[name].unsubscribe(); } catch (e) {}
    delete subs[name];
  }
}

function subscribe(name, request) {
  if (subs[name]) return;
  try {
    subs[name] = Pebble.subscribeToSource(request);
  } catch (e) {
    console.log('Plugins: ' + name + ' subscribe failed ' + e);
    if (request.onError) request.onError({ code: 'UNKNOWN', message: String(e) });
  }
}

function stopFavorite(n) {
  if (favSubs[n]) {
    try { favSubs[n].sub.unsubscribe(); } catch (e) {}
    favSubs[n] = null;
  }
}

function sendFavorite(n, value) {
  var msg = {};
  msg[favKeys[n]] = value;
  sendReading(msg);
}

// Follow favorite reading n (0-based) from the user's list in the Pebble app
function followFavorite(n, reading) {
  var key = reading && [reading.category, reading.item, reading.property, reading.index].join('/');
  if (favSubs[n] && favSubs[n].key === key) return;
  stopFavorite(n);
  if (!reading) {
    sendFavorite(n, '');
    setState(n ? plugins.PS_FAV_2 : plugins.PS_FAV_1, plugins.PSTATE_PICK);
    return;
  }
  setState(n ? plugins.PS_FAV_2 : plugins.PS_FAV_1, plugins.PSTATE_OK);
  var sub = null;
  try {
    sub = Pebble.subscribeToSource({
      category: reading.category,
      item: reading.item,
      properties: [reading.property],
      onData: function(update) {
        var instance = (update.instances || [])[reading.index];
        setState(n ? plugins.PS_FAV_2 : plugins.PS_FAV_1, plugins.PSTATE_OK);
        sendFavorite(n, plugins.readingText(instance && instance.properties[reading.property]));
      },
      onError: function(err) {
        console.log('Plugins: favorite ' + (n + 1) + ' ' + err.code);
        setState(n ? plugins.PS_FAV_2 : plugins.PS_FAV_1, plugins.stateForError(err));
      }
    });
  } catch (e) {
    console.log('Plugins: favorite subscribe failed ' + e);
  }
  favSubs[n] = { key: key, sub: sub };
}

function syncPlugins() {
  var s = savedSettings();
  var used = plugins.slotsUsed(s);

  if (!pluginsAvailable()) {
    // Pebble app before 2.0.0: only weather works, from Open-Meteo. The other
    // plugin corners say "Update" instead of a reading.
    if (used[plugins.SLOT_WEATHER]) fetchOpenMeteo();
    [plugins.PS_PHONE, plugins.PS_EVENT, plugins.PS_FAV_1, plugins.PS_FAV_2].forEach(function(src) {
      setState(src, plugins.PSTATE_UPDATE);
    });
    return;
  }

  if (used[plugins.SLOT_WEATHER]) {
    subscribe('weather', {
      category: 'weather',
      item: 'location',
      properties: ['temperature', 'condition_code'],
      onData: function(update) {
        var msg = plugins.weatherMessage(update.instances, celsius(savedSettings()));
        if (!msg) { fetchOpenMeteo(); return; }
        lastPluginWeather = { msg: msg, at: Date.now() };
        setState(plugins.PS_WEATHER, plugins.PSTATE_OK);
        send(msg);
      },
      onError: function(err) {
        console.log('Plugins: weather ' + err.code + ', using Open-Meteo');
        setState(plugins.PS_WEATHER, plugins.stateForError(err));
        if (!lastPluginWeather) fetchOpenMeteo();
      }
    });
  } else {
    stop('weather');
    lastPluginWeather = null;
  }

  if (used[plugins.SLOT_PHONE_BATTERY]) {
    subscribe('phone', {
      category: 'phone',
      item: 'phone_state',
      properties: ['battery_level'],
      onData: function(update) {
        setState(plugins.PS_PHONE, plugins.PSTATE_OK);
        sendReading({ PhoneBattery: plugins.phoneBattery(update.instances) });
      },
      onError: function(err) {
        console.log('Plugins: phone ' + err.code);
        setState(plugins.PS_PHONE, plugins.stateForError(err));
      }
    });
  } else {
    stop('phone');
  }

  if (used[plugins.SLOT_NEXT_EVENT]) {
    subscribe('calendar', {
      category: 'calendar',
      item: 'event',
      properties: ['starts_at', 'ends_at'],
      onData: function(update) {
        setState(plugins.PS_EVENT, plugins.PSTATE_OK);
        sendReading({ EventStart: plugins.nextEventStart(update.instances, Math.floor(Date.now() / 1000)) });
      },
      onError: function(err) {
        console.log('Plugins: calendar ' + err.code);
        setState(plugins.PS_EVENT, plugins.stateForError(err));
      }
    });
  } else {
    stop('calendar');
  }

  if (used[plugins.SLOT_FAV_1] || used[plugins.SLOT_FAV_2]) {
    subscribe('favorites', {
      category: 'preferences',
      item: 'complications',
      onData: function(update) {
        var now = plugins.slotsUsed(savedSettings());
        followFavorite(0, now[plugins.SLOT_FAV_1] ? plugins.favoriteReading(update.instances, 0) : null);
        followFavorite(1, now[plugins.SLOT_FAV_2] ? plugins.favoriteReading(update.instances, 1) : null);
      },
      onError: function(err) {
        console.log('Plugins: favorites ' + err.code);
        var state = plugins.stateForError(err);
        setState(plugins.PS_FAV_1, state);
        setState(plugins.PS_FAV_2, state);
      }
    });
  } else {
    stop('favorites');
    stopFavorite(0);
    stopFavorite(1);
  }
}

// Settings changed: drop subscriptions so units and slot choices apply
function resyncPlugins() {
  ['weather', 'phone', 'calendar', 'favorites'].forEach(stop);
  stopFavorite(0);
  stopFavorite(1);
  lastPluginWeather = null;
  lastReadings = {};
  states = {};
  sendReading({ PluginStatus: 0 });
  syncPlugins();
}

Pebble.addEventListener('ready', function() {
  syncPlugins();
});

Pebble.addEventListener('appmessage', function(e) {
  if (!e.payload || !e.payload.WeatherRequest) return;
  var used = plugins.slotsUsed(savedSettings());
  if (used[plugins.SLOT_WEATHER]) refreshWeather();
  if (!pluginsAvailable()) return;
  if (Object.keys(lastReadings).length) send(JSON.parse(JSON.stringify(lastReadings)));
  if (Object.keys(subs).length === 0) syncPlugins();
});

// Ask the phone about permissions the corners use, so the page can say what's
// off even before a corner has tried. Calls back with { source: PSTATE_* }.
function queryPermissions(done) {
  var checks = [[plugins.PS_WEATHER, 'Location'], [plugins.PS_EVENT, 'Calendar']];
  var found = {};
  if (!Pebble.permissions || typeof Pebble.permissions.query !== 'function') { done(found); return; }
  var left = checks.length;
  var finished = false;
  function one() {
    if (--left === 0 && !finished) { finished = true; done(found); }
  }
  setTimeout(function() { if (!finished) { finished = true; done(found); } }, 1500);
  checks.forEach(function(check) {
    try {
      Pebble.permissions.query(check[1]).then(function(answer) {
        if (answer === 'denied') found[check[0]] = plugins.PSTATE_ALLOW;
        one();
      }, one);
    } catch (e) { one(); }
  });
}

Pebble.addEventListener('showConfiguration', function() {
  queryPermissions(function(denied) {
    var now = {};
    Object.keys(states).forEach(function(k) { now[k] = states[k]; });
    Object.keys(denied).forEach(function(k) { now[k] = denied[k]; });
    var notes = plugins.settingsNotes(plugins.slotsUsed(savedSettings()), now, pluginsAvailable());
    clay = new Clay(withNotes(clayConfig, notes), customClay, { autoHandleEvents: false });
    Pebble.openURL(clay.generateUrl());
  });
});

// A copy of the page with what the corners need at the top of Extras
function withNotes(config, notes) {
  var copy = JSON.parse(JSON.stringify(config));
  if (!notes.length) return copy;
  copy.forEach(function(section) {
    if (!section.items || !section.items[0] || section.items[0].defaultValue !== 'Extras') return;
    section.items.splice(1, 0, {
      type: 'text',
      defaultValue: '<strong>Needs your attention</strong><br>' + notes.join('<br><br>')
    });
  });
  return copy;
}

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  var dict = clay.getSettings(e.response);
  send(dict, resyncPlugins);  // units or slots may have changed
});
