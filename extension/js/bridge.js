/*
 * Generic UNO bridge, JavaScript side (PLAN_FULL_API.md section 2, F3).
 *
 * createBridge(native, policy, rootHandle) returns a frozen object. `native` is a function provided by the host:
 *     native(op, argsJson) -> JSON text   (throws Error on failure)
 * with op one of "get", "set", "call", "release". The host never gives plugin code a reference to `native`:
 * it stays in this closure.
 *
 * policy:
 *   allow:    { Interface: { method: "read" | "edit" } }   the generated allowlist (method level)
 *   props:    { property: "read" | "edit" } optional property allowlist; if absent, any identifier-shaped
 *             property is allowed under the canRead / canEdit flags
 *   canRead:  boolean   grants read methods and property reads (document.read)
 *   canEdit:  boolean   grants edit methods and property writes (document.edit)
 *   maxArgBytes: number  limit on the JSON of one call's arguments (default 4 MB)
 *
 * Native results are JSON. An object result that the host wants to keep alive is sent as {"$h": n}; callHandle
 * returns n as a live handle, call returns it as a plain value.
 *
 * Handles are numbers that this bridge registered. A number that was never returned by native, or that was already
 * released, is refused before native is called. dispose() releases every live handle and disables the bridge.
 *
 * Written from the public description of the Office JS API. Not a copy of sdkjs code.
 */
