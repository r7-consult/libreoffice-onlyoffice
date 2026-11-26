/*
 * Api shim (stage 5). Defines the global `Api` used by callCommand() sources.
 * Ten public methods map onto the native operations in src/api_bridge.cxx through
 * __ph_call(name, argsJson), which returns a JSON text.
 *
 * Public surface (prototype):
 *   Api.GetActiveSheet() -> ApiWorksheet
 *   Api.GetDocument()    -> ApiDocument
 *   ApiWorksheet.GetName()
 *   ApiWorksheet.GetRange(address) -> ApiRange
 *   ApiRange.SetValue(value | 2D array)
 *   ApiRange.GetValue()
 *   ApiDocument.GetElementsCount()
 *   ApiDocument.GetElement(index) -> ApiParagraph
 *   ApiParagraph.AddText(text)
 *   ApiParagraph.GetText()
 *
 * Written from the public description of the Office JS API. Not a copy of sdkjs code.
 */
(function (global) {
  "use strict";

  function call(name, args) {
    var json = JSON.stringify(args || []);
    var text = global.__ph_call(name, json);
    return text === undefined ? null : JSON.parse(text);
  }

  function ApiRange(sheetName, address) {
    this._sheet = sheetName;
    this._addr = address;
  }
  ApiRange.prototype.SetValue = function (value) {
    var rows = Array.isArray(value) ? value : [[value]];
    call("calc.setRange", [this._sheet, this._addr, rows]);
    return this;
  };
  ApiRange.prototype.GetValue = function () {
    var rows = call("calc.getRange", [this._sheet, this._addr]);
    if (rows.length === 1 && rows[0].length === 1) {
      return rows[0][0];
    }
    return rows;
  };

  function ApiWorksheet(name) {
    this._name = name;
  }
  ApiWorksheet.prototype.GetName = function () {
    return this._name;
  };
  ApiWorksheet.prototype.GetRange = function (address) {
    return new ApiRange(this._name, String(address));
  };

  function ApiParagraph(index) {
    this._index = index;
  }
  ApiParagraph.prototype.GetText = function () {
    return call("writer.paragraphText", [this._index]);
  };
  ApiParagraph.prototype.AddText = function (text) {
    // Prototype: appends at the end of the body. Index-based insertion comes later.
    call("writer.appendText", [String(text)]);
    return this;
  };

  function ApiDocument() {}
  ApiDocument.prototype.GetElementsCount = function () {
    return call("writer.paragraphCount", []);
  };
  ApiDocument.prototype.GetElement = function (index) {
    return new ApiParagraph(index);
  };

  var Api = {
    GetActiveSheet: function () {
      return new ApiWorksheet(call("calc.activeSheetName", []));
    },
    GetDocument: function () {
      return new ApiDocument();
    }
  };

  global.Api = Api;
  global.ApiRange = ApiRange;
  global.ApiWorksheet = ApiWorksheet;
  global.ApiParagraph = ApiParagraph;
  global.ApiDocument = ApiDocument;
})(typeof globalThis !== "undefined" ? globalThis : this);
