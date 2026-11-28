# ADR-002: Permission policy for plugins without `hostPermissions`

- Status: **Accepted by the user on 2026-11-28** for the defaults listed under "User decisions" in the Decision
  section. Open items that the user has not answered are still listed under "Open questions".
- Date: 2026-11-28
- Supersedes: the prototype rule in `src/plugin_config.hxx` ("missing or empty means the plugin may do nothing").

## Context

1. The host gates three message types in `ProtocolAdapter::onPageMessage` (`src/protocol_adapter.cxx`):
   `executeMethod` needs `document.read`, `callCommand` needs `document.edit`, `contextMenu` needs `menu`.
   The permission list comes only from the host-specific key `hostPermissions` in `config.json`. A missing or
   empty list means every gated request is refused with `PermissionDenied`.
2. ONLYOFFICE `config.json` has no capability declaration. None of the 38 plugins counted in
   `docs/API_USAGE.csv` declares `hostPermissions`, so today every one of them is refused at its first
   document call. Phase 4 of `PLAN_NEXT.md` and gate M4 of `PLAN_FULL_API.md` (three plugins run unmodified)
   cannot be met under the current rule.
3. The ONLYOFFICE keys that do exist describe *availability*, not *capability*:
   `EditorsSupport` (which editors), `isViewer` (also offered in view mode), `isSystem` (starts without UI),
   `isVisual`, `initDataType`, `initOnSelectionChanged`, the variation `type` (panel, window, background).
   No key says "this plugin only reads". In ONLYOFFICE itself a plugin that can run `callCommand` can edit.
4. Real usage (`API_USAGE.csv`) is edit-heavy: `GetDocument`, `CreateParagraph`, `InsertContent`, `AddText`,
   content-control insertion, `PasteText`, `PasteHtml`, `InputText`, `AddContextMenuItem`. Most useful plugins
   need `document.edit`; several need `menu`.
5. `PLAN_FULL_API.md` section 2.2 adds a second, independent layer: a generated per-(interface, method)
   allowlist and a fixed denylist (`XStorable`, `XComponentLoader`, dispatch, scripting, shell, file access,
   service manager, configuration). Permissions decide *whether* a plugin may call the bridge at all;
   the allow/denylist decides *what* the bridge can reach.
6. Plugins are web pages. Their network access (for example `dall-e`, `clipdrop`) is not a host permission
   today; the WebView gives each plugin its own origin and profile.
7. `PLAN_NEXT.md` recommends option B with C choosing the requested permissions, and the grant stored in the
   plugin's data directory. `pluginDataDir()` is the WebView2 user data folder (browser profile).

## Decision

**User decisions, 2026-11-28 (these override the text below where they differ):**

- **Default for plugins without `hostPermissions` is option A**: they get `document.read`, `document.edit` and `menu`
  without a prompt. Network access is not part of A; it is a separate permission (see the open questions).
- **"Allow read only" is offered**, per executeMethod name, using the read|edit column of `API_INVENTORY.csv`.
  Names classified `edit` (for example `PasteText`) are hidden under "read only" once the column is verified.
- **Ask again only when more is requested**: a larger request set or a new permission. Under A this means a
  plugin that declares more than the three existing permissions, or a new permission name.
- **Storage is host-owned**, in the host profile, as item 3 below describes.
- **Network access is a separate permission** (a fourth name, decided by a later ADR before it is implemented).
- **Denylist and default-deny without a user are unchanged.** A does not weaken them.

Consequence the user accepted: a plugin without a declaration can edit the document on its first use with no prompt.

Where the text below says "Option B" and "ask once", it applies to plugins that declare `hostPermissions` and
to administrators who switch the default back to B.

**Option B, with C used only to narrow and explain the request.** In detail:

