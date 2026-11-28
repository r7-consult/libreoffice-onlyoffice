# Plugin host prototype P-1: detailed plan for the remaining work

State on 2026-11-27: every source file is written and checked against the LibreOffice source, `quickjs.h`
and `WebView2.h` (reviews 1–3, see `REVIEW_AND_PLAN.md`). The checks that run without a compiler pass:
`tests/test_runtime.js`, `tests/test_api_shim.js` and `tests/check_xml.py`.
**No C++ has been compiled or run yet**, so none of the 8 pass criteria in `RESULTS.md` is measured.

The CI workflow `build-windows` now runs only on manual dispatch (Actions → build-windows → Run workflow),
so pushing to `plugin-host-draft` no longer starts a remote build.

Phases run in order. Phase 1 is required before any of the others.

---

## Phase 1: First build on a Windows build machine

**Goal:** `pluginhost.oxt` builds and installs into a clean test profile.

### Prerequisites
- Visual Studio 2022 (MSVC x64), CMake ≥ 3.21, Ninja (on PATH or in `third_party\tools`).
- LibreOffice stable x64 and the matching SDK. The installed version is fine, or an admin extract
  (`msiexec /a lo.msi TARGETDIR=C:\lo`).
- QuickJS-ng 0.18.0 sources in `third_party\quickjs`.
- WebView2 SDK 1.0.4370.26 (nupkg unzipped) in `third_party\webview2`.
- Environment: `LO_PROGRAM=<office>\program`, `LO_SDK=<office>\sdk`.

### Steps
1. `pwsh scripts\gen_uno_headers.ps1`: cppumaker writes `build\gen\com\sun\star\...`.
   Check that `build\gen\com\sun\star\ui\XToolPanel.hpp` exists.
2. In an "x64 Native Tools" prompt: `pwsh scripts\build.ps1`.
3. Fix the compile errors. Expected areas, in order of likelihood:
   - Missing includes for UNO types used only through `Any` (`SystemDependentXWindow`, `LayoutSize`).
   - `WeakComponentImplHelper` template arguments in `plugin_panel.hxx` against the SDK's `compbase.hxx`.
   - `makeCallback` template deduction in `webhost_win.cxx` (fixed once, still unproven).
   - QuickJS-ng CMake option names: if `qjs` is not a target, check the upstream `CMakeLists.txt`.
   - Linking: `WebView2LoaderStatic.lib` needs `version.lib` and `advapi32.lib` (both already listed).
4. Pack with `scripts\pack.ps1`. Then unzip `distpluginhost-windows-x64.oxt` and check `META-INF/manifest.xml`, `pluginhost.components`
   and `pluginhost.dll` are at the root.
5. Install: `. scripts\env.ps1 -Profile A; powershell -File scripts\install.ps1 -Profile A` (isolated profile `profile-A`;
   `run_all.ps1` runs the whole sequence).
6. Copy `tests\plugins\hello` to `%APPDATA%\pluginhost\plugins\hello` (see `paths.cxx` for the exact root).

**Done when:** `unopkg list` shows the extension without errors and `pluginhost.log` has a `panel|created` line
once the deck is opened.

**Record:** compiler and SDK versions, each error fixed (one line each), and the build time in `RESULTS.md`.

---

## Phase 2: Stage tests 1–7 (Windows)

Run each stage on Writer and on Calc. Log lines come from `log.cxx`, format `area|event|detail`.

| Stage | What to do | Pass when | Log evidence |
|---|---|---|---|
| 1 Panel | Open the "Plugin Host" deck | Panel area appears, no crash | `panel|created` |
| 2 Render | Same | `hello` page visible inside the panel; resizing the sidebar resizes the page | `webhost|created`, `webhost|ready` |
| 3 Keep-alive | Switch to another deck and back 20× | No reload (page counter keeps increasing); memory stable ±10 MB | `panel|disposed`, `webhost|reattached`, no second `webhost|created` |
| 4 Handshake | Open the deck with `PLUGINHOST_TEST_MODE=1` | `init` reaches the plugin; `GetSelectedText` returns (fixture reports via `LogTest`) | `protocol|handshake` |
| 5 Command | Test mode in Calc: the fixture fills A1:CV100 (10 000 cells) | `cmd_fill` < 1 s; one Ctrl+Z undoes everything; `cmd_timeout` shows the watchdog stopped the infinite loop and the document is unchanged | `command|ok`, `command|timeout` |
| 6 Two versions | Install the same `.oxt` on the previous LO stable | Stages 1–5 pass unchanged | — |
| 7 Crash recovery | Kill `msedgewebview2.exe` (renderer, then browser) in Task Manager | Page reloads, or the browser is recreated, within 3 s; LibreOffice stays up | `webhost|process_failed`, `webhost|renderer_failed` |

