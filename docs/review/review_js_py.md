# Review: JavaScript and Python in C:\Oracle\plugin-host

All five Node tests pass after the edits (test_bridge now 21 checks, was 14). py_compile passes for every .py file in tests/ and scripts/.
run_member.py was exercised only on its exit-2 paths (no LibreOffice, no uno); nothing else could run here (no LO, no C++ compiler).

## Fixed (definite defects)

### extension/js/bridge.js
1. Denylist matched the full interface name; C++ matches the simple name (text after the last dot).
   With full UNO names, `com.sun.star.frame.XComponentLoader` was not caught by the exact list, and the
   substring pattern also ran against package parts. Now both use the simple name, identical to `lastComponent()` in bridge_native.cxx.
2. `get` and `set` called `String(prop)` twice (check, then send). An object with a changing toString could pass the check and send
   another name. Now converted once.
3. Handles in `get` results and inside arrays were not recorded. C++ `toJson` registers a handle for every interface value (property
   reads, sequences), so JS refused them later as "invalid or released handle" and never released them on dispose. `track` now walks arrays (depth 32) and `get` tracks too.
4. Argument limit counted UTF-16 units, the host counts UTF-8 bytes: JS passed, C++ refused. Now counted in bytes.
   A non-number, NaN, negative, zero or Infinity maxArgBytes now falls back to 4 MB (was: effectively unlimited for "abc"/NaN).
5. Native result handling: a non-string result (number, object) went through JSON.parse and was returned as if valid; a malformed JSON result
   threw a raw SyntaxError. Now "native returned a non-string result" / "native returned invalid JSON". null/undefined still give null.
6. Prototype pollution: allow/props/policy fields were read with plain property access, so an `Object.prototype.Foo = {...}` made
   `Foo` look allowlisted in JS (C++ would still refuse). Now own-property reads only.

### tests/run_member.py
- `import uno` failure raised a traceback (exit 1) instead of exit 2. Now checked before soffice starts: "CANNOT RUN", exit 2.
- soffice was started with the default user profile: a running LibreOffice of the same user takes over the command line and exits, so we
  connect to an instance without PLUGINHOST_TEST_SERVICE. Now a private profile (-env:UserInstallation, temp dir, removed afterwards).
- Office was never terminated on exceptions (only the two explicit paths). Now try/finally, per-test generic exception handling (UNO errors fail one test, others continue).
- Bad PLUGINHOST_TEST_PORT gave a traceback; now exit 2. Card without Tests line is exit 1 (a bad card is a failure, not "cannot run").
- os.add_dll_directory(LO_PROGRAM) on Windows. Docstring no longer claims it connects to an already running office.

### scripts/check_card.py (a Haiku card could bypass it in several ways)
- **Run line was executed from the card text** (`Run: python -I -c "import sys; sys.exit(0)"` gave PASS). Now the Run line must be exactly
  `python -I tests/run_member.py --card <this card>`, and the checker rebuilds the command itself (sys.executable, timeout 900 s).
- Path checks used raw `startswith`: `./tests/x`, `tests\x`, `extension/../tests/x`, `Tests/x` (Windows is case-insensitive), absolute paths
  and drive letters passed. Now normalised (posix normpath, reject `..`, absolute, drive, `:`, glob chars), compared case-insensitively.
- Edit scope is now a positive rule: only `extension/js/api/**/*.js`. `extension/js/bridge*.js`, `bridge_allowlist.json`, `api_shim.js` were not protected at all; now also in PROTECTED_PREFIXES.
- git check failed open: if git is missing or exits non-zero, stdout was empty and "unchanged" passed. Now an error is a FAIL. Uses
  `--literal-pathspecs` (a Tests line such as `:(exclude)tests` could neutralise the pathspec). It now watches all protected files (tests/ incl. run_member.py and expected/, templates, scripts, src, docs tables, bridge files), not only the listed test files. `__pycache__` ignored.
- Tests line could point to any file or to other members' tests. Now each test must be under tests/members/, exist, and each member `Class.Name` must have `.../Class/Name.js`.
- Members: 0 members passed; `;`-separated members counted as one. Now 1..10, each `Class.Name`, class must be an edited class file. Duplicate fields (last one wins in both parsers) are a FAIL.
- Exit code 2 from run_member.py (cannot run here) is now BLOCKED, not FAIL, as the docstring says.
- Denied names: added spacing/escape variants (`eval (`, `Function(`, `__proto__`, `.constructor`, `import(`, `require(`, `Reflect`). Static checks stay best effort (string concatenation gets past them).

