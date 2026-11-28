# Plan: full ONLYOFFICE plugin API support

Status on 2026-11-28: about 10 document `Api` calls and 4 `executeMethod` names are implemented
(see `PLAN_NEXT.md`, phase 4).

Revision 3 (same day): reviewed for **execution by Haiku**. The plan is split into a small foundation built
once by stronger models and a large, repetitive "factory" that Haiku runs card by card. Review findings are in
section 10 and are already applied.

## 0. Target: what "100 %" means

Every inventory item (section 3, F1) gets one state in `docs/API_COVERAGE.csv`:

| State | Meaning |
|---|---|
| DONE | Implemented, conformance test passes |
| MAPPED-LIMITED | Implemented with a recorded difference, such as units, default styles or numbering |
| CORE | Needs a LibreOffice core change, tracked as K-1…K-6 from `LIBREOFFICE_PLUGIN_HOST_OPTION_A_PLUS.md` |
| WONTFIX | Meaningless in LibreOffice, with the reason (server-only features, co-editing, ONLYOFFICE-only UI) |

**Gate (usage-weighted):** DONE + MAPPED-LIMITED cover ≥ 99 % of the API calls made by the top 50 marketplace
plugins, and each of those plugins passes its smoke script or has every failure explained.

**Reported, not a gate:** coverage by count of rows. **No row stays TODO** when the plan is declared complete.

Not reachable from an extension alone: K-3 (content controls, plugin objects, form fields, JS macros in OOXML),
K-1 (run-time Calc functions), K-2, K-4, K-5 (ribbon tabs, caret screen position, buttons on content controls),
and features of ONLYOFFICE's server.

## 1. Execution model: who does what

Model routing follows `AGENTS.md`. The rule that makes the plan affordable:
**a Haiku task never designs anything.** It fills in a template, runs one command, and either passes or
escalates.

| Work | Model | Why |
|---|---|---|
| Decisions: permission policy, MAPPED-LIMITED rulings, denylist, go/no-go | Opus | Judgement, few calls |
| Foundation F1–F9: parsers, bridge, sandbox, conversion layer, test runner, templates, recipes | Sonnet | Real design and C++; done once |
| Factory: one card = 5–10 members of one class, or one fixture, or one plugin smoke script | Haiku | Repetitive, template-driven, checked by a script |
| Card review: spot check of 1 card in 10, plus every card that changed an expectation | Sonnet | Catches template drift cheaply |

Cost control:
- The factory starts only after the pilot (section 5) shows Haiku passes ≥ 70 % of cards on the first try and
  escalates ≤ 20 %. Otherwise Sonnet fixes the template or recipes, not the cards.
- Every card is self-contained. Haiku reads at most: the card, one recipe file, one class file (≤ 400 lines), and
  the test output. No browsing of the LibreOffice source tree.
- C++ is never touched by a factory card. All factory work is JS, CSV and fixtures, loaded at run time, so no
  compile is needed per card.

## 2. Architecture (unchanged from revision 2, condensed)

- **Generic bridge.** One C++ primitive `call(handle, interface, method, args)` (plus `get`/`set`), implemented
  with Core Reflection. Handles live for one command only. Bulk calls (`getDataArray`/`setDataArray`, paragraph
  range enumeration) are first-class.
- **`Api*` classes in JS**, one file per class under `extension/js/api/`.
- **Sandbox.**
  - The primitive exists only as a closure argument, never on `globalThis`. Today's global `__ph_call` in
    `command_engine.cxx` must be removed.
  - Intrinsics and `Api*` prototypes are frozen before plugin code runs.
  - The allowlist is per (interface, method) and generated from `extension/js/api/*.js`, with read/edit from the
    inventory. A fixed denylist wins: `XStorable`, `XComponentLoader`, dispatch, scripting, shell, file access,
    service manager, configuration.
- **Conversion layer**, built once: units, colours, enums, A1/R1C1 addresses, and Writer character positions
  through a per-command paragraph offset table.
- `jsuno` is not reused (off by default, all of UNO exposed, no limits).

## 3. Foundation (Sonnet, Opus where marked)

Build these in order. Each one has an exit check that Haiku will later rely on.

