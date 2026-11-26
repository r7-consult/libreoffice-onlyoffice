# Progress: prototype P-1 (option A+)

Plan: `C:\Oracle\lo\PLAN_PROTOTYPE_P1_HAIKU.md`.

Status words:
- **DRAFT (not compiled, not run)**: code is written, nothing has been compiled or executed. Expect compile errors and API mismatches on first build.
- **NODE-TESTED**: pure JavaScript, tested with node here.
- **NOT STARTED**: no code.

Build machine checklist (first run, in this order): `scripts\env.ps1`, `scripts\gen_uno_headers.ps1`, `scripts\build.ps1`, `scripts\pack.ps1`, `scripts\install.ps1`, `scripts\run_all.ps1`.

| Stage | Status | Notes |
|---|---|---|
| 0 Environment | PARTIAL | Tools fetched: CMake 4.5.1, Ninja 1.13.2, QuickJS-ng 0.18.0, WebView2 SDK 1.0.4370.26. LibreOffice and its SDK NOT available here. MSVC not installed, no admin rights. See `BLOCKED.md`. |
| 1 Skeleton and empty panel | DRAFT | `src/component.cxx`, `panel_factory.*`, `plugin_panel.*`, `extension/*.xcu`. Sidebar/Factories XML modelled on `officecfg/.../Sidebar.xcu` and `odk/examples/cpp/custompanel`; not tested in a running LibreOffice. |
| 2 WebView2 in panel | DRAFT | `webhost.hxx`, `webhost_win.*` (WebView2 1.0.4370.26, static loader, hand-written COM callbacks). Logs every 5th page message as `webhost|tick`. Not logged: `navigated|title=PROBE` (not implemented). |
| 3 Lifecycle without restart | DRAFT | `host_service.*`: sessions outlive the panel; `acquire` reattaches, `release` hides. `test_stage3.py`. |
| 4 Protocol adapter and runtime | NODE-TESTED (JS) + DRAFT (C++) | `extension/content/runtime.js` passes `tests/test_runtime.js`. C++: `protocol_adapter.*`, `plugin_config.*`, `json_mini.*`, `document_bridge.*` (Writer: GetSelectedText, PasteText, LogTest). `test_stage4.py`. Test fixture `tests/plugins/hello`. |
| 5 CommandEngine, QuickJS, Api | NODE-TESTED (shim) + DRAFT (C++) | `command_engine.*` (QuickJS-ng, watchdog, undo rollback on failure), `api_bridge.cxx` (native operations), `extension/js/api_shim.js` passes `tests/test_api_shim.js` (ten public Api methods). `test_stage5.py` (fill time, Undo, TimeoutError). |
| 6 Context menu | DRAFT | `context_menu.*`: interceptor reads a cache set by the page; attaches to the document active at panel creation only. Click handling NOT implemented. Timing needs a manual right-click (`test_stage6.py` checks attach and items only). |
| 7 Renderer failure isolation | DRAFT | `webhost_win.cxx` ProcessFailed handler -> `HostService::markFailed`; restart happens on the next panel attach. `test_stage7.py` kills WebView2 processes of the plugin data folder. Panel does not show a "stopped" message yet. |
| 8 Two LibreOffice versions | DRAFT (scripts only) | `scripts\install.ps1`, `run_all.ps1` take the profile. LibreOffice not available here, so nothing ran. |
| 10 Linux and macOS preparation | DRAFT | `webhost_stub.cxx` (no browser yet), `docs/TEST_MATRIX_LINUX_MACOS.md`. CEF and WKWebView implementations NOT written. |
| 11 RESULTS.md | DRAFT | `docs/RESULTS.md`: all criteria NOT VERIFIED. |

## Known gaps in the draft
- Nothing compiled. `CMakeLists.txt` QuickJS source list must be checked against `third_party/quickjs-ng/CMakeLists.txt` (0.18.0).
- `cppumaker` arguments in `scripts/gen_uno_headers.ps1` are assumed (see `BLOCKED.md`).
- `ContextMenuInterceptorAction_EXECUTE_MODIFIED`, `ActionTrigger` property names and `XUndoManager::getAllUndoActionTitles` are assumed from the IDL; confirm in `offapi`.
- `rtl_getGlobalProcessId` and `XSystemDependentWindowPeer::getWindowHandle` usage: confirm the byte layout on Windows.
- Callbacks hold a raw `this` (WebHostWin). Destroying a host while WebView2 still has pending completions is unsafe; fix before stage 3 gate.
- Undo limit: `undoActionCount` stops increasing at the undo limit (default 100), so rollback could be skipped in very long sessions.
- Plugin install: `scripts\install.ps1` installs the OXT only; plugin packages are copied to `%LOCALAPPDATA%\PluginHost\plugins` (or `PLUGINHOST_PLUGINS_DIR`) by hand. No installer yet.
- Not implemented: click on context menu items, multi-document context menu attachment, panel "stopped" view, UNO service registration of HostService (it is a C++ singleton in the prototype).
