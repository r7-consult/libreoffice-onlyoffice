# Architecture review: plugin-host, hand-off to the Linux host

Reviewer: senior review (read-only). Date: 2026-11-28.
Scope: committed HEAD `373fe11` of `C:\Oracle\plugin-host`, plus the uncommitted work in progress seen in the working tree
during the review (`bridge.js`, `check_card.py`, `webhost_linux_webkitgtk.cxx`, `run_member.py`, `test_bridge.js`,
new `extension/js/api/`, `tests/members/`). Where WIP already fixes a HEAD defect, this is said.
Fixed decisions not re-opened: Linux only, WebKitGTK, `PH_TEST_HOOKS` compile-time, no admin pre-approval,
UNCLASSIFIED = edit, pilot = the ten shim methods.

Severity: **S1** = blocks the pilot or crashes LibreOffice; **S2** = wrong result, security hole or large performance
loss; **S3** = cleanup.

---

## 1. What was done well (keep it)

1. **Honest status.** Every document says DRAFT / NOT VERIFIED; `RESULTS.md` forbids PASS without a measurement.
   Keep this rule through the hand-off.
2. **Decision log with "where applied"** (`docs/DECISION_LOG.md`). Makes decisions traceable to code. Keep.
3. **Checking names against the LibreOffice source** (Review 3 in `REVIEW_AND_PLAN.md` found 8 real defects:
   `getWindowHandle` returns Any, `LayoutSize`, `ActionTrigger` factory, interrupt-handler signature, ...). This is the
   right substitute for a compiler. It missed the semantic bugs below, though (it checked names, not behaviour).
4. **C++ is the enforcement point.** `BridgeSession::opCall` checks denylist, allowlist, read/edit permission and
   argument count (`src/bridge_native.cxx:314-365`); allowlist load fails closed (`command_engine.cxx` `allowlistFile`,
   logs "every bridge call is refused").
5. **Per-command lifetime.** Handles live for one command; `BridgeScope` clears the global session pointer; root = handle 1.
6. **Undo context with a unique title** (`api_bridge.cxx:239-274`): one Undo per command, rollback on error/timeout.
   Correct design, independent of the undo limit.
7. **Watchdog via interrupt handler + stack and heap limits** (`command_engine.cxx` `engineFor`: 256 KB stack, 256 MB heap).
8. **`Asc.scope` parsed with `JS_ParseJSON`**, never spliced into code.
9. **Compile-time test hooks** (`#ifdef PH_TEST_HOOKS` in `jsDirectory()` and `testServiceEnabled()`): release builds
   cannot be switched by environment variables.
10. **WebKit host hygiene:** custom scheme with `..` check (`safeRelativePath`), navigation restricted to the scheme,
    dev extras off, per-plugin origin. WIP adds a parking window, case-insensitive hosts, `web-process-terminated`,
    `gtk_disable_setlocale()` before its own `gtk_init_check` - all good.
11. **Pure-JS parts are node-tested** (`tests/test_bridge.js`, `test_api_shim.js`, `test_runtime.js`,
    `test_allowlist.js`, which checks the allowlist against the IDL tree).
12. **ADR-003 "Differences that are NOT acceptable"** list (13 items) is the most valuable part of ADR-003: it is
    short, objective, and useful to any implementer, Haiku or not.

---

## 2. Worst / most likely wrong or fragile

Ordered by impact. File:line refers to HEAD unless marked WIP.

### 2.1 S1 - The Linux web host cannot embed under the default gtk3 VCL plugin
`webhost_linux_webkitgtk.cxx` embeds with `gtk_plug_new(XID)`. With the gtk3 VCL plugin the XID from
`VCLXSystemDependentWindow::getWindowHandle` is `SystemEnvData::GetWindowHandle` of a GtkGrid without its own window
(`vcl/unx/gtk3/gtkobject.cxx:40-60` creates `gtk_grid_new()` inside the frame's GtkFixed), i.e. a GDK window owned by
this process. GtkPlug refuses in-process non-GtkSocket parents ("Can't create GtkPlug as child of non-GtkSocket") and
becomes a stray top-level. HEAD's `if (!plug)` never fires. The WIP version detects it and returns nullptr - so on
gtk3 (GNOME, Ubuntu, Fedora defaults) the panel is **blank by design**. Under native Wayland the "XID" is a `wl_surface*`.
Only qt5/kf5 on xcb gives a real foreign child window. (Same conclusion in the parallel `review_webhost.md`.)
Decision 14 picks WebKitGTK; that is not challenged. **The embedding mechanism is.** See task 6(a)/(e) and plan step L1.

### 2.2 S1 - Every bridge call fails: method lookup is done on `XInterface`
`bridge_native.cxx:337`: `m_reflection->getType(Any(obj))` where `obj` is `Reference<XInterface>`.
`IdlReflectionServiceImpl::getType` returns the class of the Any's *static* type (`stoc/.../crefl.cxx:89`:
`forType(rObj.getValueTypeRef())`) = `com.sun.star.uno.XInterface`. Its `getMethods()` has only
queryInterface/acquire/release, so the loop never finds the method and every call throws "method not found".
Fix: `Reference<XIdlClass> cls = m_reflection->forName(iface); target = cls->getMethod(method);` (cache per
`iface.method`). `IdlInterfaceMethodImpl::invoke` then queries the object for the declaring interface itself
(`criface.cxx:455` -> `mapToUno` -> `extract`). Also removes the per-call `getMethods()` scan (performance).

