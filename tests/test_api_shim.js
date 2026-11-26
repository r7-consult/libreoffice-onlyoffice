// Node check of extension/js/api_shim.js with a fake __ph_call (stage 5, JS part).
// Run: node tests/test_api_shim.js   (exit 0 = pass)
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const calls = [];
const store = { "Sheet1": { "A1:B2": [[1, 2], [3, 4]] } };
const paragraphs = ["first", "second"];

const fakeCall = function (name, argsJson) {
  const args = JSON.parse(argsJson);
  calls.push([name, args]);
  switch (name) {
    case "calc.activeSheetName": return JSON.stringify("Sheet1");
    case "calc.getRange": return JSON.stringify(store[args[0]][args[1]]);
    case "calc.setRange": store[args[0]][args[1]] = args[2]; return "true";
    case "writer.paragraphCount": return JSON.stringify(paragraphs.length);
    case "writer.paragraphText": return JSON.stringify(paragraphs[args[0]]);
    case "writer.appendText": paragraphs.push(args[0]); return "true";
    default: throw new Error("unknown " + name);
  }
};

const ctx = { __ph_call: fakeCall, JSON, Array, String, Error };
vm.createContext(ctx);
const src = fs.readFileSync(path.join(__dirname, "..", "extension", "js", "api_shim.js"), "utf8");
vm.runInContext(src, ctx);

const Api = ctx.Api;
assert.ok(Api, "Api must be defined");
assert.strictEqual(typeof Api.GetActiveSheet, "function");
assert.strictEqual(typeof Api.GetDocument, "function");

// Calc
const ws = Api.GetActiveSheet();
assert.strictEqual(ws.GetName(), "Sheet1");
const range = ws.GetRange("A1:B2");
assert.deepStrictEqual(range.GetValue(), [[1, 2], [3, 4]]);
range.SetValue([[9, 8], [7, 6]]);
assert.deepStrictEqual(store.Sheet1["A1:B2"], [[9, 8], [7, 6]]);
ws.GetRange("C1").SetValue("x");
assert.deepStrictEqual(store.Sheet1["C1"], [["x"]]);

// Writer
const doc = Api.GetDocument();
assert.strictEqual(doc.GetElementsCount(), 2);
assert.strictEqual(doc.GetElement(1).GetText(), "second");
doc.GetElement(0).AddText(" more");
assert.strictEqual(doc.GetElementsCount(), 3);

// Every method name used above must be one of the native operations
const names = new Set(calls.map(function (c) { return c[0]; }));
for (const n of names) {
  assert.ok(/^(calc|writer)\./.test(n), "unexpected native op " + n);
}

console.log("test_api_shim: PASS");
