# Plan: full ONLYOFFICE plugin API support

Status on 2026-11-28: about 10 document `Api` calls and 4 `executeMethod` names are implemented
(see `PLAN_NEXT.md`, phase 4). This plan describes how to reach the whole plugin API.
Revision 2 (same day): reviewed and challenged; the findings are in section 8 and are already applied to
sections 0–7.

## 0. Target: what "100 %" means

"100 %" is a **measured** target. Every inventory item (step 1) gets one state in `docs/API_COVERAGE.csv`:

| State | Meaning |
|---|---|
| DONE | Implemented, conformance test passes |
| MAPPED-LIMITED | Implemented with a recorded difference, such as units, default styles or numbering |
| CORE | Needs a LibreOffice core change, tracked as K-1…K-6 from `LIBREOFFICE_PLUGIN_HOST_OPTION_A_PLUS.md` |
| WONTFIX | Meaningless in LibreOffice, with the reason (server-only features, co-editing, ONLYOFFICE-only UI) |

**Gate (usage-weighted, primary):** DONE + MAPPED-LIMITED cover ≥ 99 % of the API calls made by the top 50
marketplace plugins, and each of those 50 plugins passes its smoke script or has every failure explained.

**Reported, not a gate:** coverage by count of inventory rows. A count target would spend most of the budget
on members that no plugin calls (revision 1 had "≥ 90 % by count"; dropped, see section 8, R-3).

**No row may stay TODO or UNKNOWN when the plan is declared complete**, even if its state is WONTFIX.

Not reachable from an extension alone:
- **K-3:** content controls, plugin objects (OLE), form fields and JS macros stored in OOXML need changes in the
  `oox` and `xmloff` filters.
- **K-1:** Calc custom functions registered at run time under arbitrary names.
- **K-2, K-4, K-5:** ribbon tabs, exact caret screen position, in-document buttons of content controls.
- Features of ONLYOFFICE's server: co-editing, document builder service URLs, server file links.

## 1. Step 1: inventory and usage

The plan cannot be sized without the real list. Build it from sources, by script:

1. Parse the public ONLYOFFICE sources (`office-js-api`, `sdkjs-plugins`, `sdkjs`, api.onlyoffice.com). List every
   `Asc.plugin` member, every `executeMethod` name, every event, every `Api` class and member for Word, Cell,
   Slide, Form and PDF, and every `config.json` key.
2. Write `docs/API_INVENTORY.csv` with columns `area, class, member, kind, editors, signature, read|edit, source`.
   The `read|edit` column feeds the permission allowlist in section 2.
3. Usage: download the top 50 marketplace plugins and statically count which rows each calls. Write
   `docs/API_USAGE.csv`. Static counting misses dynamic names (`executeMethod(name)` from a variable); flag those
   plugins for a runtime trace in step 4 of section 4.
4. Record the ONLYOFFICE version, and re-run on each ONLYOFFICE release.

**Licence:** names and signatures only. No code or documentation text copied.
**Done when:** both files are committed and every row has `editors` and `read|edit`.
**Size:** 3–5 days.

## 2. Architecture: generic bridge, `Api` written in JS

Today each `Api` method is a C++ operation in `api_bridge.cxx` plus a JS shim. That does not scale to
thousands of members.

| Option | Work per member | Verdict |
|---|---|---|
| A. Keep C++ per method | C++ + JS + test, compile per change | Reject: too slow to develop |
| B. Generic UNO call from QuickJS, `Api*` classes in JS | JS + test | **Choose** |
| C. Reuse LibreOffice `jsuno` | Low | Reject: off by default (`--enable-quickjs`), exposes all of UNO, no limits |

### 2.1 Bridge

- One C++ primitive: `call(handle, interface, method, args)`, plus `get`/`set` for properties.
- It is implemented with Core Reflection (`XIdlMethod::invoke`). That is slower than a direct C++ call; the M2 benchmark
  measures it (section 7).
