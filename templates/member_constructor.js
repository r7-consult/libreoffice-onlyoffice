// BEGIN {{CLASS}}.{{NAME}}
{{CLASS}}.prototype.{{NAME}} = function (arg) {
  var handle = this._bridge.call(this._handle, "{{IFACE}}", "{{METHOD}}", [arg]);
  return new {{RETURNS}}(this._bridge, handle);
};
// END {{CLASS}}.{{NAME}}
