module.exports = [
  {
    "type": "heading",
    "defaultValue": "WashTime Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Time & Date"
      },
      {
        "type": "select",
        "messageKey": "TimeFormat",
        "label": "Time Format",
        "defaultValue": 0,
        "options": [
          {"label": "System Default", "value": 0},
          {"label": "12-Hour", "value": 1},
          {"label": "24-Hour", "value": 2}
        ]
      },
      {
        "type": "toggle",
        "messageKey": "ShowDate",
        "label": "Show Date",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