1. **Request set.** For each plugin the host computes the set of permissions it will ask for:
   - If `config.json` declares `hostPermissions`, the request set is exactly that list (declaring narrows,
     it never grants).
   - Otherwise the request set is derived (option C): `document.read` and `document.edit` for the editors
     listed in `EditorsSupport`, plus `menu`. The derived set is *not* trusted as a security statement; it
     only limits the editors and drives the wording of the prompt.
   - `isSystem: true` or a background variation marks the plugin as "runs without a window"; the prompt says so.
2. **Ask once, on first gated use.** The first gated message of a plugin with no stored answer opens one native
   dialog naming the plugin (name, GUID, version, install source) and listing the request set in plain words.
   Answers: **Allow**, **Allow read only** (grants `document.read` only), **Deny**. The pending request waits
   for the answer; the command watchdog does not run while the dialog is open. Closing the dialog is Deny for
   this session only (not remembered).
3. **Remember.** The answer is stored by the host, not by the plugin: one host-owned file in the host's
   profile folder, keyed by GUID, holding the granted set, the request set it answered and the package hash.
   It is **not** stored in `pluginDataDir()`, because that folder is the plugin's browser profile.
   The entry is deleted when the plugin is removed.
4. **Ask again** only when the request set grows (new editor, new permission) or the package's GUID is
   installed from a different source. A version update with the same or a smaller request set keeps the grant.
5. **Revocable.** The plugin manager shows each plugin's grant and lets the user reset it (next use asks again).
6. **Default deny without a user.** Headless runs, the F5 test runner and unattended starts never show a
   dialog; an unanswered plugin gets nothing. Tests and WP13 smoke scripts pre-seed grants from a fixture
   file passed by the runner (`PLUGINHOST_TEST_MODE=1` only).
7. **Administrator override.** A LibreOffice configuration key (extension schema, lockable like other xcu
   settings) may pre-grant or block by GUID, and may disable prompting (then: only pre-granted plugins run).
   Admin policy wins over the user's answer. What an organisation puts there is its decision.
8. **Denylist is outside the permission model.** No permission, no grant, no admin key and no future
   permission name can enable a denylisted interface or method (see Consequences, denylist).
9. Permissions keep the current names: `document.read`, `document.edit`, `menu`. New names are added only by an
   ADR, never by a factory card.

## Options considered

### A: missing `hostPermissions` means all permissions

- For: zero friction; every ONLYOFFICE plugin behaves as in ONLYOFFICE; trivial to implement.
- Against: every installed plugin, including one installed by mistake or from a hostile catalogue entry, gets
  silent edit access to every open document and can add context-menu items that look like host commands.
  The permission layer becomes decoration: only plugins written for this host (which declare the key) would
  ever be restricted, which is the inverse of the risk. With a marketplace (`OO-MKT`) this is the weakest
  position available.
- Verdict: rejected. Kept only as an *administrator* choice (pre-grant by GUID or by policy), not as a default.

### B: ask once per plugin, remember the answer

- For: the user sees and decides before a plugin touches a document; one click makes an unmodified ONLYOFFICE
  plugin work, so compatibility is kept; the decision is per plugin, revocable and auditable; matches what
  users know from browsers and phones.
- Against: one extra dialog per plugin; users may click Allow without reading (consent fatigue); needs a
  native dialog and a grant store (both small, Sonnet work in WP11); a dialog on first use can interrupt a
  command, so the watchdog must pause.
- Verdict: chosen.

### C: derive permissions from ONLYOFFICE config keys

- For: no dialog; uses data that already exists in every package.
- Against: the keys describe where a plugin is offered, not what it does. `isViewer` does not mean read-only;
  nothing in the config distinguishes a translator that edits from a counter that only reads. A derived
  "read only" would break most real plugins (Context 4); a derived "edit" is option A in disguise. Static
  scanning of plugin code is not reliable either (dynamic `executeMethod` names, plan R-9).
- Verdict: rejected as a grant mechanism; used inside B to narrow by editor and to word the prompt.

## Consequences

### Security

