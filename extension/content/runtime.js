/*
 * Plugin Host runtime. Injected into every plugin page before its own scripts.
 * Provides the ONLYOFFICE-compatible Asc.plugin surface on top of the host message protocol.
 *
 * ONLYOFFICE-compatible surface (what existing plugins call):
 *   Asc.plugin.init(data)                                  -- called once with the init data
 *   Asc.plugin.executeMethod(name, params, callback)       -- callback(result); also returns a Promise
 *   Asc.plugin.callCommand(func, isClose, isCalc, callback)-- func runs in the host engine; Asc.scope is copied
 *   Asc.scope                                              -- plain data passed to callCommand
 *   Asc.plugin.attachEvent(name, fn) / Asc.plugin.event_<name> = fn
 *   Asc.plugin.executeMethod("AddContextMenuItem", [{items:[{id, text, items?}]}])
 *   Asc.plugin.attachContextMenuClickEvent(id, fn)
 *   Asc.plugin.executeCommand("close", ...) / Asc.plugin.button(id)
 *
 * Host -> page: initialize, plugin_init, init, method_result, command_result, event
 * Page -> host: ready, executeMethod, callCommand {source, scope}, contextMenu {items}, close, resize
 *
 * Limitation: context menu items are a static cache in the host (the menu must open within 300 ms,
 * no round trip to the page). onContextMenuShow is raised once after init so plugins that add items
 * "on show" fill the cache; per-selection dynamic items are not supported.
 *
 * Plain JavaScript, no dependencies. Written from the public description of the ONLYOFFICE plugin API;
 * no code copied from AGPL-licensed sources.
 */
(function () {
  "use strict";

  var pending = {};
  var nextId = 1;
  var listeners = {};
  var menuClickHandlers = {};

  function send(message) {
    try {
      if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage(JSON.stringify(message));
      }
    } catch (e) {
      /* host unavailable; nothing to do */
    }
  }

  function request(kind, payload, callback) {
    var promise = new Promise(function (resolve, reject) {
      var id = nextId++;
      pending[id] = { resolve: resolve, reject: reject };
      var msg = Object.assign({ type: kind, id: id }, payload);
      send(msg);
    });
    if (typeof callback === "function") {
      // ONLYOFFICE style: callback(result); errors give undefined and must not reject unhandled.
      promise.then(function (r) { callback(r); }, function () { callback(undefined); });
    }
    return promise;
  }

  function fireEvent(name, data) {
    var list = (listeners[name] || []).slice();
    var direct = Asc.plugin["event_" + name];
    if (typeof direct === "function") list.push(direct);
    for (var i = 0; i < list.length; i++) {
      try {
        list[i](data);
      } catch (e) {
        /* one failing listener must not stop the others */
      }
    }
  }

  // Flattens ONLYOFFICE menu items (with nested items) into the host's [{id, text}].
  function flattenMenu(items, out) {
    for (var i = 0; i < (items || []).length; i++) {
      var it = items[i];
      if (!it) continue;
      if (it.items && it.items.length) flattenMenu(it.items, out);
      else if (it.id !== undefined) out.push({ id: String(it.id), text: String(it.text || it.id) });
    }
    return out;
  }

  function dispatch(message) {
    if (!message || typeof message !== "object") {
      return;
    }
    switch (message.type) {
      case "initialize":
        Asc.plugin.info = message;
        break;
      case "plugin_init":
        Asc.plugin.onPluginInit && Asc.plugin.onPluginInit(message.data);
        break;
      case "init":
        Asc.plugin.init && Asc.plugin.init(message.data);
        Asc.plugin.onInit && Asc.plugin.onInit(message.data);
        fireEvent("onContextMenuShow", { type: "Selection" });
        break;
      case "method_result":
      case "command_result": {
        var p = pending[message.id];
        if (!p) {
          return;
        }
        delete pending[message.id];
        if (message.error) {
          p.reject(new Error(message.error));
        } else {
          p.resolve(message.result);
        }
        break;
      }
      case "event":
        if (message.name === "onContextMenuClick" && message.data) {
          var h = menuClickHandlers[message.data.id];
          if (h) {
            try { h(message.data); } catch (e) { /* plugin error */ }
          }
        }
        fireEvent(message.name, message.data);
        break;
      default:
        break;
    }
  }

  var Asc = (window.Asc = window.Asc || {});
  Asc.plugin = Asc.plugin || {};
  Asc.scope = Asc.scope || {};

  Asc.plugin.executeMethod = function (name, params, callback) {
    if (name === "AddContextMenuItem") {
      var arg = (params || [])[0] || {};
      Asc.plugin.setContextMenuItems(flattenMenu(arg.items, []));
      var done = Promise.resolve(true);
      if (typeof callback === "function") done.then(callback);
      return done;
    }
    return request("executeMethod", { name: name, args: params || [] }, callback);
  };

  Asc.plugin.callCommand = function (fn, isClose, isCalc, callback) {
    // The command is sent as source text and evaluated by the host's CommandEngine.
    var source = typeof fn === "function" ? "(" + fn.toString() + ")()" : String(fn);
    var scope;
    try {
      scope = JSON.stringify(Asc.scope || {});
    } catch (e) {
      scope = "{}"; // non-serialisable scope: same as ONLYOFFICE, functions are dropped
    }
    var p = request("callCommand", { source: source, scope: scope, isCalc: isCalc !== false }, callback);
    if (isClose) {
      p.then(function () { Asc.plugin.executeCommand("close", ""); }, function () {});
    }
    return p;
  };

  // Host-level API (also used by AddContextMenuItem). items: [{id, text}].
  Asc.plugin.setContextMenuItems = function (items) {
    send({ type: "contextMenu", items: items || [] });
  };

  Asc.plugin.attachContextMenuClickEvent = function (id, handler) {
    menuClickHandlers[String(id)] = handler;
  };

  Asc.plugin.executeCommand = function (command, data) {
    if (command === "close") send({ type: "close" });
  };

  Asc.plugin.attachEvent = function (name, handler) {
    (listeners[name] = listeners[name] || []).push(handler);
  };

  Asc.plugin.detachEvent = function (name, handler) {
    var list = listeners[name] || [];
    listeners[name] = list.filter(function (h) {
      return h !== handler;
    });
  };

  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener("message", function (ev) {
      try {
        dispatch(typeof ev.data === "string" ? JSON.parse(ev.data) : ev.data);
      } catch (e) {
        /* malformed host message: ignore */
      }
    });
  }

  send({ type: "ready" });
})();
