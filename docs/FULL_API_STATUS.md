# Full API plan: status of the work that needs no binary

Date: 2026-11-28. Tracks `PLAN_FULL_API.md` (revision 3). "No binary" means nothing was compiled, no LibreOffice
or ONLYOFFICE build was run, and no UNO call was executed.

## Done here (source and scripts only)

| Item | Result | Where |
|---|---|---|
| F1 inventory | Names from call sites in the examples (see deviations). 2,114 rows: 2,063 methods and 51 `executeMethod` names. Signatures and read\|edit added from the `sdkjs` declarations for the 2,061 method rows with examples: 1,942 signatures; read 1,308, edit 491, UNCLASSIFIED 262 (119 with no declaration, 143 unclear). Heuristic, spot-checked only | `scripts/inventory.py`, `docs/API_INVENTORY.csv`, `docs/API_CLASSIFICATION_RULES.md` |
| D1 decisions (proposed) | Permission policy: option B, first-use prompt per plugin. Acceptable differences: MLR-1 to MLR-15. Both wait for user sign-off | `docs/adr/ADR-002-permission-policy.md`, `docs/adr/ADR-003-mapped-limited-rules.md` |
| F2 usage | 38 plugins from `sdkjs-plugins`, 139 names. 73 names are flagged `in_api_examples=no` (helpers bundled in plugins, such as `Tween` and `Decorate`) | `docs/API_USAGE.csv` |
| F7 templates and generator | Five member kinds, five test kinds, `new_card.py` with refusals. Generated class and test files parse with `node --check` (checked in a sandbox copy) | `templates/`, `scripts/new_card.py` |
| F9 card checker | Static checks: card fields, member limit, protected paths, tests unchanged vs HEAD, line limit, denied names. Tested on 4 failing cards and 1 clean card; the clean card returns BLOCKED because the runner is missing | `scripts/check_card.py` |

| Waivers (ADR-003) | `check_card.py` validates `tests/mlr_waivers.csv`: rule IDs are read from ADR-003, reason required, member must be on the card. Tested with valid and invalid waivers | `scripts/check_card.py` |
| Coverage file | `docs/API_COVERAGE.csv` created with all 2,114 rows at TODO, with the `mlr` column. Script refuses to overwrite it | `scripts/init_coverage.py`, `docs/API_COVERAGE.csv` |

Existing tests still pass: `tests/test_runtime.js` and `tests/test_api_shim.js` (both PASS).

## Not done, and why

| Item | Blocked by | Needs |
|---|---|---|
| F3 generic bridge (C++ and JS) | Compile | Sonnet, build machine |
| F4 conversion layer | Test run (performance budgets) | Sonnet, build machine |
| F5 headless runner `tests/run_member.py` | Running LibreOffice headless | Build machine |
| F6 expectations from Document Builder | Running Document Builder (licence now answered: inherited AGPL-3.0, see `docs/LICENSING.md`) | A machine that can run Document Builder, then Sonnet |
| F8 recipes | Each snippet must be tested | Sonnet, after F5 |
| D1 sign-off (ADR-002, ADR-003 are Proposed) | Open questions listed in the ADRs, and the Document Builder licence | The user |
| read\|edit for the 262 UNCLASSIFIED rows | Needs a judgement pass or a manual check | Sonnet, then spot-check; blocks the allowlist in F3 |
| Pilot (10 cards) | F5, F6, F8 | After the foundation |

## Deviations from the plan (record these in the decision log)

1. **F1 source.** The plan says names and signatures come from ONLYOFFICE sources. `sdkjs` is too large to clone here,
   so names come from call sites in `office-js-api` and `sdkjs-plugins`. Signatures and properties are not extracted.
   Impact: no signatures, no properties, no events. Fix: a later pass over the `sdkjs` declarations.
2. **`editors` is multi-valued** (for example `cell;word`), because a name such as `RGB` appears in every area.
   The plan said single-valued.
3. **Usage source.** 38 plugins from `sdkjs-plugins`, not the top 50 of the marketplace. The 99 % gate needs the
   marketplace list, which is not in these repositories.
4. **Usage is counted by name only.** Dynamic `executeMethod(name)` calls are not traced yet (plan R-9).
5. **`check_card.py` treats an untracked test as changed.** So tests must be committed before a card is run. The
   generator says this on every run.
6. **Templates are provisional.** They assume the bridge contract in `templates/README.md`, which is not built yet.

## Next steps, in order

1. Commit the inventory, usage, templates and scripts (after review).
2. Opus: decide the permission policy and the MAPPED-LIMITED rules (D1), and check the Document Builder licence.
3. Sonnet, on the build machine: F3, F4, F5, then F6, F8, then the read|edit classification.
4. Pilot (plan section 5), only after step 3.
