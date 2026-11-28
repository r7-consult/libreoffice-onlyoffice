# Templates (F7)

Used by `scripts/new_card.py` to create member stubs, tests and cards. Haiku never edits these files.

**Contract** (implemented in `extension/js/bridge.js`, tested by `tests/test_bridge.js`; the native side is F3 C++, not yet built):

- Every `Api*` class file receives a `bridge` object as a closure argument, never a global. `createBridge(native,
  policy, rootHandle)` returns it frozen; the native primitive stays inside the closure.
- `bridge.get(handle, property)` and `bridge.set(handle, property, value)` for properties.
- `bridge.call(handle, iface, method, argsArray)` for methods; returns the native JSON value as is.
- `bridge.callHandle(handle, iface, method, argsArray)` for methods that return an object; returns the new handle number.
- `bridge.release(handle)`, `bridge.liveCount()`, `bridge.dispose()`.
- An argument of an interface type is passed as `{"$h": handle}`, not as the bare number and not as an `Api*` object
  (src/bridge_convert.cxx accepts only that shape). `null` is a null interface. Results are the same shape: an
  object, also inside an array, comes back as `{"$h": n}` and is recorded by the bridge, so it can be used and released.
- A `null` receiver in `bridge.call(null, ...)` means the document root (handle 1). `get`, `set` and `release` need a
  real handle.
- Property names for `get` and `set` are letters and digits only (no underscore), at most 64 characters. Interface
  names in `call` are full UNO names such as `com.sun.star.sheet.XSpreadsheet`; the allowlist is keyed the same way.
- `test_execute.js` calls `Asc.plugin.executeMethod`, which exists in the plugin page but not in the command engine
  that `tests/run_member.py` uses (there `Asc` holds only `scope` and `bridge`). Until the executeMethod table is
  reachable from the engine, an execute test cannot run in the runner.
- Denied interfaces and methods are refused in JS before native runs. The same list must be enforced again in C++.
  Both sides match the denylist on the simple interface name (the part after the last dot).
- `this._bridge` and `this._handle` are set by the class constructor (written by Sonnet, once per class).

Placeholders: `{{CLASS}}` class name, `{{NAME}}` ONLYOFFICE member name, `{{PROP}}` UNO property name,
`{{ID}}` row id from `docs/API_INVENTORY.csv`.

Member blocks are wrapped in `// BEGIN` and `// END` markers. The generator only appends a block that is not
already present, so re-running it never overwrites a card's work.

Tests are plain `var TEST = {...}` objects. The runner (F5) loads them into a fixture document and compares
`value` with `tests/expected/<id>.json` (F6). Expectations are never written by a card.
