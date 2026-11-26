// Hello fixture. Runs inside the plugin page; Asc.plugin is provided by runtime.js.
// In test mode (initialize.testMode === true) it runs the checks of stages 4-6 and
// reports each result with executeMethod("LogTest", [name, json]). The harness reads the log.
(function () {
  "use strict";

  function report(name, value) {
    var out = document.getElementById("out");
    if (out) out.textContent += name + ": " + JSON.stringify(value) + "\n";
    Asc.plugin.executeMethod("LogTest", [name, JSON.stringify(value)]).catch(function () {});
  }

  Asc.plugin.init = function () {
    Asc.plugin.setContextMenuItems([{ id: "hello-1", text: "Hello from plugin" }]);
  };

  Asc.plugin.onPluginInit = function () {};

  Asc.plugin.onInit = function () {
    var info = Asc.plugin.info || {};
    if (!info.testMode) return;

    // Stage 4: method round trip (Writer selection).
    Asc.plugin.executeMethod("GetSelectedText", []).then(function (text) {
      report("gst", text);
    }, function (err) {
      report("gst_error", String(err && err.message));
    });

    // Stage 5a: write 100 x 100 cells in one call; time it on the page side.
    var started = Date.now();
    Asc.plugin.callCommand(function () {
      var rows = [];
      for (var r = 0; r < 100; r++) {
        var row = [];
        for (var c = 0; c < 100; c++) row.push(r * 100 + c);
        rows.push(row);
      }
      Api.GetActiveSheet().GetRange("A1:CV100").SetValue(rows);
      return "ok";
    }).then(function (res) {
      report("cmd_fill", { result: res, ms: Date.now() - started });
    }, function (err) {
      report("cmd_fill_error", String(err && err.message));
    });

    // Stage 5b: infinite loop must be stopped by the watchdog.
    Asc.plugin.callCommand(function () {
      while (true) {}
    }).then(function () {
      report("cmd_timeout", "unexpected success");
    }, function (err) {
      report("cmd_timeout", String(err && err.message));
    });
  };
})();
