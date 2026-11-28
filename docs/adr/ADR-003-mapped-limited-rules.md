# ADR-003: Rules for acceptable differences (MAPPED-LIMITED)

- Status: **Accepted by the user on 2026-11-28** (decision D1, part 2, of `PLAN_FULL_API.md`), except the
  Document Builder licence question, now answered (see "Open questions").
- Date: 2026-11-28
- Used by: factory cards (`PLAN_FULL_API.md` section 4, rule 4), card checker F9, `docs/API_COVERAGE.csv`.

## Context

A member is MAPPED-LIMITED when it is implemented but its observable result differs from the ONLYOFFICE
expectation (`tests/expected/<id>.json`) in a recorded, accepted way. Haiku must not judge differences, so the
acceptable ones are listed here once, by number. Anything not covered by a rule is an escalation.

## Decision

### How a rule is applied

1. A difference may be accepted only under exactly one rule below. One member may cite several rules if it has
   several independent differences; each difference names its own rule.
2. The rule's acceptance condition must hold **exactly**. A difference just outside a tolerance is an escalation,
   not a wider reading of the rule.
3. The card never edits tests or expectations. It records a waiver row in `tests/mlr_waivers.csv`
   (`member_id, rule, field, expected, actual, note`). F9 must be extended (Sonnet) to: accept a failing
   assertion only when a waiver row matches it; run the rule's machine check where the rule has one (column
   "Check" below: **M** = machine-checked by F9, **R** = needs Sonnet review of the card).
4. `API_COVERAGE.csv` gets state `MAPPED-LIMITED` and the rule IDs in column `mlr` (for example `MLR-1;MLR-6`).
5. Rule IDs are stable. A rule is never renumbered or reused; a withdrawn rule is marked "withdrawn" and kept.
   New rules are added only by an ADR amendment (Opus), never by a card.

### Rules

#### MLR-1 Unit rounding (M)
- **Difference:** a length, size, position or angle read back differs because LibreOffice stores it in its
  own unit (1/100 mm for lengths, 1/100 degree for angles) and ONLYOFFICE uses another (twips, EMU,
  half-points, points, mm, degrees).
- **Accept if:** the conversion goes through `_convert.js`, and `|actual - expected|` is at most one
  LibreOffice internal unit expressed in the source unit (for example 360 EMU, 1 twip, 0.01 mm, 0.01 degree).
  Values that are exactly representable in both units must round-trip exactly.
- **Record:** source unit, expected, actual, delta.

#### MLR-2 Floating-point results (M)
- **Difference:** a computed number (formula result, scaled value) differs in the last digits.
- **Accept if:** both are finite and agree to 15 significant digits; or both are integers and equal.
- **Record:** expected, actual, relative difference.

#### MLR-3 Document defaults (R)
- **Difference:** a property the test did **not** set (font name, font size, colour, spacing, style of a new
  paragraph, run or cell) reads back as the LibreOffice document default instead of the ONLYOFFICE default.
- **Accept if:** the value equals the default of the fixture document in LibreOffice, and the same property,
  when set explicitly by the test, reads back exactly (or under MLR-1).
- **Record:** property, ONLYOFFICE default, LibreOffice default, fixture name.

#### MLR-4 Environment-dependent values (M)
- **Difference:** a value comes from the machine or session: installed font list (`GetFontList`), user name,
  locale and language names, current date and time, file paths, application version.
- **Accept if:** type and format match the expectation (for example array of strings, ISO-like date string);
  values are not compared.
- **Record:** field, expected type/format, one sample actual value.

#### MLR-5 Generated identifiers (M)
- **Difference:** internal IDs (element IDs, comment IDs, content-control internal IDs, unique element IDs)
  have a different format or value.
- **Accept if:** same JS type as expected; unique within the document; stable for the session; looking the
  object up again by the ID returns the same object. User-visible IDs (for example a content-control `Id` or
  `Tag` the plugin set) are **not** covered and must match.
