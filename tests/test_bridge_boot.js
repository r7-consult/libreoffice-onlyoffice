// Node check of extension/js/bridge_boot.js (PLAN_FULL_API section 2, F3): the host factory gives plugin code a bridge
// and removes createBridge from the global object. Run: node tests/test_bridge_boot.js (exit 0 = pass).
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const dir = path.join(__dirname, "..", "extension", "js");
const ctx = {};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(dir, "bridge.js"), "utf8"), ctx);
const maker = vm.runInContext(fs.readFileSync(path.join(dir, "bridge_boot.js"), "utf8"), ctx);
assert.strictEqual(typeof maker, "function", "bridge_boot.js must evaluate to a function");

// Fake native primitive: handle 1 is the document root with name "Doc".
const objects = new Map([[1, { name: "Doc" }]]);
const log = [];
function native(op, argsJson) {
  const args = JSON.parse(argsJson);
  log.push(op);
  if (op !== "call") throw new Error("native: unexpected " + op);
  const obj = args[0] === null ? objects.get(1) : objects.get(args[0]);
  if (args[2] === "GetName") return JSON.stringify(obj.name);
  throw new Error("native: unknown method " + args[2]);
}

const factory = maker(native);
const bridge = factory({ canRead: true, canEdit: false, allow: { Sheet: { GetName: "read" } } });

let passed = 0;
function t(name, fn) {
  fn();
  passed += 1;
  console.log("ok  " + name);
}

t("the factory returns a working bridge bound to the native primitive", () => {
  assert.strictEqual(bridge.call(null, "Sheet", "GetName", []), "Doc");
  assert.deepStrictEqual(log, ["call"]);
});

t("createBridge is no longer reachable from plugin code", () => {
  assert.strictEqual(ctx.createBridge, undefined);
  assert.strictEqual(vm.runInContext("typeof createBridge", ctx), "undefined");
});

t("the bridge policy still refuses calls outside the allowlist", () => {
  assert.throws(() => bridge.call(null, "Sheet", "Delete", []), /not in allowlist/);
  assert.strictEqual(log.length, 1);
});

console.log(`test_bridge_boot: PASS (${passed} checks)`);
