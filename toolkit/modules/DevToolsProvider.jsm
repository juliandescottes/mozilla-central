/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */

this.EXPORTED_SYMBOLS = [
  "DevToolsProvider",
  "gDevTools",
  "gDevToolsBrowser",
];

let _gDevTools, _gDevToolsBrowser, gDevToolsShim;

this.DevToolsProvider = {
  // Should be called by the devtools addon on startup()
  register: function (gDevTools, gDevToolsBrowser) {
    _gDevTools = gDevTools;
    _gDevToolsBrowser = gDevToolsBrowser;
    gDevToolsShim.attachAllEvents();
  },

  // Should be called by the devtools addon on shutdown()
  unregister: function () {
    _gDevTools = null;
    _gDevToolsBrowser = null;
  },

  /**
   * Bootstrap devtools addon installation wizard.
   */
  install: function () {
    // TODO
  }
};

/**
 * gDevTools is a singleton that controls the Firefox Developer Tools.
 *
 * It is an instance of a DevTools class that holds a set of tools. It has the
 * same lifetime as the browser.
 */
let gDevToolsMethods = [
  // Used by: - b2g desktop.js
  //          - Addon SDK
  "showToolbox",

  // Used by Addon SDK
  "closeToolbox",
  "getToolbox",
  "getTargetForTab",

  // Used by Addon SDK, main.js and tests:
  "registerTheme",
  "registerTool",
  "unregisterTheme",
  "unregisterTool",

  // Used by extensions/ext-devtools.js
];

this.gDevTools = {
  listeners: [],
  on: function (event, listener) {
    if (_gDevTools) {
      return _gDevTools.on(event, listener);
    }

    this.listeners.push([event, listener]);
  },

  attachAllEvents: function () {
    dump("[DevToolsProvider] attachAllEvents\n");
    for (let [event, listener] of this.listeners) {
      dump("[DevToolsProvider] attachAllEvents: attaching " + event + "\n");
      _gDevTools.on(event, listener);
    }

    _gDevTools.on("toolbox-created", () => {
      dump("[DevToolsProvider] received toolbox-created event\n");
    })

    this.listeners = [];
  },

  /**
   * Check if the devtools addon is currently available.
   */
  isInstalled: function () {
    return _gDevTools && _gDevToolsBrowser;
  },
};

gDevToolsMethods.forEach(name => {
  this.gDevTools[name] = (...args) => {
    if (this.DevToolsProvider.isAvailable()) {
      return _gDevTools[name].apply(_gDevTools, args);
    }
  };
});

gDevToolsShim = this.gDevTools;

/**
 * Exposing a separate gDevToolsBrowser really depends on how we implement adding the
 * context menu item for "Inspect Node".
 */
let gDevToolsBrowserMethods = [
  "inspectNode"
];

this.gDevToolsBrowser = {};

gDevToolsBrowserMethods.forEach(name => {
  this.gDevToolsBrowser[name] = (...args) => {
    return browser[name].apply(browser, args);
  };
});