# Read-only review of C:\Oracle\plugin-host (checklist)

Scope: read-only. No file in C:\Oracle\plugin-host or C:\Oracle\lo was edited. No git add, commit or push. Method: Grep, Glob, Read, plus read-only shell listings. One shell call was blocked by the sandbox, so some counts were done with Grep.

## Task 1: stale statements

- [ ] docs/FULL_API_STATUS.md:30 says "ADR-002, ADR-003 are Proposed". Both ADR headers say "Accepted by the user on 2026-11-28" (docs/adr/ADR-002-permission-policy.md:3, docs/adr/ADR-003-mapped-limited-rules.md:3). Stale.
- [ ] docs/FULL_API_STATUS.md:11 "D1 decisions (proposed) ... Both wait for user sign-off". Same as above. Stale.
- [ ] docs/FULL_API_STATUS.md:31 says "read|edit for the 262 UNCLASSIFIED rows". docs/API_INVENTORY.csv has 161 UNCLASSIFIED rows in total (110 method rows, 51 executeMethod rows). 262 matches nothing.
- [ ] docs/FULL_API_STATUS.md:10 gives "read 1,372, edit 582, UNCLASSIFIED 109" for the 2,061 example rows. The file has read 1375, edit 578, UNCLASSIFIED 108 (method rows with in_api_examples=yes). Totals agree (2,061 rows counted in both); the three split values do not.
- [ ] docs/FULL_API_STATUS.md:10 "1,942 signatures": matches (1942 method rows with yes and a non-empty signature). "2,063 methods and 51 executeMethod": matches (2063 method rows, 51 executeMethod rows). "2,114 rows": matches.
- [ ] docs/FULL_API_STATUS.md:27 (F5) names PLUGINHOST_API_DIR as "override read by the engine" with no build gate. Code gates it at src/command_engine.cxx:169-170 (PH_TEST_HOOKS only). Partly stale.
- [ ] docs/TEST_RUNNER.md:18 says the service is created when PLUGINHOST_TEST_SERVICE is 1, without the PH_TEST_HOOKS gate. Code gates it at src/test_runner.cxx:26-34. The gate is recorded at docs/TEST_RUNNER.md:36 only. Partly stale.
- [ ] docs/TEST_RUNNER.md:28 still describes both switches as an open choice ("Decision needed before shipping"). Resolved at line 36. Historical wording.
- [ ] Not found anywhere: "not yet called by the engine", "TestRunner not written", "Linux and macOS are stubs" as a docs phrase. Only src/webhost.hxx:4-5 has "Linux and macOS are stubs (stage 10)".
- [ ] src/webhost.hxx:4-5: "Implementations: webhost_win.cxx (WebView2). Linux and macOS are stubs (stage 10)". Stale: src/webhost_linux_webkitgtk.cxx exists and is in CMake (src/CMakeLists.txt:64).
- [ ] src/webhost_stub.cxx:1-6: "Non-Windows placeholder (stage 10)... Planned: Linux (X11 / XWayland): CEF". Stale: Linux is WebKitGTK (docs/DECISION_LOG.md:22; src/CMakeLists.txt:63-64). The stub is no longer built on Linux (src/CMakeLists.txt:62-64 lists only webhost_linux_webkitgtk.cxx).
- [ ] docs/TEST_MATRIX_LINUX_MACOS.md:3 says "src/webhost_stub.cxx returns no browser" as the current Linux state. Stale.
- [ ] docs/TEST_MATRIX_LINUX_MACOS.md:9 "Linux X11 / XWayland: CEF ... (webhost_linux_cef.cxx, to be written)". Stale: no such file; WebKitGTK file exists (src/webhost_linux_webkitgtk.cxx).
- [ ] docs/PROGRESS.md:23 (stage 10) "webhost_stub.cxx (no browser yet)... CEF and WKWebView implementations NOT written". Stale for Linux.
- [ ] docs/PLAN_NEXT.md:123 "Linux: decide between CEF and WebKitGTK". Decided in docs/DECISION_LOG.md:22 (row 14). Not updated.
- [ ] docs/adr/ADR-002-permission-policy.md:164 "Proposed answer: admin keys exist but ship empty". Superseded by docs/DECISION_LOG.md:10 and ADR-002:174 (no administrator pre-approval).
- [ ] src/plugin_config.hxx:18-19 says "Missing or empty means the plugin may do nothing... Prototype policy". ADR-002:3 says this rule is superseded, and ADR-002:38-41 decides option A (default grant). Code still denies: src/protocol_adapter.cxx:52-61 (hasPermission returns false when the list is empty).
- [ ] src/command_engine.hxx:5 comment: "extension\js\api_shim.js" (Windows backslash path in a cross-platform file). Code builds the path with kPathSep (src/command_engine.cxx:164-177).
- [ ] src/host_service.hxx:28 comment: "parentHandle ... (HWND on Windows)". Cross-platform file; wording is platform-specific but qualified.
- [ ] src/plugin_panel.cxx:201 log text "stage 2 is Windows-only". Message is printed on Linux too; stale wording.
- [ ] src/paths.hxx:7, :24 and src/paths.cxx:15 are correctly qualified with "on Windows" / "on Linux". No stale claim found.
- [ ] src/context_menu.hxx:32 "TODO(stage 6 follow-up): click handling". Code exists: src/menu_dispatch.cxx:43-56 handles clicks. Partly stale.
- [ ] No "ForceRecalculate" in any edit list in src/. Mentions are only in docs (see Task 4, rows 6 and inventory).
- [ ] tests/run_member.py:22-23 docstring says the default LO_PROGRAM is "C:\Program Files\LibreOffice\program". Code also has a Linux default at tests/run_member.py:49. Docstring stale.
- [ ] docs/REVIEW_AND_PLAN.md:33 "Linux/macOS stub... Defer". Historical; still consistent with the plan, not with DECISION_LOG row 8.
- [ ] docs/PLAN_NEXT.md:37 "distpluginhost-windows-x64.oxt" (missing path separator, typo).
- [ ] docs/PROGRESS.md:3 cites C:\Oracle\lo\PLAN_PROTOTYPE_P1_HAIKU.md (outside this repo).
- [ ] ADR-001 is referenced (docs/PLAN_NEXT.md:133) but no docs/adr/ADR-001 file exists. Planned item, not a stale claim.