- Handles are integers in a table owned by one command. All are released when the command ends, so no UNO object
  outlives a command, which matches ONLYOFFICE, where `callCommand` keeps no objects either.
- Bulk calls are first-class: `getDataArray`/`setDataArray`, enumeration of a whole paragraph range, so one JS
  call is one UNO call.

### 2.2 Security: the bridge must not be reachable by plugin code

**Finding from this review:** today `command_engine.cxx` puts the primitive on the global object as `__ph_call`,
so plugin code can call it directly and skip the `Api` layer. With today's ten fixed operations that is harmless.
With a generic bridge it would expose any allowed UNO method to any plugin. Required before the first generic call:

1. The primitive is passed into the `Api` layer as a closure argument and never stored on `globalThis`.
2. After the `Api` layer loads and before plugin code runs, every built-in prototype and `Api*` class is frozen
   (`Object.freeze` on `Object`, `Array`, `Function`, `Promise` and the rest of the intrinsics, and on all `Api*`
   prototypes). Plugin code then cannot hook a built-in the `Api` layer calls and capture handles or values.
3. The allowlist is **per (interface, method)**, not per interface. It is generated at build time from the calls
   that `extension/js/api/*.js` actually makes. Every entry is marked `read` or `edit` from the inventory, so the
   `document.read` / `document.edit` permission is checked inside the bridge.
4. A fixed denylist always wins over the allowlist: storing or loading files (`XStorable`, `XComponentLoader`),
   dispatch (`XDispatchHelper`, `XDispatchProvider`), scripting and macros, shell execution, file access
   (`XSimpleFileAccess`), the service manager, and configuration.
5. Fuzz tests (section 4) try to reach a denied method through every `Api` class.

### 2.3 Shared conversion layer (build first)

Every `Api` area depends on it, so it is part of step 2, not of the work packages:
- Units: ONLYOFFICE twips, points and EMU ↔ UNO 1/100 mm.
- Colours, enums, alignment and border constants.
- Range addresses: A1 and R1C1 ↔ `CellRangeAddress`.
- Text positions. ONLYOFFICE Word uses character positions in the document. Writer has no direct offset index, and a
  cursor that moves character by character is O(n). The layer keeps a per-command paragraph → start-offset table,
  built once with one enumeration, then moves within one paragraph. This is the main performance risk of the Word
  `Api`; measure it in M2 on a 300-page document.

**Exit criterion for step 2:**
- The current ten methods run on the generic bridge, and the existing tests pass.
- Filling 10 000 cells with one bulk call takes < 1 s; per-cell calls (10 000 bridge calls) take < 2 s.
- `GetRange(start, end)` on a 300-page document takes < 50 ms.
- The fuzz suite cannot reach any denied method.

**Size:** 2–3 weeks.

## 3. Work packages

Order inside each package follows `API_USAGE.csv`: most-used members first. A package is complete when its members
up to the 99 % usage line are DONE or MAPPED-LIMITED and the rest are recorded with a state.

| # | Package | Content |
|---|---|---|
| WP1 | Plugin runtime | All `Asc.plugin` members: `executeMethod`, `callCommand`, `executeCommand`, `resizeWindow`, translation, theme, `button`, `info`, `Asc.scope` limits |
| WP2 | Plugin types and windows | panel, panelRight, window (modal and non-modal), background, system; window buttons, `isVisual`, `isModal`, `isInsideMode`, sizes, `EditorsSupport` |
| WP3 | Events | Document, selection, sheet, slide and context-menu events from UNO listeners; `config.events` and `attachEvent` |
| WP4 | Writer `Api` | Document, paragraphs, runs, text properties, styles, tables, sections, headers and footers, bookmarks, hyperlinks, comments, footnotes, fields, shapes and images |
| WP5 | Calc `Api` | Worksheets, ranges, formats, formulas, names, charts, conditional formats, validation, protection |
| WP6 | Impress `Api` | Presentation, slides, shapes, text frames, images, tables, charts, layouts and masters |
| WP7 | `executeMethod` catalogue | Selection, search and replace, insert, get text/HTML/file, comments, track changes, content controls |
| WP8 | Menus and toolbar | Dynamic context-menu items with cached synchronous answers; toolbar and menu via the UI configuration API. Ribbon tab is CORE (K-2) |
| WP9 | Content controls and forms | Emulation with fields and frames where ODF allows. OOXML round trip is CORE (K-3) |
| WP10 | Macros and custom functions | JS macros in document user properties; Calc add-in with `XVolatileResult`. Run-time names are CORE (K-1) |
| WP11 | Manager and security | Install, update and remove with data cleanup; permission policy (`PLAN_NEXT.md` phase 4); admin policy |
| WP12 | Platforms | macOS (`WKWebView`), Linux (CEF or WebKitGTK). Only the browser host is new |

