// BEGIN executeMethod.{{NAME}}
// Registered by name in the executeMethod table. Permission is checked by the host before this runs.
executeTable["{{NAME}}"] = function (params) {
  return this._bridge.call(null, "{{IFACE}}", "{{METHOD}}", params || []);
};
// END executeMethod.{{NAME}}
