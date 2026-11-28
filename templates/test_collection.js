// Generated test for {{CLASS}}.{{NAME}} (collection). Do not change the assertions; the expectation is in tests/expected/{{ID}}.json.
var TEST = {
  id: "{{ID}}",
  setup: function (api) {
    return { target: {{SETUP}} };
  },
  run: function (ctx) {
    var list = ctx.target.{{NAME}}();
    return { value: { count: list.length } };
  }
};