| # | Deliverable | Exit check |
|---|---|---|
| F1 | `scripts/inventory.py`: parses ONLYOFFICE sources into `docs/API_INVENTORY.csv` (`id, area, class, member, kind, editors, signature, read|edit, source`). Names and signatures only | Every row has `editors` and `read|edit`; the script is reproducible from a pinned ONLYOFFICE version |
| F2 | `scripts/usage.py`: counts calls of the top 50 plugins into `docs/API_USAGE.csv`; flags plugins with dynamic method names | Each of the 50 plugins appears; flagged ones listed |
| F3 | Generic bridge, closure-only primitive, frozen intrinsics, generated allowlist, denylist (C++ + JS) | Fuzz suite cannot reach a denied method; the 10 existing methods pass on the new bridge |
| F4 | Conversion layer `extension/js/api/_convert.js` | Unit tests for every conversion; Writer `GetRange` < 50 ms on a 300-page document |
| F5 | **Headless test runner.** A UNO service `org.example.pluginhost.TestRunner` with `run(source, scopeJson) → resultJson`, plus `tests/run_member.py`, which starts `soffice --headless`, loads a fixture, runs one test and prints PASS/FAIL. Env var `PLUGINHOST_API_DIR` makes the engine load `api/*.js` from the repository, so JS edits need no reinstall | `python tests/run_member.py cell.ApiRange.SetValue` prints PASS in < 10 s; editing a JS file and re-running picks up the change |
| F6 | **Expectations.** `scripts/expect.py` runs each test through ONLYOFFICE Document Builder and stores the result in `tests/expected/<id>.json`. Check the Document Builder licence first (Opus decision) | Expectations exist for the 10 existing methods; files are read-only for factory cards |
| F7 | **Templates.** `templates/member_<kind>.js` and `templates/test_<kind>.js` for getter, setter, collection, constructor, and `executeMethod`. `scripts/new_card.py <class> <ids...>` creates the card file, the stubs and the test files | A generated stub for a new getter passes after filling in one UNO call |
| F8 | **Recipes.** `docs/recipes/{writer,calc,impress,common}.md`: short, tested UNO snippets ("get the text cursor of a paragraph", "set a cell's number format", property names for character attributes) | Each recipe snippet is backed by a passing test |
| F9 | **Card checker.** `scripts/check_card.py <card>` runs: lint, allowlist generation (fails on a denylisted method), the card's tests, the performance budgets of the area, and updates `API_COVERAGE.csv` | Fails on any of: denied method, edited expectation, test without assertion, file > 400 lines |
| D1 | **Decisions (Opus):** permission policy for plugins without `hostPermissions`; the MAPPED-LIMITED rules (which differences are acceptable: units rounding, default fonts, numbering) | Written in `docs/adr/` before the pilot |

**Size:** 5–7 weeks of Sonnet-driven work, plus a few Opus decisions. This is the only part of the plan that needs
a compiler and the build machine.

## 4. Card format (what Haiku receives)

One file per card in `cards/todo/<area>-<class>-<n>.md`, generated by F7. Haiku moves it to `cards/done/` or
`cards/escalated/`.

```markdown
# Card cell-ApiRange-03
Members: ApiRange.SetNumberFormat, ApiRange.GetNumberFormat, ApiRange.SetAlignHorizontal, ... (≤ 10)
Edit only: extension/js/api/cell/ApiRange.js
Read: docs/recipes/calc.md (section "Number formats", "Alignment")
Tests: tests/members/cell/ApiRange/*.js (generated, do not change the assertions)
Run: python tests/run_member.py --card cards/todo/cell-ApiRange-03.md
Done when: python scripts/check_card.py cards/todo/cell-ApiRange-03.md prints PASS
```

**Haiku rules (non-negotiable):**
1. Edit only the files listed under "Edit only". Never edit tests, expectations, templates, recipes, C++,
   the allowlist or the denylist.
2. Use only UNO calls from the recipe file. If the recipe has no way to do it, escalate; do not search the
   LibreOffice source.
3. At most 3 attempts per member. Then mark the member `ESCALATE` in the card with: the test output, what was tried,
   and a one-line guess at the cause. Continue with the next member.
4. If the result differs from the expectation in a way the MAPPED-LIMITED rules (D1) list, record it as
   MAPPED-LIMITED with the rule number. Any other difference is an escalation, not a code change.
5. Never write PASS without the checker's output.

