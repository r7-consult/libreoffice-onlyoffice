# Review of the P-1 draft and consolidated plan (2026-11-26)

Code navigation used lean-ctx / Grep: no tool named `ontoindex` exists in this session.

## 1. What went well
- Honest status (NOT VERIFIED everywhere); gates in `RESULTS.md` are measurable.
- Browser session decoupled from the panel (`HostService`), the right shape for criterion 2.
- Protocol and Api shim are plain JS and are tested with node.
- Every stage logs one greppable line; Python tests depend only on the log.

## 2. What went worst
1. **Nothing compiled.** 3500 lines of unverified C++ stacked on assumed UNO/WebView2 signatures. Risk compounds with every file.
2. **Stage 3 would have passed falsely**: `reparent()` only stored a handle and logged. Worse, the panel window was destroyed on deck switch, which destroys the WebView2 child with it. Fixed (parking window, real `put_ParentWindow`).
3. **Criterion 3 was not achievable as written**: Undo rollback used "undo once if the count grew", but several native calls make several undo steps. Fixed: one `enterUndoContext` per command with a unique title.
4. **Raw `this` in COM callbacks** (use after free). Fixed with a shared alive flag.
5. **Any ProcessFailed forced a full restart**, including harmless GPU/utility failures. Fixed: render failure -> `Reload()`, browser exit -> recreate, others ignored.

## 3. Forgotten (not in the draft)
- Panel did not resize with the sidebar (fixed: window listener).
- No check or message when the WebView2 Runtime is missing (only a log line).
- Context menu click is not handled; the menu attaches to one document only.
- Security model: plugin pages run from `file://`, can call `callCommand` with arbitrary JS. Needs a virtual host mapping and a permission list before any market.
- `ProcessFailed` handler runs inside WebView2 callbacks; a Panel "stopped" view and automatic re-create are missing.
- Uninstall/cleanup of the user data folder, DPI, IME, accessibility.

Second pass (same day) closed: missing-runtime log line, context menu click (protocol handler), virtual https host per plugin instead of `file://`. Third pass closed: `hostPermissions` gate, multi-document menu attach. Still open: "stopped" panel view, JSON parser replacement, and a user-facing permission prompt for ONLYOFFICE plugins that declare nothing.

## 4. Overengineering
| Item | Verdict |
|---|---|
| Per-plugin session map, `pluginIds`, scan of many plugins | Prototype needs exactly one plugin. Keep the map, drop scanning later. |
| Hand-written `json_mini` (311 lines) | Replace with QuickJS `JS_ParseJSON` (already linked) or only extract fields the protocol needs. Do after the first compile. |
| Linux/macOS stub and test matrix before Windows works | Defer; write them after criteria 1-5 pass on Windows. |
| `PasteText`, `GetSelectedText`, handshake fidelity | Only what the tests use. |
| `scripts/*` seven PowerShell files | Fine, but `run_all.ps1` should be the single entry. |

## 5. Options for the next tasks
| Task | Option A | Option B | Option C | Chosen |
|---|---|---|---|---|
| Get a compile signal | Wait for the other machine | GitHub Actions (MSVC preinstalled, SDK MSI extracted) | Install MSVC locally (needs admin, blocked) | **B** |
| Browser survives panel death | Recreate browser per panel (loses state) | Park in hidden HWND, reparent back | Own top-level window | **B** |
| Atomic Undo | Count actions and undo | Undo context with unique title | Snapshot document | **B** |
| Crash recovery | Always recreate | Reload for renderer, recreate for browser | Ignore | **B** |
| Protocol parsing in C++ | Own parser | QuickJS parse | Add nlohmann/json | **B later** (A kept until it compiles) |
| Watchdog | Thread kills QuickJS | Interrupt handler with deadline | Process isolation | **B** |

## 6. Consolidated plan
Done in this commit: parking window, real reparent, alive guard, ProcessFailed kinds, undo context, resize listener, CI workflow draft.