**Sizing rule instead of a fixed total.** Revision 1 gave 40–55 person-weeks. That number was not derived from
anything, and the full API is likely several thousand members, so it was removed (section 8, R-2). After step 1:

```
weeks per package = members up to the 99 % usage line / members per week
                  + fixed cost per package (conversion work, fixtures)
```

Measure `members per week` in M2 on the ten ported methods plus the first 30 members of WP5. Plan with that
measured rate, not an assumed one. WP1–WP3 and WP8–WP12 are sized as features, not by member count.

Dependencies: step 1 → step 2 → WP1, WP2 → WP3 → WP4–WP7 in parallel → WP8–WP11. WP12 starts after ADR-001.

## 4. Conformance testing

1. **Expectations from ONLYOFFICE itself.** Use ONLYOFFICE Document Builder, which runs the same `Api` scripts
   headless, to produce the expected results once per member. Check its licence (community edition) before use.
   Plugin UI members, which Document Builder cannot run, get expectations from the Desktop Editors by a recorded
   manual run.
2. **Behavioural comparison, not file comparison.** A test sets a value through `Api` and reads it back through
   `Api`, and also reads the UNO model directly. Comparing exported files between two engines is too noisy.
   Accepted differences are recorded as MAPPED-LIMITED with the reason. No expectation is edited without a recorded
   reason.
3. **Generated tests.** One test per inventory row, from a template per member kind (getter, setter, collection,
   constructor). Fixtures are ODF and OOXML files in `tests/fixtures/`.
4. **Real plugins.** The top 50 plugins run unmodified, with a smoke script each. Plugins flagged in step 1 for
   dynamic method names also get a runtime trace of the calls actually made. Results go in `docs/PLUGIN_COMPAT.md`.
5. **Performance budgets** from `RESULTS.md` and section 2.3 run on every package; a package may not break an
   earlier budget.
6. **Fuzz and abuse:** malformed arguments, infinite loops, huge strings, handles used after their command ended,
   attempts to reach denied methods (section 2.2). Nothing may crash LibreOffice or escape the sandbox.
7. **Coverage report:** `scripts/coverage.py` joins inventory, usage, tests and states, prints counts per area and the
   usage-weighted figure, and fails if a row is DONE without a passing test.

## 5. Core changes to ask for in parallel (not blocking)

One tracked issue per item, small upstreamable patches. Each moves rows from CORE to DONE:
- **K-1:** register Calc functions at run time.
- **K-2:** an API for ribbon tabs.
- **K-3:** OOXML and ODF filter support for content controls and plugin objects.
- **K-4:** the caret's screen position, and view events.
- **K-5:** buttons on content controls.
- **K-6:** a native window handle for gtk3 under Wayland.

LibreOffice core never goes to a pushed branch of this repository. Patches live in `C:\Oracle\lo` and go upstream
as patches.

## 6. Risks

