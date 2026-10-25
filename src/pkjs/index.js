var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');
var weather = require('./weather');

// Handle events ourselves so weather refreshes right after settings change
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

function savedSettings() {
  try {
    return JSON.parse(localStorage.getItem('clay-settings')) || {};
  } catch (e) {
    return {};
  }
}

function weatherWanted(s) {
  return String(s.SlotLeft) === '1' || String(s.SlotRight) === '1';
}

function fetchWeather() {
  var s = savedSettings();
  if (!weatherWanted(s)) return;
  navigator.geolocation.getCurrentPosition(function(pos) {
    var url = weather.weatherUrl(pos.coords.latitude, pos.coords.longitude,
                                 String(s.TempUnits) === '1');
    var xhr = new XMLHttpRequest();
    xhr.onload = function() {
      var msg = null;
      try { msg = weather.parseWeather(JSON.parse(this.responseText)); } catch (e) {}
      if (!msg) { console.log('Weather: bad response ' + this.status); return; }
      Pebble.sendAppMessage(msg, null, function() { console.log('Weather: send failed'); });
    };
    xhr.onerror = function() { console.log('Weather: request failed'); };
    xhr.open('GET', url);
    xhr.send();
  }, function(err) {
    console.log('Weather: location error ' + err.code);
  }, { timeout: 15000, maximumAge: 30 * 60 * 1000 });
}

Pebble.addEventListener('ready', function() {
  fetchWeather();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload && e.payload.WeatherRequest) fetchWeather();
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  var dict = clay.getSettings(e.response);
  Pebble.sendAppMessage(dict, function() {
    fetchWeather();  // units or slots may have changed
  }, function(err) {
    console.log('Settings: send failed ' + JSON.stringify(err));
  });
});