## 5. Pilot (gate for the factory)

1. Sonnet makes 10 cards: 4 Calc, 4 Writer, 2 `executeMethod`, chosen from the most-used rows.
2. Haiku runs them, one card per agent, with no extra help.
3. Measure: first-try pass rate, escalation rate, tokens per card, wall time per card, and how many escalations were
   caused by a missing recipe versus a template flaw.
4. **Gate:** first-try pass ≥ 70 %, escalations ≤ 20 %. If missed, Sonnet improves the recipes or templates and the
   pilot runs again on 10 new cards.
5. The measured tokens per card × number of cards gives the factory cost. Opus approves the budget before the
   factory starts.

## 6. Factory work packages

Cards are generated in `API_USAGE.csv` order: most-used members first. A package stops at the 99 % usage line. Rows
below the line get a state (usually TODO → DONE later, or WONTFIX) by a Haiku card that only classifies.

| # | Package | Factory share | Not for Haiku |
|---|---|---|---|
| WP1 | Plugin runtime (`Asc.plugin` members) | Tests and small members | Window and message plumbing (Sonnet) |
| WP2 | Plugin types and windows | — | All (Sonnet: native windows) |
| WP3 | Events | Event-name mapping rows | Listener wiring in C++ (Sonnet) |
| WP4 | Writer `Api` | Most members | Tables, fields, footnotes, sections (Sonnet writes the first card of each class) |
| WP5 | Calc `Api` | Most members | Charts, conditional formats (first card by Sonnet) |
| WP6 | Impress `Api` | Most members | Masters and layouts (first card by Sonnet) |
| WP7 | `executeMethod` catalogue | Most methods | Search and replace engine, track changes (Sonnet) |
| WP8 | Menus and toolbar | — | Sonnet |
| WP9 | Content controls and forms | Classification of rows | Emulation design (Opus), implementation (Sonnet). OOXML round trip is CORE |
| WP10 | Macros and custom functions | — | Sonnet; run-time names are CORE |
| WP11 | Manager and security | Fixture and smoke scripts | Sonnet |
| WP12 | macOS and Linux browser hosts | — | Sonnet; the JS `Api` is shared |
| WP13 | Top-50 plugin smoke scripts | One card per plugin: script, run, record result | Failures are triaged by Sonnet |

**Rule for a new class:** Sonnet writes the first card of every new `Api*` class and adds any missing recipes.
Haiku continues with the rest of that class. This keeps design out of Haiku's cards.

Sizing: the factory size is `cards × measured cost per card` from the pilot. Sonnet work per package is sized as
features. No total is committed before F1, F2 and the pilot are done.

## 7. Conformance testing

1. Expectations come from ONLYOFFICE Document Builder (F6). Plugin UI members get expectations from a recorded
   Desktop Editors run.
2. Tests are behavioural: set through `Api`, read back through `Api` and through UNO. No file diffs between engines.
3. One generated test per inventory row (F7).
4. The top 50 plugins have a smoke script each (WP13). Flagged plugins with dynamic method names get a runtime trace.
5. Performance budgets run on every card through the checker (F9): 10 000 cells < 1 s in bulk, < 2 s per cell;
   Writer `GetRange` < 50 ms on 300 pages; context menu < 300 ms.
6. The fuzz suite runs on every card that changes a class. Nothing may crash LibreOffice or reach a denied method.
7. `scripts/coverage.py` prints counts per area and the usage-weighted figure, and fails if a row is DONE without a
   passing test.

## 8. Core changes (parallel, not blocking)

One upstream issue per item: K-1 run-time Calc functions, K-2 ribbon tab API, K-3 OOXML/ODF filter support for
content controls and plugin objects, K-4 caret position and view events, K-5 buttons on content controls, K-6 native
window handle for gtk3 under Wayland. Patches stay in `C:\Oracle\lo` and go upstream; LibreOffice core never goes to
a pushed branch of this repository.

## 9. Risks and milestones

