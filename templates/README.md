# Templates (F7)

Used by `scripts/new_card.py` to create member stubs, tests and cards. Haiku never edits these files.

**Provisional contract** (until the generic bridge F3 is built and this section is confirmed by Sonnet):

- Every `Api*` class file receives a `bridge` object as a closure argument, never a global.
- `bridge.get(handle, property)` and `bridge.set(handle, property, value)` for properties.
- `bridge.call(handle, iface, method, argsArray)` for methods.
- `this._bridge` and `this._handle` are set by the class constructor (written by Sonnet, once per class).

Placeholders: `{{CLASS}}` class name, `{{NAME}}` ONLYOFFICE member name, `{{PROP}}` UNO property name,
`{{ID}}` row id from `docs/API_INVENTORY.csv`.

Member blocks are wrapped in `// BEGIN` and `// END` markers. The generator only appends a block that is not
already present, so re-running it never overwrites a card's work.

Tests are plain `var TEST = {...}` objects. The runner (F5) loads them into a fixture document and compares
`value` with `tests/expected/<id>.json` (F6). Expectations are never written by a card.
