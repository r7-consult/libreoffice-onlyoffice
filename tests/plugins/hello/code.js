// Hello fixture, written the way an ONLYOFFICE plugin is written (init(data), callbacks, Asc.scope,
// AddContextMenuItem). Asc.plugin is provided by runtime.js.
// In test mode (info.testMode === true) it runs the checks of stages 4-6 and reports each result
// with executeMethod("LogTest", [name, json]). The harness reads the log.
(function () {
  "use strict";

  function report(name, value) {
    var out = document.getElementById("out");
    if (out) out.textContent += name + ": " + JSON.stringify(value) + "\n";
    Asc.plugin.executeMethod("LogTest", [name, JSON.stringify(value)]);
  }

  Asc.plugin.event_onContextMenuShow = function () {
    Asc.plugin.executeMethod("AddContextMenuItem", [{ items: [{ id: "hello-1", text: "Hello from plugin" }] }]);
  };
  Asc.plugin.attachContextMenuClickEvent("hello-1", function () {
    report("menu_click", "hello-1");
  });

  Asc.plugin.init = function () {
    var info = Asc.plugin.info || {};
    if (!info.testMode) return;

    // Stage 4: method round trip (Writer selection), ONLYOFFICE callback style.
    Asc.plugin.executeMethod("GetSelectedText", [], function (text) {
      report("gst", text === undefined ? "<error>" : text);
    });

    // Stage 5a: write 100 x 100 cells in one call; size comes through Asc.scope.
    Asc.scope.size = 100;
    var started = Date.now();
    Asc.plugin.callCommand(function () {
      var n = Asc.scope.size;
      var rows = [];
      for (var r = 0; r < n; r++) {
        var row = [];
        for (var c = 0; c < n; c++) row.push(r * n + c);
        rows.push(row);
      }
      Api.GetActiveSheet().GetRange("A1:CV100").SetValue(rows);
      return "ok";
    }, false, true).then(function (res) {
      report("cmd_fill", { result: res, ms: Date.now() - started });
    }, function (err) {
      report("cmd_fill_error", String(err && err.message));
    });

    // Stage 5b: infinite loop must be stopped by the watchdog (Promise form to see the error text).
    Asc.plugin.callCommand(function () {
      while (true) {}
    }).then(function () {
      report("cmd_timeout", "unexpected success");
    }, function (err) {
      report("cmd_timeout", String(err && err.message));
    });
  };
})();
