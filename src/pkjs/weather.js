// Weather helpers (no Pebble APIs here so they can be unit tested in node)

// Map a WMO weather code (Open-Meteo) to the watch's icon categories
var WX_CLEAR = 0, WX_CLOUDY = 1, WX_RAIN = 2, WX_SNOW = 3, WX_STORM = 4, WX_FOG = 5;

function wmoToCategory(code) {
  if (code <= 1) return WX_CLEAR;
  if (code <= 3) return WX_CLOUDY;
  if (code === 45 || code === 48) return WX_FOG;
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return WX_SNOW;
  if (code >= 95) return WX_STORM;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WX_RAIN;
  return WX_CLOUDY;
}

function weatherUrl(lat, lon, celsius) {
  return 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat.toFixed(3) +
    '&longitude=' + lon.toFixed(3) +
    '&current=temperature_2m,weather_code' +
    '&temperature_unit=' + (celsius ? 'celsius' : 'fahrenheit');
}

// Turn an Open-Meteo response into the AppMessage the watch expects
function parseWeather(json) {
  var cur = json && json.current;
  if (!cur || typeof cur.temperature_2m !== 'number') return null;
  return {
    WeatherTemp: Math.round(cur.temperature_2m),
    WeatherCode: wmoToCategory(cur.weather_code)
  };
}

module.exports = {
  wmoToCategory: wmoToCategory,
  weatherUrl: weatherUrl,
  parseWeather: parseWeather
};