Manual checks for this phase:
- Context menu: right-click in Writer/Calc shows the plugin entry within 300 ms (criterion 7); clicking it shows the
  `onContextMenuClick` result in the plugin (`ctxmenu|click`).
- Sidebar docked left and right; floating sidebar; 150 % and 200 % scaling.
- IME input (e.g. Japanese) inside the plugin page.
- NVDA reads the plugin page (WebView2 exposes UIA itself; the panel's `createAccessible` returns empty).
- Two documents open at once: each has its own panel; closing one doesn't break the other.

**Done when:** `RESULTS.md` has a measured value or PASS/FAIL for each criterion on Windows.

---

## Phase 3: Fixes found in phases 1–2

Not knowable in advance. Rules:
- Every fix gets a test, or a log line that proves it.
- If stage 3 or 7 fails because of the parking window, the fallback is to destroy and recreate the browser on
  `release`, keeping the plugin's JS state in the host (slower deck switch, simpler lifetime).
- If the context menu misses the 300 ms target, move the item list to a cached `ActionTriggerContainer`
  that is built once after `init`.

---

## Phase 4: Compatibility with real ONLYOFFICE plugins

1. Pick 3 unmodified plugins from `ONLYOFFICE/sdkjs-plugins`: one simple (e.g. "hello world"), one that uses
   `callCommand` heavily (e.g. "translator" or "typograf"), one with a context menu.
2. Install each into the plugins folder and record what breaks (missing `executeMethod` names, missing `Api`
   methods, unsupported config keys).
3. **Permission policy decision.** Real plugins don't declare `hostPermissions`, so today they are refused.
   Options:
   - A: missing `hostPermissions` means all permissions (compatible; trusts every plugin).
   - B: ask the user once per plugin on first use, and remember the answer in the plugin's data dir.
   - C: derive permissions from the ONLYOFFICE config (`isSystem`, `isViewer`, `EditorsSupport`).
   **Decided 2026-11-28 (ADR-002):** A is the default, so plugins without `hostPermissions` get all three
   permissions on first use. Read-only grants are offered per method. Grants are stored in a host-owned file in
   the host profile, not in the plugin data dir as this item previously said. Network is a separate permission.
4. Grow the `Api` shim and the `executeMethod` table by measured need. Only add what the 3 plugins call.
5. Dynamic context menu items (changing per selection): implement only if one of the 3 plugins needs it.

**Done when:** 3 real plugins run, or each failure is listed with its cause.

---

## Phase 5: Known gaps (do after phase 2 is green)

| Gap | Fix | Size |
|---|---|---|
| Panel is blank when the plugin stopped or failed to load | Show a native "Plugin stopped — Reload" label in the panel window | S |
| Missing WebView2 Runtime is only logged | Same label with a download link to the Evergreen runtime | S |
| One browser process tree per plugin (~100 MB+ each) | Share one `ICoreWebView2Environment` across plugins (one browser process, one renderer per plugin). Only after stage 7 passes, so crash recovery is measured first | M |
| Hand-written JSON parser `json_mini` | Replace with `JS_ParseJSON` / `JS_JSONStringify` from QuickJS | S |
| Plugins are installed by copying folders | `unopkg`-style install of plugin zip into the plugins root; delete the plugin's data dir on removal | M |

---

## Phase 6: Linux and macOS (criteria 1 and 8)

See `TEST_MATRIX_LINUX_MACOS.md`.
1. Linux: decision made 2026-11-28: WebKitGTK 4.1 in a GtkPlug on the X11 window, CEF as fallback (`docs/DECISION_LOG.md` item 14).
   Written in `src/webhost_linux_webkitgtk.cxx`, not compiled. The embedding check on the Linux host still decides
   whether it works under the LibreOffice gtk3 VCL plugin's `systemchildwindow`; it probably needs a core patch.
2. macOS: out of scope for now (`docs/DECISION_LOG.md` item 8). When picked up, `WKWebView` as a child of the `NSView` returned by `nativeWindowHandle`.
3. Both reuse `WebHost`, `ProtocolAdapter`, `CommandEngine` and `runtime.js`; only `webhost_<platform>.cxx` is new.

---

## Phase 7: Decision

Write `ADR-001` (go / no-go for the plugin market on this architecture) using the `RESULTS.md` numbers. It must
cover memory per plugin, deck-switch time, command throughput, crash isolation and the compatibility rate from
phase 4.

---

## Housekeeping

- `C:\Oracle\lo`: `PLAN_BUILD_WINDOWS.md`, `PLAN_PROTOTYPE_P1_HAIKU.md` and the edited `AGENTS.md` are still
  uncommitted. Commit them locally only; the `lo-plugins` branch holds LibreOffice core and must never be pushed.
  Copy any document that needs publishing to the docs-only `lo-plugins-docs` branch.
- Never commit `third_party/` or `.claude/settings.local.json`.
- To run the CI build: Actions → build-windows → Run workflow on `plugin-host-draft`.