- **Record:** expected sample, actual sample.

#### MLR-6 Element indexing in Writer (R)
- **Difference:** `GetElementsCount` / `GetElement(i)` count differently from ONLYOFFICE because the fixture
  contains a construct LibreOffice represents differently.
- **Accept if:** the index rule in `_convert.js` is followed (top-level body blocks in document order; a table
  counts as one; content of frames, shapes, headers and footers is excluded), the difference is caused only by
  a construct named in the note (for example a LibreOffice section boundary, an empty paragraph LibreOffice adds
  after a table at the end of the body), and on fixtures without that construct the counts are equal.
- **Not covered:** block content controls (CORE, K-3), off-by-one errors, wrong element returned.
- **Record:** fixture, construct, expected count, actual count.

#### MLR-7 Special characters in extracted text (R)
- **Difference:** text returned by `GetText`, `GetSelectedText` and similar differs only in placeholder
  characters: field results versus field codes, anchors of footnotes, comments or inline objects, soft hyphen,
  non-breaking space or hyphen, the line-break and paragraph-end representation.
- **Accept if:** after removing or mapping only the characters listed in the record, the strings are equal.
  Letters, digits, punctuation and whitespace between words must be identical.
- **Record:** the character mapping used (code points), expected, actual.

#### MLR-8 List and numbering presentation (R)
- **Difference:** bullet glyph, bullet font, default indent and tab per level, label suffix spacing.
- **Accept if:** the list membership, list level and the computed number value of each item are equal (item 3
  is "3" in both, in the same format: decimal, letter, roman); only presentation differs.
- **Record:** property, expected, actual.

#### MLR-9 Number format codes (M)
- **Difference:** a Calc number-format code string reads back in LibreOffice's normalised form (for example
  `General` versus `Standard`, quoting, case of tokens).
- **Accept if:** for the test's sample values, the formatted display text is equal in the `en-US` locale.
- **Record:** expected code, actual code, sample values and display texts.

#### MLR-10 Built-in style names (R)
- **Difference:** a built-in style has a different programmatic name in LibreOffice.
- **Accept if:** the style-name table in `_convert.js` maps it, and the `Api` returns the ONLYOFFICE name. If the
  table has no entry, returning the LibreOffice programmatic name is accepted and recorded; custom style names
  must always match exactly.
- **Record:** ONLYOFFICE name, LibreOffice name, mapped yes/no.

#### MLR-11 Nearest enum value (R)
- **Difference:** an ONLYOFFICE enum value (underline style, border style, highlight colour, dash type,
  alignment variant) has no exact LibreOffice equivalent and is mapped to the nearest one.
- **Accept if:** the mapping is listed in the enum table of `_convert.js`; set-then-get returns the mapped
  value from that table; all values with an exact equivalent round-trip exactly.
- **Record:** set value, stored LibreOffice value, read-back value.

#### MLR-12 Theme and automatic colours (R)
- **Difference:** a theme or scheme colour, or "auto", comes back as a resolved RGB colour or as LibreOffice's
  automatic colour.
- **Accept if:** the resolved RGB equals the expectation's RGB; explicit RGB colours always match exactly.
- **Record:** colour identity lost (theme slot or auto), RGB.

#### MLR-13 Layout-derived values (R)
- **Difference:** values produced by layout (row heights after autofit, text width, line count, positions of
  floating objects, page count) differ because the layout engines differ.
- **Accept if:** within 5 % or 1 pt (whichever is larger) for lengths, and within one line or one page for
  counts, on fixtures that use only fonts installed on the test machine.
- **Record:** quantity, expected, actual, fonts of the fixture.

#### MLR-14 Order of unordered collections (M)
- **Difference:** a collection (all comments, all content controls, font list) comes back in a different order.
- **Accept if:** the ONLYOFFICE expectation does not depend on order (the members compared as a multiset are
  equal). If ONLYOFFICE returns document order, LibreOffice must too; that is not covered.
- **Record:** collection, ordering used.