Next, in order (each step stops on the first failure):
1. Push a draft branch and run `build-windows.yml`; fix compile errors only (no features). Needs the remote decision (B3).
2. Run stages 1-2 on a machine with LibreOffice: panel shows the probe page.
3. Stages 3, 4, 5, 7 gates; fill `RESULTS.md` with measurements.
4. Then, and only then: context menu click, `json_mini` removal, security (virtual host, permissions), Linux/macOS.

---

# Review 2 (2026-11-27)

## What review 1 missed
Review 1 looked at runtime behaviour and skipped the build chain and the product goal. Both had blockers.

**Build chain: the first compile would have failed in five places (all fixed):**
| Defect | Effect | Fix |
|---|---|---|
| QuickJS list named `cutils.c`, which 0.18.0 does not have | configure error | `add_subdirectory` upstream CMake (also brings its MSVC flags: `/experimental:c11atomics`, `_GNU_SOURCE`) |
| `CPPU_ENV=msci` | 32-bit UNO bridge ABI in a 64-bit DLL: link errors or crash in UNO calls | `mscx`, plus `WIN32 WNT` as in `sdk/settings` |
| Linked `sal cppu cppuhelper salhelper` | Windows import libs are `isal icppu icppuhelper isalhelper` | fixed names |
| `uno-component;type=native` in the manifest | legacy active registration; the panel factory may never be found | passive `pluginhost.components` |
| `cppumaker` read `sdk\ure-link-lib\*.rdb` | that folder is Linux-only; on Windows the rdb files are in `<office>\program` | script uses `LO_PROGRAM`, puts it on `PATH` |
Also: duplicate declaration of `CreateCoreWebView2EnvironmentWithOptions` (already in `WebView2.h`) removed; CI did not run on the pushed branch (trigger fixed), had no MSVC environment and no office types (now `msvc-dev-cmd`, both MSIs, latest stable version found automatically).

**Product goal: ONLYOFFICE plugins would not have worked.** The runtime implemented an invented API (Promise-only, `setContextMenuItems`, `init()` called without data before `init`). Real plugins use callbacks and `Asc.scope`. Fixed and node-tested:
- `executeMethod(name, params, callback)`, `callCommand(func, isClose, isCalc, callback)`; both also return a Promise.
- `Asc.scope` copied to the engine (parsed with `JS_ParseJSON`, no code splicing).
- `init(data)` once, on `init`; `event_<name>` handlers; `AddContextMenuItem` (nested items flattened) and `attachContextMenuClickEvent`; `executeCommand("close")`.
- The test fixture is now written as an ONLYOFFICE plugin.

**Runtime robustness:**
- A command returning nothing produced `"result":undefined` (invalid JSON); the page promise never settled. Now `null`.
- QuickJS on the LibreOffice main thread had no stack or heap limit: deep recursion could crash LibreOffice instead of throwing. Now 256 KB stack, 256 MB heap.
- A stale watchdog deadline could fire while preparing the next command. Reset first.
- `config.json` was read from disk on every page message (permission check). Cached per plugin.

## Still overengineered / to simplify
- One WebView2 environment (= one browser process tree, ~100+ MB) per plugin. Options: (A) keep per-plugin, strongest crash isolation; (B) one shared environment, per-plugin virtual host origins, one process tree; (C) shared environment with per-plugin `--renderer-process-limit`. **Chosen for the market: B** (memory scales with plugins otherwise); prototype keeps A until criterion 5 is measured.
- `json_mini` (311 lines): replace with QuickJS JSON after first green build.
- Python stage tests and per-stage logs: keep, they are the only gates.

## Known limits (accepted)
- Context menu items are a static cache, not per-selection (the menu must open in <= 300 ms without a page round trip).
- `hostPermissions` is our field; ONLYOFFICE packages need a host policy or prompt.

## Plan (replaces section 6 order)
1. Green CI build of `plugin-host-draft` (compile errors only; no features). Each push runs it.
2. Stages 1-2 on a machine with LibreOffice; then stage 3, 4, 5, 7 gates into `RESULTS.md`.
3. Run 3 real ONLYOFFICE sample plugins unmodified (hello world, a callCommand one, a context menu one); list what breaks.
4. Shared WebView2 environment (option B), then `json_mini` removal, then Linux/macOS.
