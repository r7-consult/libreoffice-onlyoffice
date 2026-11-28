// Checks extension/js/bridge_allowlist.json against the LibreOffice IDL files (PLAN_FULL_API section 2, F3).
// Each entry must name a published interface whose IDL file exists in the LibreOffice tree, and that file must
// declare the method with that name. Otherwise the bridge would refuse or misroute the call on the build machine.
// Run: node tests/test_allowlist.js   (LO_SRC overrides C:\Oracle\lo). Exit 0 = pass.
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");

const LO_SRC = process.env.LO_SRC || "C:\\Oracle\\lo";
const ROOTS = [path.join(LO_SRC, "offapi"), path.join(LO_SRC, "udkapi")];

if (!fs.existsSync(ROOTS[0]) && !fs.existsSync(ROOTS[1])) {
  console.log("test_allowlist: SKIPPED, no LibreOffice IDL tree at " + LO_SRC);
  process.exit(0);
}

function findIdl(iface) {
  const rel = path.join(...iface.split(".")) + ".idl";
  for (const root of ROOTS) {
    const full = path.join(root, rel);
    if (fs.existsSync(full)) return full;
  }
  return null;
}

const allow = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "extension", "js", "bridge_allowlist.json"), "utf8"));
const problems = [];
let checked = 0;

for (const iface of Object.keys(allow)) {
  const idl = findIdl(iface);
  if (!idl) {
    problems.push(`no IDL file for ${iface}`);
    continue;
  }
  const text = fs.readFileSync(idl, "utf8");
  const simple = iface.split(".").pop();
  if (!new RegExp("interface\\s+" + simple + "\\b").test(text)) {
    problems.push(`${idl} does not declare interface ${simple}`);
  }
  for (const method of Object.keys(allow[iface])) {
    checked += 1;
    const kind = allow[iface][method];
    if (kind !== "read" && kind !== "edit") problems.push(`${iface}.${method}: kind must be read or edit`);
    const declared = new RegExp("\\b" + method + "\\s*\\(").test(text);
    if (!declared) problems.push(`${iface}.${method}: not declared in ${path.basename(idl)}`);
  }
}

if (problems.length) {
  for (const p of problems) console.log("ERROR " + p);
  console.log("test_allowlist: FAIL");
  process.exit(1);
}
assert.ok(checked > 0, "allowlist must not be empty");
console.log(`test_allowlist: PASS (${checked} methods checked against IDL in ${LO_SRC})`);