- No plugin reaches the document without an explicit human or administrator decision. Today's fail-closed
  behaviour is kept for every unattended path.
- The grant is coarse: `document.edit` is all-or-nothing within the allowlist. A plugin the user allowed can
  still damage or read the whole document. This is the accepted residual risk; finer permissions
  (for example per area) are not worth their prompt cost now.
- "Allow read only" is real only if `executeMethod` names are classified by read/edit. Today every
  `executeMethod` needs only `document.read`, but names such as `PasteText`, `PasteHtml`, `InputText`,
  `AddContentControl`, `InsertAndReplaceContentControls`, `RemoveContentControls`, `SearchAndReplace` edit the
  document. **Required change:** the gate must use the `read|edit` column of `API_INVENTORY.csv` per
  `executeMethod` name, not the message type. Until that column exists, "Allow read only" must not be shown.
- Context-menu items can imitate host commands. The `menu` permission stays separate, and the host prefixes
  or groups plugin items so they are recognisable (Sonnet, WP8).
- Network access of the plugin page is not covered by this ADR (open question 3).
- The grant file is host data; a plugin page cannot write it. A plugin package that ships a grant file has
  no effect, because the host reads grants only from its own folder.

### Compatibility with real plugins

- All 38 plugins in `API_USAGE.csv` can run unmodified after one Allow. With Deny or Allow read only, edit
  calls fail with `PermissionDenied`, as today; plugins that do not handle errors will look broken. That is
  the user's choice and is logged.
- Plugins that write `hostPermissions` (host-aware plugins, our fixtures) ask for less and get a smaller
  prompt. Declaring the key no longer grants by itself; test fixtures must use the pre-seeded grant file.
- `isSystem` and background plugins start without UI; their first gated call shows the dialog out of context.
  The prompt names the plugin and says it has no window. Acceptable; rare in the counted set.
- Code impact (Sonnet, WP11): `PluginConfig` must record whether `hostPermissions` was present (today an absent
  and an empty key look the same); `hasPermission` asks a new grant store; a dialog service; a pause hook in
  the command watchdog; an admin schema key; a fixture loader for tests.

### Denylist (`PLAN_FULL_API.md` section 2.2)

- Because B makes `document.edit` common, the denylist is the main barrier against escaping the document. It
  stays fixed, fail-closed and **not grantable**: no permission name, user answer or admin key enables it.
- The allowlist generator (F3) and card checker (F9) already refuse denylisted methods; this ADR adds no
  exception.
- ONLYOFFICE methods that need a denylisted capability (for example `GetFileToDownload`, which needs a
  storable/export path, or anything that opens files or URLs) are **not** served through the generic bridge.
  Each is either WONTFIX or a dedicated host-mediated operation implemented in C++ outside the bridge, with its
  own permission name and its own prompt, added by a later ADR. A factory card that hits one escalates.

## Open questions (user decisions)

1. **Default for plugins without `hostPermissions`: ANSWERED, option A** (see "User decisions" above).
2. **Administrator policy**: still open. Should the shipped configuration allow pre-granting by GUID and disabling
   the prompt? Which plugins, if any, ship pre-granted? Proposed answer: admin keys exist but ship empty.
3. **Network access: ANSWERED, a separate permission**, named later by its own ADR. Not implemented yet.
4. **"Allow read only": ANSWERED, offered per method** (see "User decisions" above).
5. **Re-prompt trigger: ANSWERED, only when more is requested.** A version update with the same or smaller
   request set keeps the grant.
6. **Grant store location: ANSWERED, host-owned file.** This deviates from `PLAN_NEXT.md`, which must be updated.
7. Prompt wording and localisation are still open; to be written with the UI owner.

## Decisions (round 2, 2026-11-28)

- No administrator pre-approval in this version. Installation and use must stay easy. The first-use prompt is the only
  approval path (see `docs/DECISION_LOG.md`, item 2).
- Prompt language: English strings in one resource table now. A Russian translation is added before the first pilot user.
