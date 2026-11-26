// Node-based check of extension/content/runtime.js (stage 4, protocol part).
// Run: node tests/test_runtime.js   (exit code 0 = pass)
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const sent = [];
let listener = null;
const windowStub = {
  chrome: {
    webview: {
      postMessage: function (s) { sent.push(JSON.parse(s)); },
      addEventListener: function (name, fn) { if (name === "message") listener = fn; },
    },
  },
};

const src = fs.readFileSync(path.join(__dirname, "..", "extension", "content", "runtime.js"), "utf8");
vm.runInNewContext(src, { window: windowStub, Promise, Error, JSON, Object, console });

const Asc = windowStub.Asc;
assert.ok(Asc && Asc.plugin, "Asc.plugin must exist");
assert.strictEqual(sent[0].type, "ready", "runtime announces ready first");

function deliver(obj) { listener({ data: JSON.stringify(obj) }); }

(async function () {
  // executeMethod round trip
  const p1 = Asc.plugin.executeMethod("GetSelectedText", []);
  const req1 = sent[sent.length - 1];
  assert.strictEqual(req1.type, "executeMethod");
  assert.strictEqual(req1.name, "GetSelectedText");
  deliver({ type: "method_result", id: req1.id, result: "abc" });
  assert.strictEqual(await p1, "abc");

  // error path rejects
  const p2 = Asc.plugin.executeMethod("PasteText", ["x"]);
  const req2 = sent[sent.length - 1];
  deliver({ type: "method_result", id: req2.id, error: "denied" });
  await assert.rejects(p2, /denied/);

  // callCommand sends source text
  Asc.plugin.callCommand(function () { return 1; }, false);
  const req3 = sent[sent.length - 1];
  assert.strictEqual(req3.type, "callCommand");
  assert.ok(req3.source.indexOf("return 1") >= 0);

  // events reach listeners; a throwing listener does not stop the next one
  let hits = 0;
  Asc.plugin.attachEvent("onX", function () { throw new Error("boom"); });
  Asc.plugin.attachEvent("onX", function () { hits++; });
  deliver({ type: "event", name: "onX", data: {} });
  assert.strictEqual(hits, 1);

  // handshake order: initialize -> plugin_init -> init
  const order = [];
  Asc.plugin.init = function () { order.push("init-plugin"); };
  Asc.plugin.onPluginInit = function () { order.push("plugin_init"); };
  Asc.plugin.onInit = function () { order.push("init"); };
  deliver({ type: "initialize", guid: "g" });
  deliver({ type: "plugin_init", data: {} });
  deliver({ type: "init", data: {} });
  assert.deepStrictEqual(order, ["init-plugin", "plugin_init", "init"]);

  console.log("test_runtime: PASS");
})().catch(function (e) {
  console.error("test_runtime: FAIL", e && e.message);
  process.exit(1);
});
