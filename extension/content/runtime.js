/*
 * Plugin Host runtime (stage 4). Injected into every plugin page before its own scripts.
 * Provides a minimal Asc.plugin surface and the message protocol to the host.
 *
 * Host -> page messages (window.chrome.webview "message" event, data is an object):
 *   {type:"initialize", guid, variations, ...}     -- handshake start
 *   {type:"plugin_init", data}                      -- plugin may start
 *   {type:"init", data}                             -- data for the plugin's init()
 *   {type:"method_result", id, result, error}       -- answer to executeMethod
 *   {type:"command_result", id, result, error}      -- answer to callCommand
 *   {type:"event", name, data}                      -- editor event (e.g. onContextMenuClick {id})
 *
 * Page -> host messages (window.chrome.webview.postMessage(string of JSON)):
 *   {type:"ready"}
 *   {type:"executeMethod", id, name, args}
 *   {type:"callCommand", id, source}
 *   {type:"resize", width, height}
 *   {type:"contextMenu", items:[{id, text}]}
 *
 * Plain JavaScript, no dependencies. Written from the public description of
 * the ONLYOFFICE plugin protocol; no code copied from AGPL-licensed sources.
 */
(function () {
  "use strict";

  var pending = {};
  var nextId = 1;
  var listeners = {};

  function send(message) {
    try {
      if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage(JSON.stringify(message));
      }
    } catch (e) {
      /* host unavailable; nothing to do */
    }
  }

  function request(kind, payload) {
    return new Promise(function (resolve, reject) {
      var id = nextId++;
      pending[id] = { resolve: resolve, reject: reject };
      var msg = Object.assign({ type: kind, id: id }, payload);
      send(msg);
    });
  }

  function dispatch(message) {
    if (!message || typeof message !== "object") {
      return;
    }
    switch (message.type) {
      case "initialize":
        Asc.plugin.info = message;
        Asc.plugin.init && Asc.plugin.init();
        break;
      case "plugin_init":
        Asc.plugin.onPluginInit && Asc.plugin.onPluginInit(message.data);
        break;
      case "init":
        Asc.plugin.onInit && Asc.plugin.onInit(message.data);
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
      case "event": {
        var list = listeners[message.name] || [];
        for (var i = 0; i < list.length; i++) {
          try {
            list[i](message.data);
          } catch (e) {
            /* one failing listener must not stop the others */
          }
        }
        break;
      }
      default:
        break;
    }
  }

  var Asc = (window.Asc = window.Asc || {});
  Asc.plugin = Asc.plugin || {};

  Asc.plugin.executeMethod = function (name, args) {
    return request("executeMethod", { name: name, args: args || [] });
  };

  Asc.plugin.callCommand = function (fn, isModal) {
    // The command is sent as source text and evaluated by the host's CommandEngine.
    var source = typeof fn === "function" ? "(" + fn.toString() + ")()" : String(fn);
    return request("callCommand", { source: source, isModal: !!isModal });
  };

  // items: [{id: "...", text: "..."}]. Replaces the plugin's entries in the editor context menu.
  Asc.plugin.setContextMenuItems = function (items) {
    send({ type: "contextMenu", items: items || [] });
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
