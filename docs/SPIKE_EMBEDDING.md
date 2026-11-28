# Spike: embedding the web view on Linux

Status: written on 2026-11-28, **not run**. It needs a Linux machine with a desktop session and LibreOffice.
It answers one question: can WebKitGTK be shown inside a LibreOffice sidebar panel, and by which route?

Background (verified in the LibreOffice source, see `docs/REVIEW_2026-11-28.md`): on Linux the UNO API returns only an
X11 window id (`SystemDependentXWindow`). Under the gtk3 front end that id belongs to a GDK window of the soffice process
itself, so a `GtkPlug` inside soffice cannot reparent into it. Native Wayland returns no usable id. Only the Qt/KDE front
ends on X11 give a plain foreign child window. The spike measures what this host actually does.

## Run

```
sudo apt install python3-uno python3-gi gir1.2-webkit2-4.1 gir1.2-gtk-3.0 x11-utils
cd spikes/embed
# one run per front end that is installed (gtk3, gen, kf5, qt5); always on X11, not native Wayland
SAL_USE_VCLPLUGIN=gtk3 GDK_BACKEND=x11 soffice --norestore --accept="socket,host=localhost,port=2002;urp;" &
python3 probe_uno.py --plug
```

`probe_uno.py` opens an empty Writer window, creates a `systemchildwindow` in it (the same call the panel code makes),
asks for its X11 id and then starts `plug_webkit.py`, which puts a WebKitGTK view into that id from a **separate process**.
Look at the Writer window for 20 seconds: a light-blue "SPIKE OK" page should appear in the child area.

## Record the results

Copy the `RESULT` lines and the visual answers here (one row per front end).

| Front end | XID obtained | xwininfo sees it | Plug embedded | Page visible | Notes (resize, focus, typing) |
|---|---|---|---|---|---|
| gtk3 / X11 | | | | | |
| kf5 or qt5 / X11 | | | | | |
| gen / X11 | | | | | |
| gtk3 / native Wayland | | | | | |

## Decision table

| Result | Meaning | Next step |
|---|---|---|
| Page visible with gtk3 | Cross-process plug works in this setup | Move the web host into a helper process (also removes the WebKit link from `pluginhost.so`); keep `GtkPlug` code out of soffice |
| Visible only with kf5/qt5 | Same-process or cross-process plug needs a real child window | First Linux target: LibreOffice with the Qt/KDE front end on X11; document it; no core patch |
| Not visible anywhere, but XID exists | Window exists, embedding refused | Core patch exposing the toolkit widget (`pWidget`), or a separate floating window as an interim |
| No XID (Wayland only) | Handle API gives nothing | Run LibreOffice on X11 (XWayland) as a requirement, or core patch |

An out-of-process host is attractive anyway (a WebKit crash cannot take down soffice); the spike also tells whether it
is possible without a LibreOffice patch.
