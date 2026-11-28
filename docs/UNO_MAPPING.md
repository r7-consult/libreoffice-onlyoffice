# UNO mapping for the Api shim (first ten methods)

Each row maps one public `Api` method of `extension/js/api_shim.js` to the UNO calls the generic bridge makes.
Interface names are the **declaring** interface of the method, because `bridge_native.cxx` matches methods by
declaring class. The file `extension/js/bridge_allowlist.json` is built from this table. `tests/test_allowlist.js`
checks every entry against the IDL files in the LibreOffice tree (`LO_SRC`, default `C:\Oracle\lo`).

| Shim method | Bridge call chain (handle in, handle out) | Kind | Status |
|---|---|---|---|
| `Api.GetActiveSheet()` | root (model) `XModel.getCurrentController` → controller `XSpreadsheetView.getActiveSheet` | read, read | Works once handles and object results are wired |
| `ApiWorksheet.GetName()` | sheet `XNamed.getName` | read | Works |
| `ApiWorksheet.GetRange(addr)` | sheet `XCellRange.getCellRangeByName(addr)` | read | Works |
| `ApiRange.GetValue()` | range `XCellRangeData.getDataArray` | read | **Blocked**: returns `sequence<sequence<any>>` |
| `ApiRange.SetValue(v)` | range `XCellRangeData.setDataArray(rows)` | edit | **Blocked**: takes `sequence<sequence<any>>` |
| `Api.GetDocument()` | root (model) itself | read | Works (no call) |
| `ApiDocument.GetElementsCount()` | text `XEnumerationAccess.createEnumeration`, then `XEnumeration.hasMoreElements` and `nextElement` in a loop | read | Works, but a loop of bridge calls |
| `ApiDocument.GetElement(i)` | same enumeration, stop at index `i` | read | Works, same loop |
| `ApiParagraph.GetText()` | paragraph `XTextRange.getString` | read | Works |
| `ApiParagraph.AddText(t)` | text `XSimpleText.createTextCursorByRange(paragraph)` → cursor, then `XSimpleText.insertString(cursor, t, false)` | read, edit | Works |

Writer's text object is reached from the model through `XTextDocument.getText` (read). It is not in the table
above because the shim does not call it directly yet; it is needed in the chain for `ApiDocument` and `ApiParagraph`.

## Allowlist entries (declaring interface → methods)

| Interface | Method | Kind | Used by |
|---|---|---|---|
| `com.sun.star.frame.XModel` | `getCurrentController` | read | GetActiveSheet |
| `com.sun.star.sheet.XSpreadsheetView` | `getActiveSheet` | read | GetActiveSheet |
| `com.sun.star.sheet.XSpreadsheetDocument` | `getSheets` | read | not in the first ten; listed for `GetSheets` later |
| `com.sun.star.container.XNameAccess` | `getByName` | read | sheet lookup by name |
| `com.sun.star.container.XNamed` | `getName` | read | GetName |
| `com.sun.star.table.XCellRange` | `getCellRangeByName` | read | GetRange |
| `com.sun.star.sheet.XCellRangeData` | `getDataArray` | read | GetValue (blocked) |
| `com.sun.star.sheet.XCellRangeData` | `setDataArray` | edit | SetValue (blocked) |
| `com.sun.star.text.XTextDocument` | `getText` | read | document text |
| `com.sun.star.container.XEnumerationAccess` | `createEnumeration` | read | GetElement, GetElementsCount |
| `com.sun.star.container.XEnumeration` | `hasMoreElements`, `nextElement` | read | GetElement, GetElementsCount |
| `com.sun.star.text.XTextRange` | `getString` | read | GetText |
| `com.sun.star.text.XSimpleText` | `createTextCursorByRange` | read | AddText |
| `com.sun.star.text.XSimpleText` | `insertString` | edit | AddText |

## What is still missing

- Sequence parameters and results (`getDataArray`, `setDataArray`). Until `bridge_convert.cxx` handles sequences,
  the two cell calls fail with "parameter type not supported yet". Sequence support is the next F3 step.
- A null handle for global calls (`executeMethod`) is not supported yet.
- Each `ApiDocument.GetElement` call makes a loop of bridge calls. Measure this before it is accepted (budget in
  `PLAN_FULL_API.md` section 2.3).
- The engine does not load the bridge yet, so none of this runs.
