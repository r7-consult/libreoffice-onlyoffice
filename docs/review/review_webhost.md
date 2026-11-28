# Review: webhost_linux_webkitgtk.cxx (not compiled, reviewed statically)

Sources used: WebKit glib/gtk API sources (raw.githubusercontent.com/WebKit/WebKit), GTK 3 gtkplug.c (gtk-3-24),
WebKitGTK reference search results; LibreOffice sources under C:\Oracle\lo. webkitgtk.org and docs.gtk.org were refused by
the proxy, so version facts come from WebKit source doc comments.

## Job 2: embedding

What PluginPanel passes: nativeWindowHandle() asks the systemchildwindow for getWindowHandle(SYSTEM_XWINDOW) and reads
SystemDependentXWindow.WindowHandle (an XID). That is toolkit/source/awt/vclxsystemdependentwindow.cxx:
`pSysData->GetWindowHandle(frame)`. createWebHost therefore gets `(void*)XID`, or garbage on Wayland (below).
SystemEnvData (include/vcl/sysdata.hxx) on UNX: pSalFrame, pWidget (GtkWidget* / QWidget*), pDisplay, private aWindow.
Only the XID and DisplayPointer leave through UNO; pWidget is NOT exposed.

Per VCL plugin:
- gtk3 (default on most distros). GtkSalObject (vcl/unx/gtk3/gtkobject.cxx) makes a GtkGrid (no X window) inside the
  frame's GtkFixed (inside a GtkEventBox). aWindow is resolved lazily by GtkSalFrame::ResolveWindowHandle ->
  gdk_x11_surface_get_xid(gtk_widget_get_window(grid)) = the XID of the frame's event-box/toplevel GdkWindow, a window
  GDK owns in this process. Not a dedicated child window, no position info.
  - X11/XWayland: gtk_plug_new_for_display(XID): gdk_x11_window_lookup finds the in-process GdkWindow with user_data set,
    GTK warns "Can't create GtkPlug as child of non-GtkSocket", socket_window = NULL; the plug is a free top-level
    window. gtk_plug_new returns non-null, so the old `if (!plug)` check never fired. NOT WORKING.
  - native Wayland: GetNativeWindowHandle returns a wl_surface* cast to an integer (not an XID); pDisplay is wl_display*.
    GtkPlug does not exist on Wayland. Old code would pass a pointer as an XID.
- gtk4: GtkSalObject exists, no XID for widgets, GtkPlug does not exist in GTK4; loading GTK3 into a GTK4 process is
  unsupported. Not supported.
- qt5/kf5 on xcb: QtFrame::ResolveWindowHandle returns QWidget::winId() of the QtObject container (native X child window).
  Works with GtkPlug on XID (GDK does not own it -> gdk_x11_window_foreign_new). GTK is not initialised there, so the host
  must gtk_init_check itself (done, forced to X11). Qt5 on Wayland: winId() deliberately not called -> handle 0 -> panel
  logs no handle. qt6/kf6: no winId (version < 6 check) -> 0 -> unavailable.
- gen (X11, no toolkit): real X child window, but the GLib main context is not iterated by VCL's own loop, so WebKit
  would never run. Unsupported/untested.

Supported by this file after the fix: qt5/kf5 VCL plugin on X11/XWayland (xcb). Not: gtk3 (any session), gtk4, qt6/kf6,
native Wayland anywhere, gen.

Most robust approach: expose the socket GtkWidget* for gtk3. Core patch (outside this task): in
VCLXSystemDependentWindow::getWindowHandle add a way to return pSysData->pWidget (new SystemDependent constant or a
new field), then the host does `gtk_container_add(GTK_CONTAINER(grid), webview)` with no plug and no XID. VCL already
positions/clips/reparents that grid (GtkSalObject::SetPosSize/Reparent), so this works on gtk3 under X11, XWayland AND
native Wayland, with no GtkPlug. Qt (kf5/qt6) would need QWidget::createWindowContainer / QWindow::fromWinId, separate work.
Alternative without core patch for gtk3 on X11 only: none that is clean (would mean guessing the GtkGrid in the frame's
GtkFixed).

## Job 1: API verification

OK as written:
- webkit_website_data_manager_new("base-data-directory", ..., "base-cache-directory", ..., NULL): since 2.10, construct-only
  strings, returns transfer full (only compiled when not the 2022 API; webkit2gtk-4.1 is the old API, fine).
- webkit_web_context_new_with_website_data_manager (2.10): OK, context refs the manager.
- webkit_web_context_register_uri_scheme(ctx, scheme, cb, user_data, destroy): OK. Callback (WebKitURISchemeRequest*, gpointer).
- webkit_security_manager_register_uri_scheme_as_secure / _as_cors_enabled: not deprecated; no documented ordering rule.
- webkit_uri_scheme_request_finish(request, stream, gint64 len, content_type): len -1 = unknown; request keeps its own
  ref to the stream, so unref after finish is right. finish_error does not take the GError, caller frees: matches code.
