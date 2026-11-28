# Test matrix: Linux and macOS (for a person to run)

Status: DRAFT. Linux: a WebKitGTK 4.1 host is written but not compiled or run (`src/webhost_linux_webkitgtk.cxx`). macOS: no browser implementation exists yet (`src/webhost_stub.cxx` returns no browser); macOS is out of scope for now (`docs/DECISION_LOG.md`, item 8). Criteria 1 and 8 of plan P-1 stay NOT VERIFIED until someone runs the steps below and records the results.

## Preparation
1. Two LibreOffice branches with SDK (same as Windows: `config/versions.json`).
2. Build `pluginhost.so` / `pluginhost.dylib` with `src/CMakeLists.txt` (non-Windows branch). The C++ sources are portable except `plugin_panel.cxx` (uses `rtl_getGlobalProcessId` and the Windows window handle) and `paths.cxx` (returns empty paths until implemented).
3. Pick one browser implementation per platform (plan section 5):
   - Linux X11 / XWayland: WebKitGTK 4.1 in a GtkPlug on the X11 window (`webhost_linux_webkitgtk.cxx`, written, not compiled). CEF is the fallback (`webhost_linux_cef.cxx`, not written), per `docs/DECISION_LOG.md` item 14.
   - Linux gtk3 VCL plugin: the embedding is unverified and probably needs a core patch (`docs/DECISION_LOG.md` item 14).
   - macOS: WKWebView, parent NSView (`webhost_macos.mm`, to be written).

## Matrix

| # | OS | Session / backend | LibreOffice | Criterion | Result (PASS / FAIL / NOT VERIFIED) | Notes and log file |
|---|---|---|---|---|---|---|
| 1 | Ubuntu LTS | X11, `SAL_USE_VCLPLUGIN=gtk3` | branch A | 1 | | |
| 2 | Ubuntu LTS | XWayland (`GDK_BACKEND=x11`), gtk3 | branch A | 1 | | |
| 3 | Ubuntu LTS | X11, `SAL_USE_VCLPLUGIN=gtk4` | branch A | 8 | | record `GLib-GObject-WARNING` and `Gtk-ERROR` lines |
| 4 | Ubuntu LTS | X11, gtk3 + CEF | branch B | 8 | | `Gtk` symbol clash? |
| 5 | macOS x64 | WKWebView | branch A | 1 | | |
| 6 | macOS arm64 | WKWebView | branch A | 1 | | |
| 7 | Ubuntu LTS | X11, `SAL_USE_VCLPLUGIN=kf5` or `qt5`, WebKitGTK | branch A | 1 | NOT VERIFIED | Not run. Embedding under this VCL plugin is unverified |
| 8 | Ubuntu LTS | Native Wayland (no XWayland), WebKitGTK | branch A | 1 | NOT VERIFIED | Not run. Native Wayland embedding is unverified |

## Steps for each row
1. Install the extension into an isolated profile: `unopkg add --shared -env:UserInstallation=file:///<path>/profile-A pluginhost.oxt`.
2. Run `python3 tests/test_stage1.py` ... `test_stage7.py` with `PLUGINHOST_LOG` set and `PLUGINHOST_TEST_MODE=1`, using LibreOffice's own python (`program/python`).
3. Copy the log to `logs/` and fill the row.

## Manual checks on Windows (not automated)
- Context menu (criterion 7): right-click in Writer; "Hello from plugin" appears; `ctxmenu|requested` -> `ctxmenu|returned` <= 300 ms over 20 runs (median).
- Clicking the context menu item: click handling is in `src/menu_dispatch.cxx` (see `src/context_menu.hxx`). Not yet run.
- Docking: drag the sidebar between left and right; the panel must not restart the plugin.
- High DPI, IME, screen reader: measure and record in `RESULTS.md` under "additional measurements".
