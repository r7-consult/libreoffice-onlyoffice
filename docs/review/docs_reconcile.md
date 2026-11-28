# Docs reconciliation, 2026-11-28: change summary

Scope: C:\Oracle\plugin-host. No git add/commit/push. C:\Oracle\lo not touched. docs/REVIEW_2026-11-28.md not touched.

## 1. Permission policy (option A)
- docs/DECISION_LOG.md rows 2 and 3: replaced "first-use prompt is the only approval path" with: no administrator pre-approval, no prompt in this version, option A stands; prompt wording and Russian strings deferred; added the required "Reconciled on 2026-11-28 by the assistant..." sentence. Why: the phrase was the assistant's proposal, contradicting the user's option A.
- docs/adr/ADR-002-permission-policy.md, round-2 decision block: same content, plus the reconciliation sentence.
- docs/adr/ADR-002-permission-policy.md, open question 2 (about line 164): "Proposed answer: admin keys exist but ship empty" replaced by "Superseded: no administrator pre-approval (round 2)."
- docs/FULL_API_STATUS.md: D1 row changed from "option B, first-use prompt (proposed)" to "option A (accepted 2026-11-28)"; D1 sign-off row "ADR-002, ADR-003 are Proposed" changed to "Accepted 2026-11-28" (ADR headers say Accepted); next step 2 marked done.

## 2 and 3. Counts and inventory conflict
- docs/API_INVENTORY.csv: M0360 (GroupDrawings) and M0892 (Ungroup) read_edit read -> edit. Only that field changed. Why: docs/API_CLASSIFICATION_RULES.md says edit.
- Recount with Grep (count), after the change:
  - method rows: read 1373, edit 580, UNCLASSIFIED 110 (total 2063)
  - executeMethod rows: read 0, edit 0, UNCLASSIFIED 51 (total 51)
  - all rows: 2114
- docs/FULL_API_STATUS.md F1 row updated to these numbers (previous 1,372 / 582 / 109 did not match the file; 99 + 10 breakdown kept as a note). Next-step table row: "262 UNCLASSIFIED" replaced by 161 (110 + 51). The old 262 could not be reproduced from the CSV.

## 4. UNO mapping
- docs/UNO_MAPPING.md: every "Works" status replaced by "Written, not run" (explanations kept). Added line under the title: "Nothing in this table has been executed."

## 5. Linux and web host statements
- src/webhost.hxx header comment: Linux is WebKitGTK 4.1 (webhost_linux_webkitgtk.cxx); stub is macOS only, not built on Linux.
- src/webhost_stub.cxx header comment: rewritten as macOS placeholder; Linux is WebKitGTK; CEF remains fallback.
- docs/TEST_MATRIX_LINUX_MACOS.md: status line, browser-choice bullets (WebKitGTK primary, CEF fallback, gtk3 VCL embedding unverified, DECISION_LOG item 14), added rows 7 (kf5/qt5 VCL on X11) and 8 (native Wayland), both NOT VERIFIED; context-menu click line now points to src/menu_dispatch.cxx.
- docs/PROGRESS.md stage 10 row: WebKitGTK file noted as written, not compiled; stage 6 row: click handling is in menu_dispatch.cxx (not run).
- docs/PLAN_NEXT.md phase 6: decision recorded (WebKitGTK, CEF fallback); macOS out of scope.

## 6. Stale TODO
- src/context_menu.hxx line 32-33: TODO replaced by pointer comment to src/menu_dispatch.cxx.

## 7. README.md (new)
- Repository root. Three-sentence description, honest status paragraph (nothing compiled or run), key documents list (LINUX_BUILD.md marked as being written), AGPL-3.0 licence pointer, Node test commands. Note: tests/test_allowlist.js also needs the LibreOffice tree (LO_SRC).

## 8. .gitattributes (new) and .gitignore
- .gitattributes: created with the eleven lines specified.
- .gitignore: added `third_party/` (with a comment; the existing per-file lines are kept) and `*.oxt`. `dist/`, `build/` and `__pycache__/` were already present and were not duplicated.

## 9. Other stale statements
- Grep for "not yet called", "Not yet called", "(stage 10)", "webhost_linux_cef", "Proposed" (ADR headers), "NOT implemented": no contradicting statements remain outside history sections (the "stage 10" hits were fixed under items 5 and 6). docs/REVIEW_2026-11-28.md was not touched.
- docs/adr/ADR-002 round-1 body still says "option B" in the historical option text; left as history. Not changed.

## Not done
- Nothing skipped. Note: the instruction said to grep docs/ for the phrases; the Grep tool did not match some docs/ lines, so the final check used grep via Bash.