| Risk | Effect | Handling |
|---|---|---|
| Inventory far larger than expected | Long schedule | Usage-weighted gate; the long tail is recorded, not implemented |
| Plugin code reaches the bridge | Sandbox escape inside UNO | Section 2.2: closure-only primitive, frozen intrinsics, method-level allowlist, denylist, fuzz tests |
| Core Reflection overhead | Slow commands | Bulk calls; M2 benchmark; fall back to C++ for the few hot members only |
| Character positions in Writer | Slow Word `Api` on big documents | Offset table per command (section 2.3); 50 ms budget |
| Model differences (styles, numbering, units) | Plugin output differs | Recorded as MAPPED-LIMITED; compatibility code only where a top-50 plugin needs it |
| UNO calls cannot be cancelled on the main thread | Watchdog cannot stop one long call | Bulk calls with size limits; watchdog checked between bridge calls |
| ONLYOFFICE changes the API | Coverage drifts | Re-run the inventory per ONLYOFFICE release |
| Licence of copied material | Legal exposure | Names and signatures only; implementations from documentation and observed behaviour |

## 7. Milestones and gates

| Milestone | Gate |
|---|---|
| M0 | `PLAN_NEXT.md` phases 1–2 done: the prototype is built and measured; ADR-001 written |
| M1 | Inventory and usage committed |
| M2 | Generic bridge, security measures and conversion layer merged; step 2 exit criteria met; `members per week` measured; package sizes computed |
| M3 | WP1–WP3 done; the 3 plugins of `PLAN_NEXT.md` phase 4 run unmodified |
| M4 | WP4–WP7 done up to the 99 % usage line; compatibility report published |
| M5 | WP8–WP11 done; CORE rows have upstream issue numbers |
| M6 | WP12 done; the section 0 gate is met on all platforms |

Go / no-go reviews happen at M0 (ADR-001) and at M2, where the bridge cost and the real development rate are known.

## 8. Review log

### Revision 2: review and challenge of revision 1

| # | Finding | Change |
|---|---|---|
| R-1 | **Security hole in the design.** Revision 1 said "allowlist of services and interfaces". An interface-level allowlist exposes every method of that interface; for example `XModel` leads to the frames and `XStorable` to arbitrary file writes. Also, the current code exposes `__ph_call` on the global object, so plugin code can bypass the `Api` layer | Section 2.2: method-level allowlist generated from the `Api` sources, fixed denylist, primitive only in a closure, frozen intrinsics, fuzz tests. Required before the first generic call |
| R-2 | **Unfounded estimate.** 40–55 person-weeks was not derived from a count. For a few thousand members it implies more than ten members per day with tests | Fixed total removed. Sizing formula with a rate measured in M2 |
| R-3 | **Wrong primary target.** "≥ 90 % of rows by count" pays for members no plugin uses | Usage-weighted gate (99 % of the top 50 plugins' calls). Count is reported only |
| R-4 | **Expectations recorded through Document Server** are hard to automate for `Api` members | ONLYOFFICE Document Builder runs the same `Api` headless; Desktop Editors only for UI members |
| R-5 | **File-diff testing** between two engines produces noise | Behavioural round trip through `Api` and UNO |
| R-6 | **Cross-cutting conversion work** (units, colours, enums, addresses, text positions) was spread over the work packages and would be built several times | Section 2.3, built once in step 2 |
| R-7 | **Missing performance risk:** character-position ranges in Writer | Offset table design and a 50 ms budget on a 300-page document |
| R-8 | **Core Reflection cost** was not mentioned; the exit criterion "within 20 % of C++" compared to an unmeasured baseline | Absolute budgets in step 2 exit criteria; C++ fallback only for measured hot members |
| R-9 | **Static usage counting** misses dynamic `executeMethod` names | Runtime trace for flagged plugins (section 4.4) |
| R-10 | **Over-engineering:** lazy per-class loading of `Api` files was proposed without a measured start-up cost | Removed; revisit only if start-up is measured as slow |
| R-11 | **Inventory lacked a read/edit column**, so the permission split had no source | Column added in step 1 |

What revision 1 got right and is kept: the inventory-first order, the generic bridge over per-method C++, the
rejection of `jsuno`, the CORE state with K-numbers, the separation of the browser host per platform, and the M0 and
M2 go / no-go points.
