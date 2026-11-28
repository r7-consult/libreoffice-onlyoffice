// Generated test for {{CLASS}}.{{NAME}} (setter). Do not change the assertions; the expectation is in tests/expected/{{ID}}.json.
var TEST = {
  id: "{{ID}}",
  setup: function (api) {
    return { target: {{SETUP}} };
  },
  run: function (ctx) {
    ctx.target.{{NAME}}({{ARG}});
    return { value: {{READBACK}} };
  }
};
