// Runs inside the Clay config page: show the custom color pickers only for
// the Custom theme, and the date style only when the date is shown.
module.exports = function() {
  var clay = this;

  function bind(triggerKey, targetKeys, isOn) {
    var trigger = clay.getItemByMessageKey(triggerKey);
    if (!trigger) return;
    function sync() {
      var on = isOn(trigger.get());
      targetKeys.forEach(function(key) {
        var item = clay.getItemByMessageKey(key);
        if (item) { if (on) { item.show(); } else { item.hide(); } }
      });
    }
    sync();
    trigger.on('change', sync);
  }

  clay.on(clay.EVENTS.AFTER_BUILD, function() {
    bind('Theme', ['WallColor', 'FloorColor'], function(v) { return String(v) === '5'; });
    bind('ShowDate', ['DateFormat'], function(v) { return !!v; });
  });
};
