// Node check of extension/js/bridge.js with a fake native primitive (F3, JS side).
// Run: node tests/test_bridge.js   (exit 0 = pass)
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

// Fake host: a tiny object model. Handles are numbers returned as {"$h": n}.
function makeNative() {
  const objects = new Map();
  const log = [];
  let next = 1;
  objects.set(next, { kind: "root", name: "Doc" });
  function native(op, argsJson) {
    const args = JSON.parse(argsJson);
    log.push(op);
    if (op === "release") {
      if (!objects.has(args[0])) throw new Error("native: no such handle " + args[0]);
      objects.delete(args[0]);
      return "null";
    }
    const obj = args[0] === null ? objects.get(1) : objects.get(args[0]);
    if (op === "get") return JSON.stringify(obj[args[1]] === undefined ? null : obj[args[1]]);
    if (op === "set") { obj[args[1]] = args[2]; return "null"; }
    if (op === "call") {
      const method = args[2];
      if (method === "GetActiveSheet") {
        next += 1;
        objects.set(next, { kind: "sheet", name: "Sheet1" });
        return JSON.stringify({ $h: next });
      }
      if (method === "GetName") return JSON.stringify(obj.name);
      if (method === "SetName") { obj.name = args[3][0]; return "null"; }
      throw new Error("native: unknown method " + method);
    }
    throw new Error("native: unknown op " + op);
  }
  return { native, objects, log };
}

// Load the bridge into its own context, as the host would load a script file.
const ctx = {};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname, "..", "extension", "js", "bridge.js"), "utf8"), ctx);
const createBridge = ctx.createBridge;
assert.strictEqual(typeof createBridge, "function", "bridge.js must define createBridge");

const policy = {
  allow: {
    Document: { GetActiveSheet: "read" },
    Sheet: { GetName: "read", SetName: "edit" }
  },
  canRead: true,
  canEdit: true
};

let passed = 0;
function t(name, fn) {
  fn();
  passed += 1;
  console.log("ok  " + name);
}

t("get and call read through the bridge", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  assert.strictEqual(b.get(1, "name"), "Doc");
  const h = b.callHandle(1, "Document", "GetActiveSheet", []);
  assert.strictEqual(typeof h, "number");
  assert.strictEqual(b.call(h, "Sheet", "GetName", []), "Sheet1");
  assert.strictEqual(b.liveCount(), 2);
});

t("edit method runs when canEdit is true", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  b.call(1, "Sheet", "SetName", ["Renamed"]);
  assert.strictEqual(n.objects.get(1).name, "Renamed");
});

t("read-only policy refuses edit methods and property writes", () => {
  const n = makeNative();
  const b = createBridge(n.native, Object.assign({}, policy, { canEdit: false }), 1);
  assert.throws(() => b.call(1, "Sheet", "SetName", ["x"]), /document.edit permission required/);
  assert.throws(() => b.set(1, "name", "x"), /document.edit permission required/);
  assert.strictEqual(b.get(1, "name"), "Doc");
});

t("no read permission refuses reads", () => {
  const n = makeNative();
  const b = createBridge(n.native, Object.assign({}, policy, { canRead: false, canEdit: false }), 1);
  assert.throws(() => b.get(1, "name"), /document.read permission required/);
  assert.throws(() => b.call(1, "Sheet", "GetName", []), /document.read permission required/);
});

t("method not in the allowlist is refused before native runs", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  const before = n.log.length;
  assert.throws(() => b.call(1, "Sheet", "Delete", []), /not in allowlist: Sheet.Delete/);
  assert.strictEqual(n.log.length, before);
});

t("fixed denylist refuses interfaces and methods even if the policy allows them", () => {
  const n = makeNative();
  const evil = { allow: { XStorable: { storeToURL: "edit" }, Sheet: { dispatch: "edit", GetName: "read" } },
                 canRead: true, canEdit: true };
  const b = createBridge(n.native, evil, 1);
  assert.throws(() => b.call(1, "XStorable", "storeToURL", ["file:///x"]), /denied: XStorable.storeToURL/);
  assert.throws(() => b.call(1, "Sheet", "dispatch", []), /denied: Sheet.dispatch/);
  assert.throws(() => b.call(1, "XMacroExpander", "GetName", []), /denied: XMacroExpander/);
  assert.throws(() => b.call(1, "XDispatchHelper", "GetName", []), /denied/);
});

t("forged or released handles are refused before native runs", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  const before = n.log.length;
  assert.throws(() => b.get(999, "name"), /invalid or released handle/);
  assert.throws(() => b.get("1", "name"), /invalid or released handle/);
  assert.strictEqual(n.log.length, before);
  const h = b.callHandle(1, "Document", "GetActiveSheet", []);
  b.release(h);
  assert.throws(() => b.get(h, "name"), /invalid or released handle/);
});

t("property names must be identifiers; props map limits names and write access", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  assert.throws(() => b.get(1, "__proto__"), /property name not allowed/);
  assert.throws(() => b.get(1, "a.b"), /property name not allowed/);
  const limited = createBridge(n.native, Object.assign({}, policy, { props: { name: "read" } }), 1);
  assert.strictEqual(limited.get(1, "name"), "Doc");
  assert.throws(() => limited.set(1, "name", "x"), /property is read only: name/);
  assert.throws(() => limited.get(1, "kind"), /property not allowed: kind/);
});

t("arguments larger than the limit are refused", () => {
  const n = makeNative();
  const b = createBridge(n.native, Object.assign({}, policy, { maxArgBytes: 50 }), 1);
  assert.throws(() => b.call(1, "Sheet", "SetName", ["x".repeat(100)]), /arguments exceed 50 bytes/);
});

t("args must be an array for calls", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  assert.throws(() => b.call(1, "Sheet", "GetName", "nope"), /args must be an array/);
});

t("dispose releases every live handle once and disables the bridge", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  const h = b.callHandle(1, "Document", "GetActiveSheet", []);
  assert.strictEqual(n.objects.has(h), true);
  b.dispose();
  assert.strictEqual(n.objects.has(h), false);
  assert.strictEqual(n.objects.has(1), false);
  assert.throws(() => b.get(1, "name"), /bridge disposed/);
  b.dispose(); // second call does nothing
});

t("a null receiver in call goes to native as null (the root)", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  assert.strictEqual(b.call(null, "Sheet", "GetName", []), "Doc");
  assert.strictEqual(n.log[n.log.length - 1], "call");
  assert.throws(() => b.get(null, "name"), /invalid or released handle/);
});

t("the native primitive is not reachable from the bridge object", () => {
  const n = makeNative();
  const b = createBridge(n.native, policy, 1);
  assert.strictEqual(b.native, undefined);
  assert.strictEqual(Object.isFrozen(b), true);
  assert.throws(() => { b.get = () => "hijacked"; }, TypeError);
  assert.deepStrictEqual(Object.keys(b).sort(), ["call", "callHandle", "dispose", "get", "liveCount", "release", "set"]);
});

t("createBridge refuses a missing native primitive", () => {
  assert.throws(() => createBridge(undefined, policy, 1), /native primitive missing/);
});

console.log(`test_bridge: PASS (${passed} checks)`);