### 2.3 S1 - Interface arguments are passed with the wrong vtable (memory corruption)
`bridge_convert.cxx:84-92`: `ref = resolve(...)` (a `Reference<XInterface>`), then `Any(&ref, type)` with `type` =
e.g. `XTextRange`. `Any(const void*, Type)` copies the pointer without `queryInterface`; the callee receives an
`XInterface*` labelled `XTextRange*`. With multiple inheritance the pointer offset differs: undefined behaviour,
likely crash. Two of the ten pilot members hit this (`createTextCursorByRange(paragraph)`, `insertString(cursor,...)`).
Fix: `Any a = ref->queryInterface(type); if (!a.hasValue()) throw ...; return a;`

### 2.4 S1 - `setDataArray` cannot work: `any` element type is refused
`XCellRangeData::setDataArray(sequence<sequence<any>>)`. `toAny` recurses to element type `any`
(`TypeClass_ANY`), hits `default:` and throws "parameter type not supported" (`bridge_convert.cxx:110`). Decision 9
("Any refused ... add one only when a pilot member needs it") - the pilot needs it (`ApiRange.SetValue`). Add
`TypeClass_ANY`: number -> double, string -> OUString, bool -> bool, null -> void.

### 2.5 S1 - UNO exceptions escape through QuickJS C frames and through GTK callbacks
- `jsPhCall` catches only `std::exception` (`command_engine.cxx`, the `__ph_call` wrapper). `nativeCall`
  (`api_bridge.cxx`) uses `UNO_QUERY_THROW`, `getByName`, `getCellRangeByName`, all of which throw
  `css::uno::RuntimeException` (not derived from `std::exception`). Unwinding through QuickJS C code (no unwind tables)
  ends in `std::terminate` = LibreOffice crash. Any `GetRange("bad address")` in a plugin kills the office.
- `ProtocolAdapter::handleExecuteMethod` catches only `std::exception` (`protocol_adapter.cxx:164-172`); it runs inside
  the WebKit `script-message-received` signal handler (C frames of GLib).
- `opGet`/`opSet` call `getPropertyValue`/`getPropertySetInfo()->getPropertyByName` with no catch (only `opCall`
  catches); `jsPhBridge` does catch `css::uno::Exception`, so that path is safe.
- `CommandEngine::run` callers: nothing catches around `run` either.
Rule to apply everywhere: every function that is called from C (QuickJS native function, GLib signal, WebView2 COM
callback) ends with `catch (const css::uno::Exception&)`, `catch (const std::exception&)`, `catch (...)`.

### 2.6 S1 - The TestRunner cannot test anything
- `HostService::setContext` is only called in the `PanelFactory` constructor (`panel_factory.cxx:33`). A headless run
  never creates a panel, so `activeModel()` throws "component context is not set" and `installBridge` returns nullptr
  (`host.is()` false). Every test fails.
- Plugin id `__test__` has no config, so `pluginHasPermission` is false and the bridge refuses every call
  (`docs/TEST_RUNNER.md` says so itself). The factory's whole test loop rests on this path.
- The root is `desktop->getCurrentComponent()`; in `--headless` with a `_blank` frame this is not reliably the loaded
  fixture. Pass the document explicitly: `run(document, source, scopeJson)`.
- Remote pyuno calls arrive on a URP worker thread, not the main thread. QuickJS-ng checks stack depth against the stack
  top recorded at `JS_NewRuntime`; on another thread's stack it can throw "stack overflow" at once or not protect at all.
  At minimum call `JS_UpdateStackTop(rt)` at the start of every `run`; better, marshal `run` onto the main thread with
  `com.sun.star.awt.AsyncCallback` and wait on an `osl::Condition`.
