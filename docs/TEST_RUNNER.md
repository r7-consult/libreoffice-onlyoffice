# TestRunner service (F5): design and open decision

Status: written, not compiled, not run. Files: `src/test_runner.hxx`, `src/test_runner.cxx`, `tests/run_member.py`.

## What it is

- UNO service `org.example.pluginhost.TestRunner`, registered in `src/component.cxx` and `extension/pluginhost.components`.
- It implements `XInvocation` and `XServiceInfo`, with no IDL type. The only method is `run(source, scopeJson)`, which
  returns the result as JSON text.
- `tests/run_member.py` calls it with `uno.invoke(runner, "run", (source, "{}"))`. That is the same XInvocation path
  that pyuno uses for objects without an IDL type. Chosen over generating an IDL type because it needs no IDL compiler
  step in the build.
- Each `run` goes through `CommandEngine::run` under the plugin id `__test__`, with the same watchdog, undo rollback and
  bridge session as a real command.

## Safety

- The service is created only when the environment variable `PLUGINHOST_TEST_SERVICE` is `1`. Otherwise `Create`
  returns an empty reference, and `createInstanceWithContext` gives `None`. `run_member.py` then exits 2
  ("not registered").
- `__test__` has no entry in the plugin configuration, so `hasPermission` returns false for it. The bridge refuses
  its calls. But the `Api` shim calls `__ph_call` directly, and `__ph_call` does not check permissions (permissions
  are checked in `protocol_adapter.cxx`). So a test command has the full `Api` surface. That is the reason for the
  gate.

## Decision needed before shipping

`PLUGINHOST_TEST_SERVICE` and `PLUGINHOST_API_DIR` both change the host's behaviour from an environment variable.
Anyone who can set them on a user's machine can run arbitrary JavaScript with the full `Api` surface, or replace the
allowlist. Options:

1. Keep both only in test builds (a compile-time switch, for example `PH_TEST_HOOKS=ON` in CMake). Shipped builds never
   read them. Recommended.
2. Keep them in all builds and document them as developer switches. Not recommended.

Resolved 2026-11-28 (see docs/DECISION_LOG.md): both switches exist only in builds with `-DPH_TEST_HOOKS=ON`. Release builds ignore them.

## Not verified

- That pyuno's `uno.invoke` accepts the object (it should, because `PyUNO_invoke` checks `hasMethod`).
- That `createSingleComponentFactory` returns "no instance" cleanly when `Create` returns an empty reference.
- Threading: remote UNO calls from the test process are assumed to run on the LibreOffice main thread, which the
  engine needs. Check on the build machine.
