// Generated test for {{CLASS}}.{{NAME}} (constructor). Do not change the assertions; the expectation is in tests/expected/{{ID}}.json.
var TEST = {
  id: "{{ID}}",
  setup: function (api) {
    return { target: {{SETUP}} };
  },
  run: function (ctx) {
    var made = ctx.target.{{NAME}}({{ARG}});
    return { value: { created: made !== null && made !== undefined } };
  }
};