- script-message-received: webkit2gtk-4.1 (old API) passes WebKitJavascriptResult (JSCValue only with ENABLE(2022_GLIB_API),
  i.e. 6.0). Not deprecated. register_script_message_handler(manager, name) returns gboolean (2-arg in 4.1; 3-arg with
  world_name only in 6.0). script-message-with-reply-received exists since 2.40 but is not needed.
- decide-policy: (view, decision, type, user_data) -> gboolean; policy decision functions OK.
- webkit_user_script_new(source, frames, time, allow, block): OK. g_uri_parse/g_uri_get_host/g_uri_get_path/
  g_uri_unescape_string/g_uri_unref: GLib 2.66+, ownership as used.
- gtk_plug_new_for_display(display, Window), gtk_plug_get_embedded, gdk_x11_window_lookup_for_display: GTK3, X11-only
  (headers guarded by GDK_WINDOWING_X11); Window typedef comes from gdkx.h -> X11/Xlib.h.

Fixed (definite defects):
1. reparent(nullptr) (HostService::release parks on every panel disposal) destroyed the browser and left m_view null, so
   the session was dead and every later acquire silently did nothing. Now parks in a GtkOffscreenWindow; reparent to a
   new XID re-embeds; failure to embed keeps the view parked.
2. gtk_widget_destroy(plug) with the view still inside destroys the view. Always gtk_container_remove first; the host
   now owns one strong ref (g_object_ref_sink) so the view survives container changes. Constructor failure path no longer
   leaks the floating view.
3. gtk_plug_new null check is meaningless. makePlug now checks GDK_IS_X11_DISPLAY, refuses an XID that is a GDK window of
   this process (gtk3 VCL), and checks gtk_plug_get_embedded(). Returns nullptr cleanly -> "webhost|unavailable".
4. GTK not initialised under qt5/kf5: ensureX11Gtk() does gtk_disable_setlocale, gdk_set_allowed_backends("x11"),
   gtk_init_check, only when no GDK display exists.
5. web-process-crashed deprecated since 2.20: replaced by web-process-terminated (since 2.20, void, reason arg).
   Behaviour aligned with Windows: renderer failure -> reload once; second failure before a load finishes -> failure handler.
   WEBKIT_WEB_PROCESS_TERMINATED_BY_API (2.34) ignored, guarded by WEBKIT_CHECK_VERSION.
6. webkit_web_view_run_javascript deprecated since 2.40: evaluate_javascript under WEBKIT_CHECK_VERSION(2,40,0),
   run_javascript below.
7. m_loaded never reset: after reload/navigation, messages hit a half-loaded page. Reset on WEBKIT_LOAD_STARTED.
   __phDeliver call is guarded in case the shim is missing (error page).
8. setBounds only set a size request: an XEmbed-less plug is not resized by any socket, so add gtk_window_resize.
   setVisible also hides/shows the plug (empty plug window stayed mapped).
9. Host names matched case-insensitively (WebKit keeps case of custom-scheme hosts, WebView2 does not). Added mime
   types wasm/woff/woff2/ttf/ico/webp/txt/xml.

Not fixed / risks:
- gtk3 VCL (the common default) cannot work with GtkPlug until the core patch above or another embedding exists.
- XEmbed-less plug: no keyboard focus hand-off / focus-in from the Qt container; x,y of setBounds ignored (always 0,0).
- Plugin page origin is pluginhost://<id>.plugin.invalid, not https:// as on Windows; pages testing location.protocol differ.
- Scheme handler user_data is `this` with no destroy notify; relies on the context being finalized in the destructor
  (we hold the last refs) so no callbacks arrive afterwards.
- Plug into Qt container across two X connections: should work (XIDs are server-wide) but is untested.
- Needs the GLib main loop iterated by the host app: true for gtk3 and Qt (glib dispatcher), false for the gen plugin.
- WebKit web-process sandbox (bubblewrap) may fail inside Snap/Flatpak LibreOffice.

## Build requirements

pkg-config: gtk+-3.0 (>= 3.10 for gdk_set_allowed_backends; use >= 3.22 in practice) and webkit2gtk-4.1 (>= 2.32 which
first shipped the 4.1 API; >= 2.34 to see TERMINATED_BY_API; >= 2.40 uses evaluate_javascript, older falls back).
Add gtk+-x11-3.0 (pkg_check_modules(PH_GTK3X11 REQUIRED IMPORTED_TARGET gtk+-x11-3.0)) so gdkx.h/gtkx.h and Xlib are
guaranteed; libgtk-3-dev pulls libx11-dev.
Debian/Ubuntu: libgtk-3-dev (>= 3.22), libwebkit2gtk-4.1-dev (>= 2.36 on Ubuntu 22.04; 2.44 on 24.04).
Fedora: gtk3-devel, webkit2gtk4.1-devel. Runtime: libwebkit2gtk-4.1-0 + gir not needed.
Lines in src/CMakeLists.txt 66-67 and 72: add PkgConfig::PH_GTK3X11 only if gdkx.h is not found; otherwise unchanged.