## Task 2: Linux packaging gaps (Windows-only items and their Linux counterpart)

- [ ] scripts/build.ps1: CMake/Ninja with third_party\tools paths and vcvars64 (build.ps1:1-3, 12-21). Linux needs build.sh: cmake, ninja or make, pkg-config for gtk+-3.0 and webkit2gtk-4.1 (src/CMakeLists.txt:65-67), output build/bin/libpluginhost.so (name per user note; not confirmed, see below).
- [ ] scripts/pack.ps1: writes dist\pluginhost-windows-x64.oxt (pack.ps1:8); copies build\bin\pluginhost.dll (pack.ps1:5, 15); substitutes platform "windows_x86_64" (pack.ps1:19); uses Compress-Archive (pack.ps1:23). Linux needs pack.sh: copy libpluginhost.so, substitute "linux_x86_64" in description.xml, zip, output dist/pluginhost-linux-x86_64.oxt.
- [ ] scripts/install.ps1: dist\pluginhost-windows-x64.oxt (install.ps1:6), unopkg.exe (install.ps1:7). Linux needs install.sh with unopkg (no .exe) and the same -env:UserInstallation=file:// form.
- [ ] scripts/uninstall.ps1: unopkg.exe (uninstall.ps1:6), file URL (uninstall.ps1:7). Linux needs uninstall.sh.
- [ ] scripts/env.ps1: reads config\versions.json, sets LO_PROGRAM = install_path\program (env.ps1:11). Linux needs env.sh, with a JSON reader (python or jq) because PowerShell is not available.
- [ ] scripts/gen_uno_headers.ps1: cppumaker.exe (gen_uno_headers.ps1:10), PATH for URE DLLs (gen_uno_headers.ps1:4). Linux needs gen_uno_headers.sh with LD_LIBRARY_PATH and the Linux SDK layout (docs/REVIEW_AND_PLAN.md:70 says the ure-link-lib rdb folder is Linux-only).
- [ ] scripts/run_all.ps1: python.exe (run_all.ps1:7), backslash paths (run_all.ps1:10), PLUGINHOST_PLUGINS_DIR with "\plugins" (run_all.ps1:10). Linux needs run_all.sh with an X11 display (run_all.ps1:3 says an interactive desktop is needed).
- [ ] extension/META-INF/manifest.xml:3: media-type "application/vnd.sun.star.uno-components;platform=windows_x86_64". Linux: platform=linux_x86_64. Verified: C:\Oracle\lo\desktop\source\deployment\misc\dp_platform.cxx:80 (windows_x86_64) and :96 (linux_x86_64, checkOSandCPU "Linux"/"X86_64"). The Linux platform string is linux_x86_64.
- [ ] extension/pluginhost.components:5 uri="pluginhost.dll". Linux must name the Linux library. Library name libpluginhost.so: NOT confirmed. My greps of C:\Oracle\lo found no `uri="lib...so"` example in .components files and no DLL-prefix logic in cppuhelper/source/shlib.cxx. The only .components uri examples are Java .uno.jar files (odk/examples). Verify on the build machine.
- [ ] extension/description.xml:7-8: placeholder UPDATED_SUPPORTED_PLATFORM, replaced only by scripts/pack.ps1:19 with windows_x86_64. Linux needs the same substitution with linux_x86_64.
- [ ] extension/*.xcu (Factories, ProtocolHandler, Sidebar, WriterWindowState, CalcWindowState): grep found no platform, dll or path strings. No Linux counterpart needed.
- [ ] .github/workflows/build-windows.yml: runs-on windows-latest (line 16), msiexec (lines 35-36), LibreOffice MSI (lines 32-34), Invoke-WebRequest (line 28), build/bin/*.dll upload (line 87), dist/*.oxt (line 88). Linux needs a second workflow (ubuntu) with apt packages libgtk-3-dev and libwebkit2gtk-4.1-dev, a LibreOffice deb or tarball SDK, and upload of libpluginhost.so and the Linux oxt. Repository has no Linux CI at all.
- [ ] config/versions.json: tools have only Windows URLs and "local" paths (lines 5-6); libreoffice A and B have null install_path and sdk_path (lines 13-14). Linux entries missing (CMake and Ninja Linux builds, webkit2gtk-4.1 version, LibreOffice Linux install and SDK paths).
- [ ] .bat files: none in the repository (checked the full file list).
- [ ] No Linux build or quick-start document exists (see Task 3).

## Task 3: repository hygiene

- [ ] .gitignore:2-5: "third_party/tools/", "third_party/quickjs-ng.tar.gz", "third_party/webview2/*.nupkg", "third_party/webview2/pkg/" are ignored. The rest of third_party/ is not ignored. git status shows "?? third_party/" as a whole (untracked), including third_party/quickjs-ng (MIT source).
- [ ] .gitignore: build/ (line 8), dist/ (9), logs/ (10), profile-A/ and profile-B/ (11-12), __pycache__/ (20). Covered. tests/__pycache__/*.pyc files exist on disk and are ignored.
- [ ] .gitignore: *.oxt not ignored. Only dist/ covers the package output. No .gitignore pattern for *.oxt elsewhere.
- [ ] .gitignore has *.user and .vs/ (16-17) and .claude/settings.local.json (15). No issue found.
- [ ] LICENSE present at repository root (AGPL-3.0 text, 35184 bytes). It uses CRLF line endings (verified bytes: \r\n). docs/LICENSING.md:63-66 says it was copied from the first 661 lines of the ONLYOFFICE copy and must be compared with gnu.org before release.
- [ ] No copyright or SPDX header in any source file under src/, extension/, scripts/, tests/ (grep for Copyright, SPDX, License). Only docs/LICENSING.md, scripts/init_coverage.py, extension/content/runtime.js:23 (a sentence about AGPL sources) and config/versions.json mention licences.
- [ ] README at repository root: does not exist. Only templates/README.md exists.
- [ ] Top-level build or quick-start document: none. Closest is docs/PROGRESS.md:10 (a build machine checklist).
- [ ] .gitattributes: does not exist. No line-ending rules.
- [ ] CRLF vs LF: only three files contain CR (LICENSE, docs/API_COVERAGE.csv, docs/API_USAGE.csv). All other text files, including all scripts/*.ps1, scripts/*.py, src/*, extension/*, tests/*, and the workflow, are LF. Shell scripts for Linux will be LF already. Git on Windows with core.autocrlf=true would warn "LF will be replaced by CRLF" on these LF files because there is no .gitattributes.
- [ ] Non-ASCII characters: only in docs/DECISION_LOG.md, docs/UNO_MAPPING.md, docs/PLAN_NEXT.md, docs/PLAN_FULL_API.md (no non-ASCII in any .ps1, .py, .js, .cxx, .hxx, .json, .xcu, .components or .xml file). BOM check: not done.
- [ ] Large files: only docs/API_INVENTORY.csv is over 100 KB (2,115 lines). docs/API_COVERAGE.csv is 2,115 lines and is also tracked. Not a problem by size; the inventory is regenerated by scripts/inventory.py.
- [ ] Empty directories present on disk (not tracked by git): build/, dist/, logs/ (empty), and third_party/ content (untracked, see above).
- [ ] TODO/FIXME/XXX/HACK in src/: one hit, src/context_menu.hxx:32 "TODO(stage 6 follow-up): click handling". In extension/: no hits. In scripts/: none.
- [ ] Other "DRAFT" markers (not TODO): src/CMakeLists.txt:4 "DRAFT: not compiled yet"; scripts/run_all.ps1:3 "DRAFT: not run yet"; src/webhost_linux_webkitgtk.cxx:4 "DRAFT, NOT COMPILED".
- [ ] tests/run_member.py:187 uses os.add_dll_directory only on Windows (guarded). Fine.

## Task 4: DECISION_LOG.md rows (yes / partly / no)

- [ ] Row 1 (test switches compile-time only): PARTLY. Gated: CMakeLists.txt:29, 75-76; command_engine.cxx:169; test_runner.cxx:28. Not gated: PLUGINHOST_TEST_MODE at protocol_adapter.cxx:151; PLUGINHOST_PLUGIN at plugin_panel.cxx:122; PLUGINHOST_PLUGINS_DIR at paths.cxx:79. The decision names only PLUGINHOST_API_DIR and PLUGINHOST_TEST_SERVICE.
- [ ] Row 2 (no administrator pre-approval): PARTLY. ADR-002:174-175 states it. ADR-002:164 still proposes admin keys (stale). No admin mechanism in code.
- [ ] Row 3 (English prompt strings in one resource table, prompt not built): YES. No prompt code in src/ (grep "prompt" returned nothing). Docs say "Prompt not yet built".
- [ ] Row 4 (counsel review, internal only): YES. docs/LICENSING.md:48-51 and 3-5. No code effect expected.
- [ ] Row 5 (UNCLASSIFIED treated as edit, read-only mode refuses it): PARTLY. extension/js/bridge.js:170 refuses names that are not read or edit in the allowlist (effectively UNCLASSIFIED = refused). No "read only" mode exists in src/ (grep for "read only" and "readOnly" returned nothing), though ADR-002 lists "Allow read only" as a user decision.
- [ ] Row 6 (ForceRecalculate is not an edit): PARTLY. Rule text at docs/API_CLASSIFICATION_RULES.md:24-27 and inventory rows match for GetStartPage (M1608 read), GetEndPage (M1482 read), GetPageCount (M1556 read), ShowComment (M2002 UNCLASSIFIED), UpdateAllTOC (M2037 edit), ToFixed (M0164 edit), SetDropCap (M0328 edit). Mismatch: GroupDrawings (M0360) is read in docs/API_INVENTORY.csv but docs/API_CLASSIFICATION_RULES.md:27 says it stays edit. Ungroup (M0892) is read but the same line says it stays edit.
- [ ] Row 7 (Asc.bridge and Api shim run side by side): YES. src/command_engine.cxx:298 (installBridgeScripts), :305 (installBridge), :402 (called in CommandEngine::run). extension/js/api_shim.js still defines Api.
- [ ] Row 8 (Linux only, macOS out of scope): PARTLY. Code is portable: src/paths.cxx:46-74 (dladdr), :90-99 (fileUrl Linux branch, no percent-encoding), src/plugin_config.cxx:85-88 (std::filesystem), src/CMakeLists.txt:72 (links dl), tests/run_member.py:49 (Linux soffice default). The else branch in src/CMakeLists.txt:62-68 is used for any non-Windows platform, including macOS. Packaging is still Windows-only (see Task 2); docs/DECISION_LOG.md:30 lists it as open.
- [ ] Row 9 (structs, Any, out-parameters refused): PARTLY, not verified. src/bridge_native.hxx:10 states the refusal. src/bridge_convert.cxx has explicit cases for scalars, enum, interface, sequence (lines 45-107). Grep found no STRUCT or ANY case and no "unsupported type" message. The default branch of the switch was not read.
- [ ] Row 10 (compile and test on the Linux host): YES (docs only). docs/FULL_API_STATUS.md next steps. No Linux build or packaging files yet (Task 2).
- [ ] Row 11 (Document Builder run on the Linux host): YES (docs only). docs/FULL_API_STATUS.md, F6. Nothing in code.
- [ ] Row 12 (internal distribution only): YES (docs only). docs/LICENSING.md; dist/ is empty.
- [ ] Row 13 (pilot members = the ten shim methods in UNO_MAPPING): YES. docs/UNO_MAPPING.md lists 10 shim methods; extension/js/api_shim.js defines 10 (ApiRange.SetValue, GetValue; ApiWorksheet.GetName, GetRange; ApiParagraph.GetText, AddText; ApiDocument.GetElementsCount, GetElement; Api.GetActiveSheet, GetDocument; lines 33-80). Allowlist has 15 entries (extension/js/bridge_allowlist.json), matching the 15 rows in UNO_MAPPING.md:28-41.
- [ ] Row 14 (WebKitGTK, CEF as fallback): PARTLY. Code: src/webhost_linux_webkitgtk.cxx exists (createWebHost at :433) and is compiled on non-Windows (src/CMakeLists.txt:64). No CEF code exists (fallback not implemented, not stated as implemented). Stale docs: docs/PROGRESS.md:23, docs/TEST_MATRIX_LINUX_MACOS.md:9, docs/PLAN_NEXT.md:123, src/webhost_stub.cxx:4-5.

Extra (not in the log, but relevant to ADR-002 user decision):
- [ ] ADR-002 user decision "default for plugins without hostPermissions is option A" (ADR-002:38-41): NO in code. src/protocol_adapter.cxx:52-61 returns false for an empty permission list, so such plugins are refused. docs/DECISION_LOG.md has no row for the option A decision (only the round 1 note at line 34-35).
- [ ] docs/DECISION_LOG.md:24 says items 1, 5, 6, 7, 8, 14 "implemented in source (not compiled)". Item 5 is partly, item 6 is partly (GroupDrawings and Ungroup mismatch), item 8 is partly, item 14 is partly for the CEF text. Items 1 and 8 need the qualifications above.

## Counts check (docs/API_INVENTORY.csv, read_edit column, Grep and shell)

- [ ] All rows: 2,114 (YES, matches docs). Method rows: 2,063 (YES). executeMethod rows: 51 (YES).
- [ ] Method rows, all: read 1,375, edit 578, UNCLASSIFIED 110 (total 2,063).
- [ ] Method rows with in_api_examples=yes (2,061): read 1,375, edit 578, UNCLASSIFIED 108. Docs say 1,372 / 582 / 109 (MISMATCH: read -3, edit +4, UNCLASSIFIED +1).
- [ ] Signatures on example method rows: 1,942 (YES, matches docs/FULL_API_STATUS.md:10).
- [ ] docs/API_COVERAGE.csv: 2,114 data rows, all state TODO (matches docs/FULL_API_STATUS.md:17).
- [ ] docs/API_USAGE.csv: 139 data rows (matches "139 names").
