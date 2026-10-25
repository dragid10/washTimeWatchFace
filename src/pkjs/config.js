var SLOT_OPTIONS = [
  { "label": "Nothing", "value": "0" },
  { "label": "Weather", "value": "1" },
  { "label": "Steps", "value": "2" },
  { "label": "Heart rate", "value": "3" },
  { "label": "Battery", "value": "4" }
];

module.exports = [
  {
    "type": "heading",
    "defaultValue": "Mr. Washy"
  },
  {
    "type": "text",
    "defaultValue": "Make the laundry room yours."
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Time & Date" },
      {
        "type": "select",
        "messageKey": "TimeFormat",
        "label": "Time format",
        "defaultValue": "0",
        "options": [
          { "label": "Watch default", "value": "0" },
          { "label": "12-hour", "value": "1" },
          { "label": "24-hour", "value": "2" }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "ShowSeconds",
        "label": "Show seconds",
        "description": "Uses more battery.",
        "defaultValue": false
      },
      {
        "type": "toggle",
        "messageKey": "ShowDate",
        "label": "Show date",
        "defaultValue": false
      },
      {
        "type": "select",
        "messageKey": "DateFormat",
        "label": "Date style",
        "defaultValue": "0",
        "options": [
          { "label": "Sat Oct 25", "value": "0" },
          { "label": "10/25", "value": "1" },
          { "label": "25/10", "value": "2" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Extras" },
      {
        "type": "text",
        "defaultValue": "Show extra info in the top corners, above Mr. Washy's arms. Steps come from your Pebble's own tracking, so they can differ a little from Google Health. Heart rate shows the watch's latest reading, so it can be a few minutes old. How often the watch measures is set by the system and your Health settings in the Pebble app."
      },
      {
        "type": "select",
        "messageKey": "SlotLeft",
        "label": "Top left",
        "defaultValue": "0",
        "options": SLOT_OPTIONS
      },
      {
        "type": "select",
        "messageKey": "SlotRight",
        "label": "Top right",
        "defaultValue": "0",
        "options": SLOT_OPTIONS
      },
      {
        "type": "radiogroup",
        "messageKey": "TempUnits",
        "label": "Temperature",
        "defaultValue": "0",
        "options": [
          { "label": "Fahrenheit", "value": "0" },
          { "label": "Celsius", "value": "1" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "capabilities": ["COLOR"],
    "items": [
      { "type": "heading", "defaultValue": "Theme" },
      {
        "type": "select",
        "messageKey": "Theme",
        "label": "Laundry room",
        "defaultValue": "0",
        "options": [
          { "label": "Classic", "value": "0" },
          { "label": "Midnight", "value": "1" },
          { "label": "Mint", "value": "2" },
          { "label": "Lavender", "value": "3" },
          { "label": "Sunset", "value": "4" },
          { "label": "Custom", "value": "5" }
        ]
      },
      {
        "type": "color",
        "messageKey": "WallColor",
        "label": "Wall color",
        "defaultValue": "FFFFAA",
        "sunlight": true
      },
      {
        "type": "color",
        "messageKey": "FloorColor",
        "label": "Floor color",
        "defaultValue": "AA5500",
        "sunlight": true
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Watch" },
      {
        "type": "toggle",
        "messageKey": "SleepyMode",
        "label": "Sleepy Mr. Washy",
        "description": "Stop all animation during Quiet Time or while you're asleep.",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "BtVibrate",
        "label": "Buzz on phone disconnect",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