#### MLR-15 Absent value and error text (M)
- **Difference:** `null` versus `undefined` for "no value"; the wording of an error message.
- **Accept if:** both sides mean "no value" (never `null`/`undefined` versus `""`, `0` or `false`); or both
  sides fail in the same way (both return the same failure value, or both throw) and only the message differs.
- **Record:** expected, actual.

### Differences that are NOT acceptable (always escalate)

A card that sees one of these stops work on the member, marks it `ESCALATE` (section 4, rule 3) and does not
change code to hide it.

1. Different return type or shape: number versus string, scalar versus array, wrong array dimensions, an
   object where `null` is expected or the reverse (beyond MLR-15).
2. Different user-visible text content: lost, extra or changed letters, digits, punctuation (beyond MLR-7).
3. The operation acts on the wrong target: another cell, paragraph, run, sheet or element; off-by-one indexes.
4. Wrong computed value: a list number, formula result or count outside MLR-2, MLR-8 or MLR-13.
5. A setter that is accepted but has no effect, or a getter that returns a constant default instead of the
   real value.
6. Side effects outside the target: neighbouring content, styles or document settings change.
7. Data not kept when the document is saved and reopened in ODF; data lost in OOXML (classify as CORE, K-3,
   by Sonnet; never MAPPED-LIMITED).
8. Undo: a `callCommand` takes more than one undo step, or a failed command leaves the document changed.
9. Throwing where ONLYOFFICE returns a value, or returning a value where ONLYOFFICE fails (beyond MLR-15).
10. Crash, hang, watchdog timeout, or a performance budget of the checker exceeded.
11. Any fix that would need a denylisted interface or method (ADR-002), or an edit of a test, expectation,
    template, recipe, allowlist, denylist or C++ file.
12. A difference that seems to fit a rule but exceeds its tolerance, or would need two rules for one value.
13. A difference in a member whose behaviour depends on content controls, plugin objects or OLE data
    (WP9, K-3): classify, do not waive.

## Consequences

- Haiku applies rules by ID; judgement stays with Opus (rule set) and Sonnet (review of **R** rules).
- Sonnet reviews every card that cites an **R** rule, in addition to the 1-in-10 spot check; this raises review
  cost for Writer formatting cards. **M** rules are checked by F9 alone.
- Required follow-up (Sonnet): F9 support for `tests/mlr_waivers.csv` and the **M** checks; an `mlr` column in
  `API_COVERAGE.csv`; index rule, style-name table and enum tables in `_convert.js` (F4).
- The usage-weighted gate counts MAPPED-LIMITED as covered. If more than 10 % of the usage-weighted calls end up
  MAPPED-LIMITED under **R** rules, Opus reviews whether a rule is too loose.

## Open questions (user decisions)

**User decisions, 2026-11-28:**

1. **Tolerances in MLR-1, MLR-2 and MLR-13: ANSWERED, kept as proposed.** Tighten them later only when
   real test runs give measured values.
2. **MLR-3 (document defaults): ANSWERED, accepted.** New content uses LibreOffice's default fonts and sizes.
   Each affected test is recorded as MAPPED-LIMITED with MLR-3.
3. **Document Builder licence (F6): ANSWERED 2026-11-28.** The project inherits the licence of its sources.
   Document Builder is AGPL-3.0 (`ONLYOFFICE/DocumentBuilder`), so expectations generated with it are treated as
   AGPL-derived. Running it to make expectations is allowed; shipping its binaries is not in scope. Open points for
   counsel are in `docs/LICENSING.md`. Desktop Editors runs are not covered by this answer.

## Decisions (round 2, 2026-11-28)

- Pilot members: the ten shim methods in `docs/UNO_MAPPING.md` (`docs/DECISION_LOG.md`, item 13).
- Classification of a member that has no sdkjs declaration or that is unclear: UNCLASSIFIED, which the host treats as edit
  (`docs/API_CLASSIFICATION_RULES.md`, revision 2026-11-28).
