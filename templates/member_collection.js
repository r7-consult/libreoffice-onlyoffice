// BEGIN {{CLASS}}.{{NAME}}
{{CLASS}}.prototype.{{NAME}} = function () {
  // Bulk enumeration: one bridge call returns the whole list.
  return this._bridge.call(this._handle, "{{IFACE}}", "{{METHOD}}", []);
};
// END {{CLASS}}.{{NAME}}
