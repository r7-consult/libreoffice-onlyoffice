// BEGIN {{CLASS}}.{{NAME}}
{{CLASS}}.prototype.{{NAME}} = function (value) {
  this._bridge.set(this._handle, "{{PROP}}", value);
  return this;
};
// END {{CLASS}}.{{NAME}}
