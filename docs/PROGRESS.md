# Progress: prototype P-1 (option A+)

Plan: `C:\Oracle\lo\PLAN_PROTOTYPE_P1_HAIKU.md`. Statuses: OK, PARTIAL, BLOCKED, NOT STARTED.

| Stage | Status | Date | Notes |
|---|---|---|---|
| 0 Environment | PARTIAL | 2026-11-26 | Tools fetched (CMake 4.5.1, Ninja 1.13.2, QuickJS-ng 0.18.0, WebView2 SDK 1.0.4370.26). LibreOffice and its SDK NOT available (download host unreachable). MSVC not installed; no admin rights. See BLOCKED.md. |
| 1 Skeleton and empty panel | WRITTEN, NOT BUILT | 2026-11-26 | Source, extension files, scripts and test written. Not compiled: no cl.exe, no SDK. Not run: no LibreOffice. |
| 2 WebView2 in panel | WRITTEN, NOT BUILT | 2026-11-26 | `src/webhost.hxx`, `src/webhost_win.*` (WebView2 1.0.4370.26, static loader, hand-written COM callbacks), `extension/content/probe.html`. Wired into PluginPanel via XSystemDependentWindowPeer. Same blockers. The host logs every 5th page message as `webhost|tick|<json>`. |
| 3 Lifecycle without restart | NOT STARTED | | Needs stages 1 and 2 running. |
| 4 Protocol adapter and runtime | PARTIAL | 2026-11-26 | JS runtime `extension/content/runtime.js` passes `tests/test_runtime.js` under node (protocol only: request/response, errors, event isolation, handshake order). C++ ProtocolAdapter and the config.json reader NOT written. Nothing run in LibreOffice. |
| 5 CommandEngine, QuickJS, Api | NOT STARTED | | |
| 6 Context menu | NOT STARTED | | |
| 7 Renderer failure isolation | NOT STARTED | | |
| 8 Two LibreOffice versions | NOT STARTED | | Blocked by LibreOffice download. |
| 10 Linux and macOS preparation | NOT STARTED | | |
| 11 RESULTS.md | NOT STARTED | | Criteria may only be NOT VERIFIED until stages 1–8 run. |
