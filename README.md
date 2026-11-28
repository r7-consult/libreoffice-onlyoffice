# Plugin host for LibreOffice

This is a LibreOffice extension that shows ONLYOFFICE-style web plugins in a sidebar panel next to the document.
A JavaScript `Api` shim (run by a QuickJS-ng command engine) maps the plugin calls onto UNO calls through a generic
bridge. The work is at the source and document stage: a Windows (WebView2) prototype exists, and the code is being
prepared for a Linux host.

## Status

Nothing has been compiled or run yet. The C++ sources, the CMake files and the build and test scripts are written
but not verified: there is no LibreOffice SDK build, no run inside LibreOffice, and no UNO call has been executed.
The JavaScript parts (the runtime, the `Api` shim and the bridge boot code) are tested with Node.js only. The
Linux host (WebKitGTK 4.1 web view) is written but not built, and the embedding under the LibreOffice gtk3 VCL
plugin is unverified. The goal of the current phase is a hand-off to a Linux host, which compiles the full code,
runs the tests and records the results in `docs/RESULTS.md`.

## Key documents

- `docs/DECISION_LOG.md`: decisions taken by the user, and where each one is applied
- `docs/FULL_API_STATUS.md`: status of the full API work that needs no binary
- `docs/TEST_RUNNER.md`: the headless test runner
- `docs/PLAN_FULL_API.md`: the full API plan
- `docs/PLAN_NEXT.md`: next steps
- `docs/adr/`: architecture decision records (ADR-002 permission policy, ADR-003 mapped-limited rules)
- `docs/LINUX_BUILD.md`: Linux build steps (being written)

## Licence

AGPL-3.0. See `LICENSE` and `docs/LICENSING.md`.

## Running the Node tests

The tests need Node.js only, except `tests/test_allowlist.js`, which also needs the LibreOffice source tree (`LO_SRC`,
default `C:\Oracle\lo`) to check the allowlist against the IDL files:

```
node tests/test_bridge.js
node tests/test_allowlist.js
node tests/test_bridge_boot.js
node tests/test_runtime.js
node tests/test_api_shim.js
```

Passing these tests does not show that the C++ code works.
