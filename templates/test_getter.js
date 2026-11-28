// Generated test for {{CLASS}}.{{NAME}} (getter). Do not change the assertions; the expectation is in tests/expected/{{ID}}.json.
var TEST = {
  id: "{{ID}}",
  setup: function (api) {
    return { target: {{SETUP}} };
  },
  run: function (ctx) {
    return { value: ctx.target.{{NAME}}() };
  }
};