- WIP `run_member.py` now uses a private profile (good), but nothing installs the `.oxt` into that profile, so the
  service is "not registered" (exit 2) on every run. Add `unopkg add -env:UserInstallation=<same URL> pluginhost.oxt`
  before starting soffice.

### 2.7 S2 - Locale-dependent number parsing and printing on Linux
`gtk_init_check` in the gtk3 VCL plugin (`vcl/unx/gtk3/gtkdata.cxx:538`) runs GTK's `setlocale(LC_ALL, "")`; the
user's locale here is likely `ru_RU.UTF-8` (decimal comma). Then:
- `json_mini.cxx:264` `std::strtod` parses `"1.5"` as `1` (stops at '.'), silently.
- `bridge_convert.cxx:37-41` `snprintf("%.17g")` prints `1,5` = invalid JSON; `JSON.parse` throws in the engine.
- `protocol_adapter.cxx:106` `std::to_string(a.number)` prints `%f` with the locale's comma and 6 decimals
  (`1e-7` -> `0,000000`).
Fix: locale-independent conversion (`std::from_chars`/`std::to_chars`, GCC 11+), or `rtl::math`, or let QuickJS do it
(see 4.6). NaN/Infinity must also be mapped (currently `nan` is emitted as a bare token).

### 2.8 S2 - Full recalculation after every command
`CommandEngine::run` calls `recalculateActiveDocument()` -> `XCalculatable::calculateAll()` after every successful
command (`api_bridge.cxx:291-302`), ignoring the page's `isCalc` flag (`runtime.js:149` sends it, nobody reads it).
`calculateAll` recomputes every formula in the workbook, dirty or not. On a large workbook this is seconds per
`callCommand`. Use `calculate()` (dirty cells only) and only when `isCalc` is true; with AutoCalculate on, often nothing.

### 2.9 S2 - Property get/set bypass the allowlist
C++ `opGet`/`opSet` check only the name shape and read/edit flags. JS `propertyRule` checks `props` only "if given";
the host never gives it (`installBridge` sends `canRead`, `canEdit`, `allow`). With option A every plugin has edit, so
plugin code can set any property of any handle it holds, starting with the document model (handle 1): e.g. Calc
`IsUndoEnabled = false` (defeats the one-Undo/rollback guarantee), Writer `RecordChanges`, `RedlineProtectionKey`.
The plan says "allowlist per (interface, method)"; properties were forgotten. The pilot uses no property, so: remove
`get`/`set` until a member needs them, then add a C++ property allowlist (`service-or-interface.Property -> read|edit`).

### 2.10 S2 - The denylist is name-based and both too weak and too strong
- Too weak: no `XDesktop` (`terminate`), `XComponent.dispose`, `XCloseable.close`, `XFrame`, `XController.getFrame`,
  clipboard, `XSystemShellExecute` (matches nothing: no "Shell" part). The allowlist is the real control; the denylist
  should not be presented as a barrier.
- Too strong: `createInstance` is denied by method name (`bridge_native.cxx` `kDeniedMethods`), and
  `XMultiServiceFactory` by interface. Writer/Calc/Impress create tables, shapes, fields, images, comments through the
  **document's** `XMultiServiceFactory::createInstance`. Large parts of WP4-WP6 (`CreateTable`, `AddShape`,
  `AddImage`, fields) are unreachable under ADR-002 item 8 ("not grantable"). Resolve now: allow
  `createInstance` on the document root only, with a service-name allowlist (`com.sun.star.text.TextTable`, ...).

### 2.11 S2 - Documented permission policy contradicts itself; code implements neither version
- ADR-002 "User decisions": **option A**, no prompt, plugins without `hostPermissions` get read+edit+menu.
- DECISION_LOG round 2 item 2: "the first-use prompt is the only approval path".
- FULL_API_STATUS: "Permission policy: option B".
- Code: `protocol_adapter.cxx:52-61` grants only what `config.json` `hostPermissions` lists. Every unmodified ONLYOFFICE
  plugin is refused at its first document call; and a plugin can self-grant by writing `hostPermissions`
  (ADR-002 says declaring "narrows, never grants" - not implemented).
Pick one wording in one place before the prompt is built (task 6(d) recommends a prompt that defaults to Allow).

### 2.12 S2 - Session per plugin, document = "the active one"
`HostService` keys sessions by `pluginId` only; a second document's panel steals the single view
(`acquire` reparents), leaving the first panel blank. Commands target `Desktop.getCurrentComponent()`
(`api_bridge.cxx:57-68`), which can be another document, the Basic IDE or the Start Center. Bind each session to the
panel's `Frame`/`Controller` (already passed to the panel factory) and use that model as the bridge root.

