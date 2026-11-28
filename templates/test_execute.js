// Generated test for executeMethod "{{NAME}}". Do not change the assertions; the expectation is in tests/expected/{{ID}}.json.
var TEST = {
  id: "{{ID}}",
  setup: function (api) {
    return {};
  },
  run: function (ctx) {
    var result = null;
    Asc.plugin.executeMethod("{{NAME}}", [], function (r) { result = r; });
    return { value: { called: true, result: result } };
  }
};
