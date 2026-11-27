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
vm.runInNewContext(src, { window: windowStub, Promise, Error, JSON, Object, String, console });

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

  // handshake: initialize only stores info; ONLYOFFICE init(data) runs once, on "init", with the data
  const order = [];
  Asc.plugin.init = function (data) { order.push("init:" + data); };
  Asc.plugin.onPluginInit = function () { order.push("plugin_init"); };
  deliver({ type: "initialize", guid: "g" });
  assert.strictEqual(Asc.plugin.info.guid, "g");
  deliver({ type: "plugin_init", data: {} });
  deliver({ type: "init", data: "selected" });
  assert.deepStrictEqual(order, ["plugin_init", "init:selected"]);

  // ONLYOFFICE callback style for executeMethod (callback gets the result; errors give undefined)
  let cbResult = "unset";
  Asc.plugin.executeMethod("GetSelectedText", [], function (r) { cbResult = r; });
  deliver({ type: "method_result", id: sent[sent.length - 1].id, result: "xyz" });
  await new Promise((r) => setTimeout(r, 0));
  assert.strictEqual(cbResult, "xyz");
  Asc.plugin.executeMethod("Nope", [], function (r) { cbResult = r; });
  deliver({ type: "method_result", id: sent[sent.length - 1].id, error: "unknown" });
  await new Promise((r) => setTimeout(r, 0));
  assert.strictEqual(cbResult, undefined);

  // callCommand(func, isClose, isCalc, callback) carries Asc.scope and closes when asked
  Asc.scope.n = 42;
  let cmdResult = null;
  Asc.plugin.callCommand(function () { return Asc.scope.n; }, true, true, function (r) { cmdResult = r; });
  const req4 = sent[sent.length - 1];
  assert.strictEqual(req4.type, "callCommand");
  assert.deepStrictEqual(JSON.parse(req4.scope), { n: 42 });
  deliver({ type: "command_result", id: req4.id, result: 42 });
  await new Promise((r) => setTimeout(r, 0));
  assert.strictEqual(cmdResult, 42);
  assert.strictEqual(sent[sent.length - 1].type, "close", "isClose sends close after the command");

  // AddContextMenuItem maps to the host cache (nested items flattened); click reaches the handler
  Asc.plugin.executeMethod("AddContextMenuItem", [{ items: [{ id: "a", text: "A" }, { text: "Sub", items: [{ id: "b", text: "B" }] }] }]);
  const menu = sent[sent.length - 1];
  assert.strictEqual(menu.type, "contextMenu");
  assert.deepStrictEqual(menu.items, [{ id: "a", text: "A" }, { id: "b", text: "B" }]);
  let clicked = null;
  Asc.plugin.attachContextMenuClickEvent("b", function (d) { clicked = d.id; });
  deliver({ type: "event", name: "onContextMenuClick", data: { id: "b" } });
  assert.strictEqual(clicked, "b");

  // event_<name> handlers (ONLYOFFICE style) also receive events; onContextMenuShow fires after init
  let shown = 0;
  Asc.plugin.event_onContextMenuShow = function () { shown++; };
  deliver({ type: "init", data: "" });
  assert.strictEqual(shown, 1);

  console.log("test_runtime: PASS");
})().catch(function (e) {
  console.error("test_runtime: FAIL", e && e.message);
  process.exit(1);
});