### 2.13 S2 - `__ph_call` is still a global
`engineFor` puts `__ph_call` on `globalThis` (`command_engine.cxx`), contrary to PLAN_FULL_API section 2 ("must be
removed"). Plugin code can call the six native ops directly with no permission check. Acceptable only until the shim is
removed after the pilot - make that removal a pilot exit criterion.

### 2.14 S3 - Smaller defects
- `CMakeLists.txt`: static `qjs` linked into a shared library without `CMAKE_POSITION_INDEPENDENT_CODE ON` -> Linux
  link error "recompile with -fPIC". No `-fvisibility=hidden`: every QuickJS and helper symbol is exported into the
  soffice process.
- `CMakeLists.txt` links WebKitGTK and GTK3 into `pluginhost.so` directly: if `libwebkit2gtk-4.1.so.0` is missing, the
  whole component (sidebar factory, menu dispatch) fails to load with an opaque error, and loading the extension into a
  Qt LibreOffice pulls GTK3 into the process.
- JS `track()` registers only a top-level `$h`; handles nested in arrays (sequence of interfaces) are registered by C++
  but refused by JS as "invalid handle".
- No cap on live handles per session (a loop can grow `m_handles` until the 256 MB JS limit is irrelevant - the C++ map
  has no limit).
- `fileUrl` on Linux does no percent-encoding (spaces, Cyrillic in `$HOME`).
- `PLUGINHOST_PLUGINS_DIR` and `PLUGINHOST_TEST_MODE` are honoured in release builds (`paths.cxx`, `handleReady`);
  same argument as for the two switches moved under `PH_TEST_HOOKS`.
- WebKit: the page shim and `messageHandlers.pluginhost` are available in **all frames**, so a third-party iframe
  inside a plugin page can send `callCommand` as that plugin. Inject `INJECT_TOP_FRAME` and drop messages whose
  frame is not the main frame (or check `event.origin` in runtime.js before forwarding).
- Native shim `writer.paragraphCount` skips tables; ADR-003 MLR-6 says a table counts as one element; the bridge
  enumeration will count tables. The pilot expectation must pick one.
- Doc drift: inventory has 110 method + 51 executeMethod UNCLASSIFIED rows (counted from the CSV), docs say 109 and
  elsewhere 262; UNO_MAPPING says "Works" for chains that never ran.

---

## 3. Forgotten or missing for a Linux hand-off

**Build**
- No Linux scripts. `build.ps1`, `gen_uno_headers.ps1`, `pack.ps1`, `install.ps1`, `run_all.ps1` are PowerShell with
  Windows paths (`cppumaker.exe`, `program\types\offapi.rdb`, `build\bin\pluginhost.dll`).
  Linux needs `gen_uno_headers.sh` (cppumaker with `LD_LIBRARY_PATH=<office>/program`, rdbs in `program/types.rdb`
  and `program/types/offapi.rdb`, or `/usr/lib/libreoffice/program` on distros), `build.sh`, `pack.sh`.
- QuickJS-ng is not in the repository (`third_party/` untracked) and no Linux fetch script exists; `config/versions.json`
  lists Windows tools only.
- No Linux CI. Only `.github/workflows/build-windows.yml`. Ubuntu runners can install `libreoffice-dev`,
  `libreoffice-dev-common` (SDK headers + cppumaker) and `libwebkit2gtk-4.1-dev` from apt - no download from
  documentfoundation.org needed (that was blocker B2 on Windows).
- No glibc/libstdc++ baseline: build on the oldest supported distro (for example Debian 12 / Ubuntu 22.04), not the
  newest, or the `.so` will not load on older desktops.

**Packaging**
- `extension/pluginhost.components` says `uri="pluginhost.dll"`; `META-INF/manifest.xml` says
  `platform=windows_x86_64`; `description.xml` platform is replaced only by `pack.ps1` with `windows_x86_64`. Needs
  per-platform generation (`pluginhost.so`, `linux_x86_64`).
- Runtime dependency on `libwebkit2gtk-4.1-0` is undeclared and unchecked.
- No statement of supported LibreOffice builds: Flatpak and Snap LibreOffice cannot load a host WebKitGTK; TDF tarball
  builds and distro builds differ (bundled libs, bundled Python for pyuno).

**Runtime assumptions not written down**
- VCL plugin (gtk3 / kf5 / qt6 / gen) and session (X11 / XWayland / Wayland) matrix: see 2.1. Wayland is the default
  session on Ubuntu and Fedora.
- Locale (2.7). Test locale must be pinned (`LANG=en_US.UTF-8`, LibreOffice UI `en-US`): default sheet name is
  `Sheet1` only in English; MLR-13 depends on installed fonts.
- Threads: TestRunner (2.6).
- Plugin folder name must be a DNS label (`<id>.plugin.invalid`); installed ONLYOFFICE plugins are often in
  `{GUID}` folders. Map GUID -> label in the host.

**Tests**
- `tests/harness.py` hard-codes `soffice.exe`; `test_stage7.py` kills `msedgewebview2.exe` through PowerShell. Linux
  needs `WebKitWebProcess` (child of soffice) and `pkill -f`.
- `TEST_MATRIX_LINUX_MACOS.md` still talks about CEF and macOS; it has no row for kf5 VCL, Wayland, or keyboard/IME
  inside the embedded view (the riskiest part of any foreign embedding).
- No microbenchmark of the bridge (calls per second), although every performance budget depends on it.
- No fuzz test for the C++ bridge (the plan's F3 exit check).

**Security**
- Permission prompt, grant store, hash of the package: not built (2.11).
- Property hole (2.9), iframe message spoofing (2.14), `createInstance` policy (2.10).
- Plugin network access is unrestricted (ADR-002 open question 3); fine for internal use, but write it down.
- Logging: `pluginhost.log` location and rotation on Linux not defined; plugin error text goes into it.

**Docs**
- One "how to build and run on Linux" page; today the reader must assemble it from `PLAN_NEXT.md` (Windows phases),
  `TEST_RUNNER.md`, `TEST_MATRIX...` and `DECISION_LOG.md`.

---

## 4. Over-engineering, and the simpler approach

### 4.1 The 2,114-row inventory and the coverage file
- The rows are keyed by **member name only** (`API_INVENTORY.csv` has no class column; `AddText` is one row for
  `cell;form;pdf;plugin;slide;word`). The plan's `class` column was dropped, so one row cannot map to one card, one
  test or one implementation; `read_edit` is a "majority class wins" guess across classes.
- The `read_edit` column has **no enforcement consumer** for the 2,063 method rows: `callCommand` needs `document.edit`
  as a whole, and the bridge classifies **UNO** methods in `bridge_allowlist.json`. The only rows whose read/edit
  matters to ADR-002 ("Allow read only" per `executeMethod` name) are the **51 `executeMethod` rows - all 51 are
  UNCLASSIFIED**. Effort went to the 2,063 rows that do not matter and none to the 51 that do.
- `API_COVERAGE.csv` (2,114 rows at TODO) is a second copy of the inventory, hand-maintained, before the first member
  ran. It will drift.
**Simpler:** freeze the inventory as reference data. Drive work from `API_USAGE.csv` (139 names, 38 plugins) and from
the three real plugins of phase 4. Generate coverage from the code and test files (a member is DONE when its test file
exists and passes) instead of a CSV that people edit. Add a class column only when cards need it.

### 4.2 The card factory (cards, templates, `new_card.py`, `check_card.py`, waivers, MLR rules)
Built before one bridge call has ever run. It encodes assumptions that are already wrong: getter template
`this._bridge.get(this._handle, "{{PROP}}")` assumes ONLYOFFICE getters are 1:1 UNO properties (most need unit, enum or
index conversion); waivers and 15 MLR rules with M/R machine checks presume a test loop that cannot run (2.6) and
expectations that do not exist (`tests/expected/` is absent). The Haiku pass-rate gate is a cost question for later.
**Simpler:** after the bridge pilot, Sonnet writes classes by hand, one file per class, with plain tests; keep two
checks from `check_card.py` (tests and expectations unchanged vs HEAD; file size limit). Keep ADR-003's
"not acceptable" list; keep MLR-1/2/4/15 as test-helper functions (tolerant comparisons), drop the waiver CSV and the
R-rule review process until the factory is approved with measured numbers.

### 4.3 Two enforcement layers (JS and C++)
The C++ layer enforces everything the JS layer does (denylist, allowlist, permissions, handle validity, size). The JS
layer adds only nicer messages and a second list to keep in sync; HEAD had already drifted (JS matched full names
against simple names; WIP fixes it, which proves the cost). **Simpler:** C++ is the only policy. `bridge.js` becomes a
thin marshaller: handle wrappers, JSON, `dispose`. Delete the JS denylist, allowlist and policy object; stop passing the
allowlist text into the engine (`installBridge` builds `policyText` with the whole allowlist on every command).

### 4.4 The TestRunner service
The idea is right (headless, JS from the repository, same engine path). The XInvocation service without IDL is a good
small choice. It is not over-engineered; it is under-specified (2.6). Keep it, fix: explicit document argument,
`setContext`, test grants under `PH_TEST_HOOKS`, main-thread marshalling, install into the private profile.

### 4.5 `Asc.bridge` plus the `Api` shim side by side
Two native paths (six hand-written ops in `api_bridge.cxx`, and the generic bridge) for the same ten members; the pilot
tests whichever path the shim uses (today: the old one). Also `Asc.bridge` is put on `Asc` for **plugin code**
(`installBridge` sets `Asc.bridge`), which turns an internal mechanism into a public, non-ONLYOFFICE API that plugin
authors will start to depend on. **Simpler:** the pilot rewrites the ten shim methods on the bridge in one step and
deletes `api_bridge.cxx`'s six ops and `__ph_call` when the ten pass. Give the bridge to the `Api` classes as a closure
argument (as `templates/README.md` already says) and do not publish `Asc.bridge`.

### 4.6 Per-call JSON string protocol
JS `JSON.stringify` -> C `JS_ToCString` -> `json_mini` parse (std::map per object) -> reflection -> string building ->
`JS_NewStringLen` -> `JSON.parse`, per UNO call. It also brings 2.7 (locale) and NaN bugs and keeps the 311-line
hand-written parser that the earlier reviews already wanted gone. **Simpler and faster:** convert `JSValue <-> Any`
directly in C++ with the QuickJS API (`JS_ToFloat64`, `JS_ToCStringLen`, `JS_IsArray`, `JS_GetPropertyUint32`,
`JS_NewFloat64`, `JS_NewArray`) - about the same amount of code as `bridge_convert.cxx`, no text format, no locale.
Do it right after the pilot passes on JSON, with the microbenchmark as evidence; do not block the first compile on it.

### 4.7 Not over-engineered (keep)
Undo context, watchdog, handle lifetime, compile-time hooks, `.xcu` schema checks in `tests/check_xml.py`.

---

## 5. Performance risks

| Risk | Where | Estimate | Mitigation |
|---|---|---|---|
| Method lookup scans `getMethods()` per call (after 2.2 fix: `forName`) | `opCall` | O(methods) per call | Cache `XIdlMethod` per `iface.method` in a static map |
| One JSON round trip per UNO call | `__ph_bridge` | roughly 5-30 us per call (unmeasured) | Microbenchmark first; direct JSValue conversion (4.6) |
| `GetElement(i)` re-enumerates from the start | `UNO_MAPPING.md` row 8 | O(i) calls per element, O(n^2) for a loop: ~3,000 paragraphs on 300 pages -> ~4.5 M bridge calls -> minutes; budget is 50 ms | Native op `enumerate(handle) -> [handles]` (one call), plus a per-command element cache in `ApiDocument` |
| `GetElementsCount` walks the enumeration with 2 calls per element | same | 6,000 calls on 300 pages | same `enumerate` op |
| `calculateAll` after every command | `api_bridge.cxx` | seconds on large workbooks | 2.8 |
| `GetActiveSheet().GetRange(a).SetValue(v)` in plugin loops | shim design | 4+ bridge calls per cell | Cache the sheet handle per command in `Api` objects; bulk `setDataArray` |
| Handles never freed within a command | `m_handles` | memory grows with loops | Cap (e.g. 100,000) and fail with a clear error |
| QuickJS runtime per plugin | `engineFor` | ~1 MB each, never freed | Fine; free on plugin removal (`dispose` exists) |
| One `WebKitWebContext` per plugin | `WebHostWebKit` ctor | one web process + one network process per plugin, ~60-150 MB each | Shared context, per-plugin origin (`pluginhost://<id>`); separate data managers are not possible in one context, so accept shared cookies for internal use |
| Allowlist text re-parsed into a JS object on every command | `installBridge` | small, grows with the allowlist | Gone with 4.3 |

---

## 6. Alternatives for the next tasks, and the pick

### (a) First compile and fix pass on the Linux host
1. Install distro packages on the host, compile everything, fix errors as they come.
2. **Staged targets**: `pluginhost_core.so` (component, bridge, engine, TestRunner, no GTK) and
   `pluginhost_web.so` (WebKitGTK host, `dlopen`ed by core); build and test core headless first.
3. Pinned container (Dockerfile on the oldest target distro) used both by GitHub Actions and by the Linux host.
**Pick: 2 inside 3.** The bridge, conversion and TestRunner are the riskiest code and need no display; they can be green
before the embedding question (2.1) is answered. A container makes "builds on the host" equal "builds in CI", fixes the
glibc baseline, and gives a compile signal for every later edit made on this machine without a compiler. `dlopen` also
gives a clean "WebKitGTK missing" message instead of a component that fails to load. Before the first compile, apply the
S1 fixes from section 2 here; they are reading-level fixes and would otherwise surface as runtime crashes, not compile
errors.

### (b) Test expectations without Document Builder
1. **Hand-written expectations** for the pilot, each with `"source": "manual"` and the ONLYOFFICE doc page it follows.
2. Document Builder in a container on any x86_64 Linux box (it ships Linux packages; "cannot run on that host" should be
   checked, not assumed), driven by a small adapter that evaluates the same `TEST` object and prints JSON.
3. Recorder plugin in ONLYOFFICE Desktop Editors that runs the `TEST` objects through `callCommand` and posts results.
**Pick: 1 now, 2 for anything beyond the pilot.** Ten pilot values (`"Sheet1"`, a number, a string, a count) are
unambiguous; building an expectation pipeline before the bridge works is waste. Option 2 is reproducible and
scriptable; option 3 is the fallback if Document Builder cannot run anywhere. Mark manual files so F6 regenerates them.

### (c) The 10-member pilot
1. Rewrite `api_shim.js` on the generic bridge exactly as `UNO_MAPPING.md` says (pure generic path).
2. Keep the six native ops for the pilot and move to the bridge later.
3. **Generic bridge plus two native bulk helpers**: `enumerate(handle)` returning all element handles, and `any`
   support for `get/setDataArray`; everything else through `call`.
**Pick: 3.** Option 1 bakes in the O(n^2) `GetElement` and fails the 50 ms budget on real documents; option 2 does not
test the bridge at all, which is the point of the pilot. Run the pilot with Sonnet and hand-written tests: it validates
the **bridge**, not Haiku. Keep the Haiku factory pilot as a separate, later gate (only if the factory is still wanted).
Exit criteria: 10 tests pass headless; microbenchmark recorded; `__ph_call` and the six ops deleted.

### (d) First-use permission prompt (easy; no admin pre-approval)
1. `css::awt::MessageBox` from the toolkit (`XMessageBoxFactory`): smallest code, but fixed button labels (Yes/No/Cancel),
   poor wording.
2. HTML consent overlay injected into the plugin page: easiest to style, but the plugin controls the DOM and can click
   "Allow" itself. Rejected.
3. **UNO dialog built at run time** (`UnoControlDialogModel` with three buttons and a label from one string table),
   shown **asynchronously**: the gated message is queued, the dialog is posted to the main loop with
   `com.sun.star.awt.AsyncCallback`, queued messages are processed after the answer.
**Pick: 3.** Real button labels and English/Russian strings in one table (decision 3); never runs a nested modal loop
inside a WebKit signal handler or during a command (re-entrancy is the classic crash source here); no admin step.
Keep it easy: Allow is the default button; two answers for now (Allow / Deny) - "Allow read only" only after the 51
`executeMethod` rows are classified (task f). Grants in one host-owned file
(`$XDG_CONFIG_HOME/PluginHost/grants.json`, keyed by GUID + package hash). Headless and TestRunner never prompt.
First reconcile ADR-002 with DECISION_LOG item 2 (2.11): the user's latest words ("first-use prompt") win.

### (e) Linux install and packaging of the `.oxt`
1. Per-user `.oxt` through Extension Manager / `unopkg add` (no root); plugins copied by hand.
2. Shared install `unopkg add --shared`, or a `.deb`/`.rpm` (CPack) that drops the extension into
   `/usr/lib/libreoffice/share/extensions/pluginhost` (bundled extension) with `Depends: libwebkit2gtk-4.1-0`.
3. Flatpak/Snap extension: not feasible (no WebKitGTK in the LibreOffice sandbox runtime).
**Pick: 1 as the primary path, 2 as the fleet wrapper later.** Double-click install with no admin rights matches
"easy to install". Requirements: build on the oldest target distro; `dlopen` WebKitGTK and show a native "WebKitGTK
4.1 is not installed" label in the panel; generate `pluginhost.components`/manifest/description per platform
(`pluginhost.so`, `linux_x86_64`); document the supported VCL plugin/session from the L1 spike (for example a desktop
launcher that sets `SAL_USE_VCLPLUGIN=kf5 QT_QPA_PLATFORM=xcb` if that is the only working embedding). Add a plugin
installer (copy a plugin zip into the plugins root, map `{GUID}` folders to host labels) - today it is manual.

### (f) Closing the 109 UNCLASSIFIED API rows
1. Manual review of every row against sdkjs (Sonnet pass plus spot check).
2. Leave them UNCLASSIFIED (= edit, already the rule) and close lazily when a plugin needs read-only access.
3. Derive the class from the implementation: an `Api` member is `read` when every UNO method it calls is `read` in the
   allowlist; computed by a script once members exist.
**Pick: 2 + 3, and classify the 51 `executeMethod` rows by hand instead.** The method rows have no enforcement consumer
(4.1); the allowlist already has true read/edit per UNO method, so 3 is exact for our host, mechanical, and cannot drift.
The 51 `executeMethod` rows are what "Allow read only" needs, they are few, and they are all UNCLASSIFIED today.

---

## 7. Consolidated plan for the hand-off

Tags: **[here]** = this machine, no compiler; **[Linux]** = the Linux host.

1. **[here]** Reconcile the docs: one permission policy (ADR-002 vs DECISION_LOG item 2 vs FULL_API_STATUS), correct
   UNCLASSIFIED counts, change "Works" to "Not run" in `UNO_MAPPING.md`. Commit the WIP in the working tree first or
   discard it - it overlaps with these fixes.
2. **[here]** Fix the S1 reading-level bugs: 2.2 (`forName`/`getMethod`), 2.3 (`queryInterface` for interface
   arguments), 2.4 (`any` elements), 2.5 (catch UNO exceptions at every C boundary), 2.6 (TestRunner: document argument,
   `setContext`, test grants under `PH_TEST_HOOKS`, `JS_UpdateStackTop` or main-thread marshalling), 2.7 (locale-free
   numbers), 2.8 (`calculate()` only when `isCalc`).
3. **[here]** Close the S2 holes that need no runtime: remove bridge `get`/`set` (2.9), `createInstance` on document root
   with a service-name allowlist (2.10), cap on handles, JS nested-handle tracking or JS policy removal (4.3).
4. **[here]** Linux build kit: `CMAKE_POSITION_INDEPENDENT_CODE ON`, `-fvisibility=hidden`, split core/web targets with
   `dlopen`; `gen_uno_headers.sh`, `build.sh`, `pack.sh` (platform-templated `.components`, manifest, description);
   QuickJS fetch script; Dockerfile on the oldest target distro; `build-linux.yml` workflow.
5. **[here]** Test kit: `run_member.py` installs the `.oxt` into its private profile and passes the document; pinned
   `LANG=en_US.UTF-8`; port `harness.py` and `test_stage7.py` to Linux; write the 10 pilot tests with hand-written
   expectations (6b); add a bridge microbenchmark test; add the `enumerate` op (6c).
6. **[Linux]** L1 embedding spike, before anything else in the GUI: a minimal panel with a WebKitWebView. Try
   (i) GtkPlug under kf5/qt5 on xcb (current code); (ii) gtk3: locate the SalObject's GtkGrid in-process and
   `gtk_container_add` the view (no GtkPlug); check paint, resize, deck switch, **keyboard focus and IME**, X11 and
   XWayland. Record in `RESULTS.md`. If neither is acceptable, open the core change (K-6: expose `pWidget` through
   `getWindowHandle`) and decide the interim supported configuration.
7. **[Linux]** Build the core target in the container; fix compile errors only; record each in `RESULTS.md`.
8. **[Linux]** Run the 10 pilot tests headless and the microbenchmark; fix; record calls per second and the
   300-page `GetElement` timing.
9. **[here]** Rewrite the shim on the bridge, delete the six native ops and `__ph_call` (pilot exit); give the bridge to
   `Api` classes as a closure, stop publishing `Asc.bridge`. **[Linux]** re-run step 8.
10. **[Linux]** Build the web target with the embedding chosen in step 6; run stages 1-7 (Linux variants); fill
    `TEST_MATRIX` rows for gtk3/kf5 x X11/XWayland/Wayland.
11. **[here]** Permission prompt (6d): async UNO dialog, grant file, string table EN/RU; bind sessions and the bridge
    root to the panel's frame (2.12); iframe message filter (2.14). **[Linux]** test it.
12. **[here]** Classify the 51 `executeMethod` rows by hand; **[Linux]** check Document Builder runs (6b option 2).
13. **[Linux]** Phase 4: three unmodified ONLYOFFICE plugins; list what breaks.
14. **[here]** Only then decide on the factory: keep or drop cards, templates, waivers and the coverage CSV, using the
    measured bridge cost and the phase 4 results.
