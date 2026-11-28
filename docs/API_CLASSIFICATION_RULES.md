# API method classification rules (read_edit column)

Applies to rows with kind=method and in_api_examples=yes in docs/API_INVENTORY.csv.
Values: `read`, `edit`, `UNCLASSIFIED`. Source: method bodies in ONLYOFFICE sdkjs (shallow clone, not stored in this repo).

Rule:
- `edit` when the body calls a method that writes to the document model (names such as Set*, Add*, Remove*, Insert*, Delete*, Replace*, Apply*, Clear*, private _set/_add/_apply helpers), writes to history, or assigns document properties on `this` or on document objects.
- `read` when the body only gets values, or builds a detached object (new Api*/Asc.* wrappers, CreateSolidFill, RGB) without touching the document.
- `UNCLASSIFIED` when the body calls helpers whose effect cannot be told from the name, when no declaration was found in sdkjs, or when the same member is declared on several classes with conflicting classes.
- A Create* call is not evidence of an edit by itself; it counts only if the result is attached to the document through a write call.
- If a member is declared on several classes, the majority class wins; the signature is the most common parameter list.

Examples:
1. `ApiRange.SetValue(value)` calls cell-writing methods: edit.
2. `ApiWorkbook.GetActiveSheet()` returns a wrapper of the active sheet: read.
3. `Api.CreateSolidFill(color)` returns a new detached fill object: read.
4. `ApiWorksheet.AddShape(...)` stays UNCLASSIFIED: the body goes through helper constructors whose effect is not clear from the name.
5. `ApiParagraph.AddText(text)` adds a run to the paragraph: edit.

Classification is a static heuristic over the function body, not a proof. Review UNCLASSIFIED rows by hand.

## Revision 2026-11-28 (user decision)

- `ForceRecalculate` is removed from the edit list. It recalculates layout and computed values; it does not change document content.
- Reclassified: `ApiRange.GetStartPage`, `ApiRange.GetEndPage`, `ApiDocument.GetPageCount` from edit to read (their only write-like call was `ForceRecalculate`).
- `ApiDocument.ShowComment` from edit to UNCLASSIFIED: it changes the view through `Document.ShowComment`, and that is not clearly a content edit.
- Methods that change content and call `ForceRecalculate` stay edit (`UpdateAllTOC`, `GroupDrawings`, `SetDropCap`, `ApiDropCap.Clear`, `Ungroup`, `ToFixed`, `private_ApplyFrame`).
- UNCLASSIFIED is treated as edit by the host. Read-only mode refuses it. A member that should be readable is reviewed by hand and moved to read.
