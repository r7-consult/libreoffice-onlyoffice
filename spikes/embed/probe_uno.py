#!/usr/bin/env python3
"""Embedding spike, part 1: what native handle does a LibreOffice 'systemchildwindow' give us on this host?

Run on the Linux host, with a GUI session:
    SAL_USE_VCLPLUGIN=gtk3 GDK_BACKEND=x11 soffice --norestore --accept="socket,host=localhost,port=2002;urp;" &
    python3 probe_uno.py [--plug]        # --plug also starts plug_webkit.py on the XID

It prints one RESULT line per question and a final VERDICT. Nothing is changed in any document.
Needs: python3-uno, x11-utils (xwininfo, xprop); for --plug also python3-gi and gir1.2-webkit2-4.1.
"""
import os, subprocess, sys, time
import uno
from com.sun.star.beans import PropertyValue
from com.sun.star.awt import Rectangle, WindowDescriptor
from com.sun.star.awt.WindowClass import SIMPLE

HERE = os.path.dirname(os.path.abspath(__file__))
SYSTEM_XWINDOW = 2  # css::lang::SystemDependent::SYSTEM_XWINDOW


def result(key, value):
    print("RESULT %-22s %s" % (key, value), flush=True)


def run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=15).stdout.strip()
    except Exception as e:  # tool missing
        return "unavailable (%s)" % e


def office_pid():
    out = run(["pgrep", "-f", "soffice.bin.*port=2002"])
    return out.split()[0] if out else None


def main():
    ctx = uno.getComponentContext()
    resolver = ctx.ServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", ctx)
    remote = resolver.resolve("uno:socket,host=localhost,port=2002;urp;StarOffice.ComponentContext")
    smgr = remote.ServiceManager
    desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", remote)
    hidden = PropertyValue(); hidden.Name = "Hidden"; hidden.Value = False
    doc = desktop.loadComponentFromURL("private:factory/swriter", "_blank", 0, (hidden,))
    time.sleep(1)

    pid = office_pid()
    result("soffice pid", pid)
    if pid:
        maps = run(["grep", "-o", "libvclplug_[a-z0-9]*", "/proc/%s/maps" % pid]).split()
        result("vcl backend", sorted(set(maps)) or "unknown")
    result("session type", os.environ.get("XDG_SESSION_TYPE", "unknown"))

    frame = doc.CurrentController.Frame
    parent = frame.ContainerWindow
    toolkit = smgr.createInstanceWithContext("com.sun.star.awt.Toolkit", remote)
    desc = WindowDescriptor()
    desc.Type = SIMPLE
    desc.WindowServiceName = "systemchildwindow"
    desc.Parent = parent
    desc.ParentIndex = -1
    desc.Bounds = Rectangle(10, 80, 500, 400)
    desc.WindowAttributes = 0
    child = toolkit.createWindow(desc)
    child.setVisible(True)
    time.sleep(1)

    handle = child.getWindowHandle(uno.ByteSequence(b"\0" * 16), SYSTEM_XWINDOW)
    xid = getattr(handle, "WindowHandle", 0)
    result("child XID", hex(xid) if xid else "none")
    if not xid:
        result("VERDICT", "no X11 handle: native Wayland or no systemchildwindow; the host needs the core patch or a Qt front end")
        return 2

    info = run(["xwininfo", "-id", str(xid)])
    result("xwininfo", "ok" if "Window id" in info else "XID is not visible to X clients")
    props = run(["xprop", "-id", str(xid), "_NET_WM_PID", "WM_CLASS"])
    result("xprop", props.replace("\n", " | "))
    same_process = bool(pid) and ("= " + pid) in props
    result("owner", "XID belongs to soffice itself" if same_process else "owner pid not reported (child windows often carry none)")

    if "--plug" in sys.argv:
        t0 = time.time()
        p = subprocess.run([sys.executable, os.path.join(HERE, "plug_webkit.py"), str(xid)], capture_output=True, text=True, timeout=60)
        print(p.stdout, end="")
        print(p.stderr, end="", file=sys.stderr)
        result("plug exit", "%s after %.1fs" % (p.returncode, time.time() - t0))
        result("ask the tester", "Was a page ('SPIKE OK') visible INSIDE the Writer window while the plug ran? Did it resize/steal focus?")
    else:
        result("next", "re-run with --plug to test a WebKitGTK GtkPlug on this XID")
    result("VERDICT", "XID obtained; judge embedding by the --plug run (visual) and the notes in docs/SPIKE_EMBEDDING.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
