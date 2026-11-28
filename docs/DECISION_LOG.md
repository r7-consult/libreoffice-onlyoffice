# Decision log

Decisions taken by the user, with the code or document each one changed. Newest first.

## 2026-11-28 (round 2 of questions, answers from the user)

| # | Question | Decision | Where it is applied |
|---|---|---|---|
| 1 | Test switches in release builds | Compile-time only. CMake option `PH_TEST_HOOKS`, OFF by default. Without it, `PLUGINHOST_API_DIR` and `PLUGINHOST_TEST_SERVICE` are ignored. | `src/CMakeLists.txt`, `src/command_engine.cxx` (`jsDirectory`), `src/test_runner.cxx` (`testServiceEnabled`) |
| 2 | Administrator pre-approval of plugins | **Not at all.** Installation and use must be easy. No administrator pre-approval. No prompt in this version (option A): plugins that declare no `hostPermissions` get `document.read`, `document.edit` and `menu`. Confirmed by the user on 2026-11-28 (the earlier text proposed a first-use prompt that the user did not choose). | `docs/adr/ADR-002-permission-policy.md` (decision block) |
| 3 | Language of the permission prompt | The prompt wording and the Russian strings are deferred until a prompt is built. The user's round-2 answer was English strings now, Russian before the first pilot user; no prompt exists under option A, confirmed by the user on 2026-11-28. | ADR-002 (decision block). Prompt not built. |
| 4 | Counsel review | Internal use only for now. Counsel reviews `docs/LICENSING.md` before any distribution outside the company. | `docs/LICENSING.md` |
| 5 | Classification of UNCLASSIFIED rows | UNCLASSIFIED is treated as **edit** by the host. Read-only mode refuses it. A member is moved to read only after a manual review. | `docs/API_CLASSIFICATION_RULES.md` (revision 2026-11-28) |
| 6 | Does `ForceRecalculate` count as an edit? | **No.** `GetStartPage`, `GetEndPage`, `ApiDocument.GetPageCount` → read. `ApiDocument.ShowComment` → UNCLASSIFIED. Content-changing methods that call it stay edit. | `docs/API_INVENTORY.csv`, `docs/API_CLASSIFICATION_RULES.md` |
| 7 | `Asc.bridge` and the `Api` shim | Run side by side. Remove the shim after the 10-card pilot passes. | `src/command_engine.cxx` (`installBridge`) |
| 8 | Platform after Windows | **Linux only** (macOS out of scope for now). The codebase is being prepared for a Linux host. | `src/paths.cxx` (Linux module directory, data folders, file URLs), `src/plugin_config.cxx` (portable `scanPlugins`), `src/CMakeLists.txt` (links `dl` on non-Windows), `tests/run_member.py` (Linux soffice and default path) |
| 9 | Structs, `Any`, out-parameters | Still refused in the first version. Add one only when a pilot member needs it. | `docs/UNO_MAPPING.md` |
| 10 | Who compiles, and where | The Linux host, after the full code is complete. | `docs/FULL_API_STATUS.md` (next steps) |
| 11 | Who runs Document Builder for expectations (F6) | The Linux host, after the full code is complete. | `docs/FULL_API_STATUS.md` (next steps) |
| 12 | Distribution | Internal only for now. | This log |
| 13 | Pilot members | The ten shim methods already mapped in `docs/UNO_MAPPING.md`. | `docs/UNO_MAPPING.md` |
| 14 | Linux web host | **WebKitGTK** (webkit2gtk-4.1) in a GtkPlug on the X11 window, CEF as fallback. Approved 2026-11-28. The embedding check on the Linux host decides it. | `src/webhost_linux_webkitgtk.cxx`, `src/CMakeLists.txt` |

Status of this round: items 1, 5, 6, 7, 8 and 14 are implemented in source (not compiled). Items 2, 3 are recorded in ADR-002 and
the prompt is not built yet. Items 4, 9–13 are recorded only.

## Open (not decided)

- ADR-002: the wording of the prompt (after item 3 is built).
- Linux packaging: the manifest and `pluginhost.components` still name `pluginhost.dll` and `windows_x86_64`.

## 2026-11-28 (round 1)

Answers in `docs/adr/` (ADR-002, ADR-003), the classification rules and `docs/FULL_API_STATUS.md`. The decision that the
permission default is option A came from the user (not the recommended B).