(function (global) {
  "use strict";

  var DEFAULT_MAX_ARG_BYTES = 4 * 1024 * 1024;

  // Fixed denylist (PLAN_FULL_API.md section 2.2). A policy can never remove an entry from this list.
  var DENIED_INTERFACES = [
    "XStorable", "XComponentLoader", "XDispatchHelper", "XDispatchProvider", "XSimpleFileAccess",
    "XConfigurationProvider", "XSingleServiceFactory", "XMultiServiceFactory", "XScriptProvider",
    "XMacroExpander", "XScriptLibraryContainer"
  ];
  var DENIED_INTERFACE_PATTERN = /Script|Macro|Basic|Python|Dispatch|FileAccess|Storable|ServiceManager/;
  var DENIED_METHODS = [
    "storeToURL", "storeAsURL", "loadComponentFromURL", "dispatch", "executeDispatch", "createInstance",
    "createInstanceWithContext", "getServiceManager", "openURL", "system", "exec"
  ];
  var PROPERTY_NAME = /^[A-Za-z][A-Za-z0-9]{0,63}$/;

  function fail(message) {
    throw new Error("bridge: " + message);
  }

  var hasOwn = Object.prototype.hasOwnProperty;

  // Own-property read: a polluted Object.prototype must not add policy entries (the C++ side is the enforcer, this
  // keeps the JS layer from granting more than the policy object holds).
  function own(obj, key) {
    return obj !== null && typeof obj === "object" && hasOwn.call(obj, key) ? obj[key] : undefined;
  }

  // Same rule as lastComponent() in src/bridge_native.cxx: the denylist is matched on the simple interface name, so
  // "com.sun.star.frame.XStorable" and "XStorable" are both denied.
  function isDenied(iface, method) {
    var dot = iface.lastIndexOf(".");
    var simple = dot === -1 ? iface : iface.substring(dot + 1);
    if (DENIED_INTERFACES.indexOf(simple) !== -1 || DENIED_INTERFACE_PATTERN.test(simple)) return true;
    return DENIED_METHODS.indexOf(method) !== -1;
  }

  function requireHandle(live, handle) {
    if (typeof handle !== "number" || !live.has(handle)) fail("invalid or released handle");
  }

  // Length of a JSON text in UTF-8 bytes (the host measures bytes, String.length counts UTF-16 units).
  function utf8Length(text) {
    var n = 0;
    for (var i = 0; i < text.length; i++) {
      var c = text.charCodeAt(i);
      if (c < 0x80) n += 1;
      else if (c < 0x800) n += 2;
      else if (c >= 0xd800 && c <= 0xdbff && i + 1 < text.length &&
               text.charCodeAt(i + 1) >= 0xdc00 && text.charCodeAt(i + 1) <= 0xdfff) { n += 4; i++; }
      else n += 3;
    }
    return n;
  }

  function checkArgs(args, maxBytes) {
    var json = JSON.stringify(args);
    if (json === undefined) fail("arguments are not JSON");
    if (json.length > maxBytes || (json.length * 3 > maxBytes && utf8Length(json) > maxBytes)) {
      fail("arguments exceed " + maxBytes + " bytes");
    }
    return json;
  }

  function createBridge(native, policy, rootHandle) {
    if (typeof native !== "function") fail("native primitive missing");
    policy = policy || {};
    var allow = own(policy, "allow") || {};
    var props = own(policy, "props") || null;
    var canRead = own(policy, "canRead") === true;
    var canEdit = own(policy, "canEdit") === true;
    var maxBytes = own(policy, "maxArgBytes");
    if (typeof maxBytes !== "number" || !(maxBytes > 0) || maxBytes === Infinity) maxBytes = DEFAULT_MAX_ARG_BYTES;
    var live = new Set();
    var disposed = false;

    function ensureOpen() {
      if (disposed) fail("bridge disposed");
    }

    function run(op, args) {
      var text = native(op, checkArgs(args, maxBytes));
      if (text === undefined || text === null) return null;
      if (typeof text !== "string") fail("native returned a non-string result");
      try {
        return JSON.parse(text);
      } catch (e) {
        fail("native returned invalid JSON");
      }
      return undefined;
    }

    // The host registers a handle for every object in a result, also inside arrays and for property reads. A handle
    // the bridge did not record would be refused later, so every {"$h": n} in a result is recorded here.
    function track(value, depth) {
      if (Array.isArray(value)) {
        if (depth < 32) {
          for (var i = 0; i < value.length; i++) track(value[i], depth + 1);
        }
        return;
      }
      if (value && typeof value === "object" && typeof value.$h === "number") live.add(value.$h);
    }

    // props is a flat map { propertyName: "read" | "edit" } when given. "edit" also allows reads.
    function propertyRule(prop, wanted) {
      if (!PROPERTY_NAME.test(prop)) fail("property name not allowed: " + prop);
      if (props) {
        var rule = own(props, prop);
        if (!rule) fail("property not allowed: " + prop);
        if (wanted === "edit" && rule !== "edit") fail("property is read only: " + prop);
      }
      if (wanted === "read" && !canRead) fail("document.read permission required");
      if (wanted === "edit" && !canEdit) fail("document.edit permission required");
    }

    var bridge = {
      get: function (handle, prop) {
        ensureOpen();
        requireHandle(live, handle);
        var name = String(prop); // converted once: the checked name is the name that is sent
        propertyRule(name, "read");
        var result = run("get", [handle, name]);
        track(result, 0);
        return result;
      },

      set: function (handle, prop, value) {
        ensureOpen();
        requireHandle(live, handle);
        var name = String(prop);
        propertyRule(name, "edit");
        run("set", [handle, name, value]);
        return undefined;
      },

      call: function (handle, iface, method, args) {
        ensureOpen();
        if (handle !== null) requireHandle(live, handle);
        iface = String(iface);
        method = String(method);
        if (isDenied(iface, method)) fail("denied: " + iface + "." + method);
        var kind = own(own(allow, iface), method);
        if (kind !== "read" && kind !== "edit") fail("not in allowlist: " + iface + "." + method);
        if (kind === "edit" && !canEdit) fail("document.edit permission required");
        if (kind === "read" && !canRead) fail("document.read permission required");
        if (args !== undefined && !Array.isArray(args)) fail("args must be an array");
        var result = run("call", [handle, iface, method, args || []]);
        track(result, 0);
        return result;
      },

      callHandle: function (handle, iface, method, args) {
        var result = bridge.call(handle, iface, method, args);
        if (result && typeof result === "object" && typeof result.$h === "number") return result.$h;
        fail("call did not return an object: " + iface + "." + method);
        return undefined;
      },

      release: function (handle) {
        ensureOpen();
        requireHandle(live, handle);
        run("release", [handle]);
        live.delete(handle);
      },

      liveCount: function () {
        return live.size;
      },

      dispose: function () {
        if (disposed) return;
        var handles = Array.from(live);
        disposed = true;
        live.clear();
        for (var i = 0; i < handles.length; i++) {
          try {
            native("release", JSON.stringify([handles[i]]));
          } catch (e) {
            // Keep releasing the rest; the host logs the failure on its side.
          }
        }
      }
    };

    if (typeof rootHandle === "number") live.add(rootHandle);
    return Object.freeze(bridge);
  }

  global.createBridge = createBridge;
})(this);