### scripts/new_card.py
- `--class ../../bridge` resolved to extension/js/bridge.js and would have appended member blocks to the bridge. Class, NAME, PROP, METHOD, IFACE are now identifier-validated (they are pasted into JS string literals and file names); duplicates in --items refused.
- Inventory lookup ignored the row kind: names that exist as both method and executeMethod (SearchAndReplace, ApplyTheme, ...) got the wrong row id. Now by kind.
- "BEGIN" marker check was a substring test (`GetValue` hid `GetValueEx`) and the executeMethod marker was checked for all kinds. Now whole-line, kind-aware.

### scripts/inventory.py
- os.path.getsize could raise outside the try (broken symlink). Directory walk is sorted so first_source is reproducible.

### templates/README.md
- Documented: interface args must be `{"$h": n}`, null receiver, property-name rule, simple-name denylist, and the test_execute limitation below.

### New tests (tests/test_bridge.js)
Full-name denylist, Object.prototype pollution, single String() conversion, non-string/invalid-JSON/null results, handle tracking from get and arrays plus double release and null release, UTF-8 byte limit, bad maxArgBytes.

## Found, not fixed

1. **JS vs C++ (remaining differences)**
   - C++ `get`/`set` have no property allowlist. JS `props` exists but the host never passes it (command_engine.cxx), so in production any identifier-shaped property is readable/writable under canRead/canEdit. Decide whether the host should load a property list.
   - C++ result size is unlimited, same as JS (only argument size is limited).
   - Method name containing a dot: C++ key is `iface + "." + method`, so iface `com.sun.star.container` + method `XNameAccess.getByName` finds the allowlist key; it then fails at "method not found" (no bypass, JS refuses it earlier). Use a nested map or reject "." in method.
   - Null receiver: same in both (root, handle 1, call only). If the root is released, JS still lets null through; C++ then errors. Fine.
2. **src/bridge_native.hxx vs .cxx**: header declares `std::string opSet(...)` and `std::string opRelease(...)`; the .cxx defines both as `void`. Compile error.
3. C++ `resolve`/`opRelease`: `static_cast<int64>(double)` of a huge or NaN number is undefined behaviour; add a range check.
4. **Stale bridge across commands**: the engine is per plugin and `__ph_bridge` uses the global `g_activeBridge`. A plugin can stash `Asc.bridge` of command 1; in command 2 the old bridge's `live` set still passes JS checks and its numbers hit the NEW session (numbers restart at 2). Same plugin, same permissions, so low impact, but the host should call `Asc.bridge.dispose()` at the end of each command (or tag sessions with an id).
5. Plugin code shares the realm with bridge.js: it can replace Set.prototype.has, JSON.stringify, etc. and persist that in its own engine. Only the JS layer is affected; C++ validates everything again. Plugin code cannot reach `native` or the policy (closure only; `Object.keys(bridge)` has no extra members; bridge.js is strict). `bridge_boot.js` functions are sloppy mode but are not reachable from plugin code.
6. `__ph_call` is a global in plugin code and is not permission gated; callCommand needs document.edit so this is covered at the protocol level. The `__test__` plugin has no permissions, so bridge calls from run_member tests are always refused (canRead/canEdit false); only Api shim tests can pass.
7. **run_member.py contract**: it compares the whole `{"value": ...}` object with tests/expected/<id>.json, templates/README.md says "compares `value`". Pick one and write it down before the F6 expectations exist. `uno.invoke(runner, "run", ...)` on an XInvocation object is unverified (TEST_RUNNER.md says so). Windows: pyuno of the LO bundled Python version must match the Python that runs the script (python 3.14 here will not import it: exit 2). Windows soffice.exe is a launcher: terminate() on the failure path may leave soffice.bin; desktop.terminate() handles the normal path.
8. **templates**: `test_execute.js` uses `Asc.plugin.executeMethod`, which does not exist in the command engine (`Asc` = {scope, bridge}) so it cannot run in run_member. `member_execute.js` uses `this._bridge` inside `executeTable[...] = function`, where `this` is executeTable unless the dispatcher binds it.
9. check_card cannot see commits: a card that commits changes to protected files looks "unchanged" to `git status`. Compare against a base ref if cards may run git commit.
10. new_card.py `--setup`, `--arg`, `--readback` are free JavaScript pasted into the generated tests (the tests are then "protected" only after they are committed).
11. api_shim.js (read only): `SetValue([1,2])` (1-D array) is sent unwrapped; `GetValue` throws if native returns null. Not bridge-security relevant.
12. test_allowlist.js checks method names with a regex over the whole IDL file (comments would match). The 15 entries are real in the IDL; passes.
