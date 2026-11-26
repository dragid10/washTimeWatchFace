var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');
var weather = require('./weather');
var plugins = require('./plugins');

// Handle events ourselves so slots refresh right after settings change
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
  if (!reading) { sendFavorite(n, ''); return; }
  var sub = null;
  try {
    sub = Pebble.subscribeToSource({
      category: reading.category,
      item: reading.item,
      properties: [reading.property],
      onData: function(update) {
        var instance = (update.instances || [])[reading.index];
        sendFavorite(n, plugins.readingText(instance && instance.properties[reading.property]));
      },
      onError: function(err) {
        console.log('Plugins: favorite ' + (n + 1) + ' ' + err.code);
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
    // Pebble app before 2.0.0: only weather works, from Open-Meteo
    if (used[plugins.SLOT_WEATHER]) fetchOpenMeteo();
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
        send(msg);
      },
      onError: function(err) {
        console.log('Plugins: weather ' + err.code + ', using Open-Meteo');
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
      onData: function(update) { sendReading({ PhoneBattery: plugins.phoneBattery(update.instances) }); },
      onError: function(err) { console.log('Plugins: phone ' + err.code); }
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
        sendReading({ EventStart: plugins.nextEventStart(update.instances, Math.floor(Date.now() / 1000)) });
      },
      onError: function(err) { console.log('Plugins: calendar ' + err.code); }
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
      onError: function(err) { console.log('Plugins: favorites ' + err.code); }
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

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  var dict = clay.getSettings(e.response);
  send(dict, resyncPlugins);  // units or slots may have changed
});