| Risk | Handling |
|---|---|
| Haiku drifts from the template, or "fixes" tests | Checker refuses edited tests and expectations; Sonnet spot checks 1 card in 10 |
| Recipes missing, so escalations pile up | Pilot measures this; Sonnet adds recipes, not Haiku |
| Plugin code reaches the bridge | Closure-only primitive, frozen intrinsics, method-level allowlist, denylist, fuzz suite |
| Core Reflection or Writer offsets too slow | Budgets in the checker; C++ fallback only for measured hot members (Sonnet) |
| Inventory far larger than expected | Usage-weighted gate; long tail is classified, not implemented |
| Document Builder licence not usable | Fall back to Desktop Editors runs scripted through a plugin; slower, Sonnet task |
| ONLYOFFICE changes the API | Re-run F1 per release; new rows become new cards |

| Milestone | Gate |
|---|---|
| M0 | `PLAN_NEXT.md` phases 1–2 done; ADR-001 written |
| M1 | F1, F2 done: inventory and usage committed |
| M2 | F3–F9 and D1 done; foundation exit checks pass |
| M3 | Pilot gate met; factory budget approved (Opus) |
| M4 | WP1–WP3 done; the 3 plugins of `PLAN_NEXT.md` phase 4 run unmodified |
| M5 | WP4–WP7 up to the 99 % line; WP13 smoke results published |
| M6 | WP8–WP12 done; CORE rows have upstream issue numbers; section 0 gate met |

Go / no-go reviews: M0 (ADR-001), M2 (bridge cost known), M3 (factory cost known).

## 10. Review log

### Revision 3: can Haiku execute it?

| # | Finding | Change |
|---|---|---|
| H-1 | Revision 2 had no division of labour. Most tasks mixed design and typing, so they needed a strong model throughout | Section 1: Haiku fills templates only; Sonnet builds the foundation once; Opus makes the decisions |
| H-2 | No way to test a member without the GUI and a rebuild. Each attempt would have cost a build and a manual run | F5: headless test runner as a UNO service plus `PLUGINHOST_API_DIR`, so JS edits are tested in seconds. The engine already loads JS from disk at run time (`command_engine.cxx`, `readShim`), so this needs no new loading mechanism |
| H-3 | Haiku would have searched the LibreOffice source for UNO calls: slow, costly and error-prone | F8: tested recipe files; rule 2 in section 4 forbids searching the source |
| H-4 | Expectations could be "fixed" by the executor to make tests pass | F6 and F9: expectation files are generated, read-only for cards, and the checker refuses edits |
| H-5 | Tasks were too large: a work package is weeks of work, far beyond one agent context | Section 4: cards of ≤ 10 members of one class, generated by script |
| H-6 | MAPPED-LIMITED was a judgement call at each member | D1: Opus writes the acceptable-difference rules once; Haiku applies them by number or escalates |
| H-7 | No evidence that Haiku can do the work at an acceptable rate | Section 5: pilot with a measurable gate before the factory starts |
| H-8 | New classes need design (first-of-kind patterns) | Rule: Sonnet writes the first card of every new class |
| H-9 | Several packages (windows, menus, macros, platforms, C++ listeners) are not template work | Marked "not for Haiku" in section 6 instead of pretending they fit |
| H-10 | Escalations had no format, so they would be expensive to triage | Section 4 rule 3: test output, attempts, one-line cause guess |
| H-11 | Over-engineering risk: building all templates and recipes up front | Recipes grow per area: Sonnet adds them when writing each class's first card, and the pilot covers only Calc, Writer and `executeMethod` |

### Revision 2: review of revision 1 (kept)

| # | Finding | Change |
|---|---|---|
| R-1 | Interface-level allowlist exposed whole interfaces; global `__ph_call` let plugin code skip the `Api` layer | Method-level generated allowlist, denylist, closure-only primitive, frozen intrinsics, fuzz tests |
| R-2 | 40–55 person-week estimate had no basis | Removed; sizing from measured rates |
| R-3 | Count-based target paid for unused members | Usage-weighted gate |
| R-4 | Expectations via Document Server were hard to automate | Document Builder |
| R-5 | File diffs between engines are noisy | Behavioural round trip |
| R-6 | Conversion work was spread over packages | One conversion layer |
| R-7 | Writer character positions were a hidden performance risk | Offset table and 50 ms budget |
| R-8 | Core Reflection cost was unmeasured | Absolute budgets; C++ fallback for hot members |
| R-9 | Static usage counting misses dynamic names | Runtime traces |
| R-10 | Lazy loading was over-engineering | Removed |
| R-11 | No read/edit source for permissions | Inventory column |
