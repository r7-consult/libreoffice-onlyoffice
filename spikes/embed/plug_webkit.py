#!/usr/bin/env python3
"""Embedding spike, part 2: put a WebKitGTK view into an existing X11 window (GtkPlug), from a separate process.

Usage: plug_webkit.py <XID>      (started by probe_uno.py --plug, or by hand)
Runs 20 seconds, then exits. This is the out-of-process variant: if it works, the web host can live in a helper process.
"""
import sys
import gi
gi.require_version("Gtk", "3.0")
gi.require_version("WebKit2", "4.1")
from gi.repository import Gtk, GLib, WebKit2

xid = int(sys.argv[1], 0)
plug = Gtk.Plug.new(xid)
view = WebKit2.WebView()
view.load_html("<body style='font:24px sans-serif;background:#cfe8ff'><h1>SPIKE OK</h1>"
               "<p id=t>input test: <input></p><script>document.title='ready'</script></body>", "about:blank")
plug.add(view)
plug.connect("embedded", lambda *_: print("RESULT plug embedded       yes", flush=True))
plug.connect("destroy", lambda *_: Gtk.main_quit())
plug.set_size_request(500, 400)
plug.show_all()
print("RESULT plug window         %#x (plug id)" % plug.get_id(), flush=True)
GLib.timeout_add_seconds(20, Gtk.main_quit)
Gtk.main()
