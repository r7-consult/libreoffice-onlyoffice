"""Test harness for pluginhost prototype P-1.

Runs a LibreOffice instance with an isolated profile, connects over UNO,
opens documents, and reads the plugin log. Requires the python interpreter
shipped with LibreOffice (program\\python.exe) because it imports `uno`.

Environment (set by scripts\\env.ps1):
  LO_PROGRAM                LibreOffice program directory
  PLUGINHOST_PROFILE_DIR    isolated user profile directory
  PLUGINHOST_LOG            log file the extension appends to
"""
import os
import subprocess
import sys
import time

PORT = int(os.environ.get("PLUGINHOST_PORT", "2202"))


def _lo_program():
    p = os.environ.get("LO_PROGRAM")
    if not p or not os.path.isdir(p):
        raise RuntimeError("LO_PROGRAM is not set or missing. Run scripts\\env.ps1 first.")
    return p


def _import_uno():
    # uno.py lives in the LibreOffice program directory.
    sys.path.insert(0, _lo_program())
    import uno  # noqa: E402
    return uno


class Office:
    """Starts soffice headless=False (panels need a real window) and connects to it."""

    def __init__(self):
        self.proc = None
        self.desktop = None
        self.uno = _import_uno()

    def start(self, timeout=60):
        profile = os.environ["PLUGINHOST_PROFILE_DIR"].replace("\\", "/")
        soffice = os.path.join(_lo_program(), "soffice.exe")
        accept = f"socket,host=localhost,port={PORT};urp;"
        self.proc = subprocess.Popen(
            [soffice, "--norestore", "--nologo", "--nodefault",
             f"-env:UserInstallation=file:///{profile}",
             f"--accept={accept}"])
        local = self.uno.getComponentContext()
        resolver = local.ServiceManager.createInstanceWithContext(
            "com.sun.star.bridge.UnoUrlResolver", local)
        deadline = time.time() + timeout
        last_err = None
        while time.time() < deadline:
            try:
                ctx = resolver.resolve(f"uno:socket,host=localhost,port={PORT};urp;StarOffice.ComponentContext")
                smgr = ctx.ServiceManager
                self.desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
                self.ctx = ctx
                return self
            except Exception as e:  # noqa: BLE001 - connection retries
                last_err = e
                time.sleep(0.5)
        self.stop()
        raise RuntimeError(f"could not connect to soffice within {timeout}s: {last_err}")

    def new_document(self, kind):
        url = {"writer": "private:factory/swriter", "calc": "private:factory/scalc"}[kind]
        return self.desktop.loadComponentFromURL(url, "_blank", 0, ())

    def stop(self):
        try:
            if self.desktop is not None:
                self.desktop.terminate()
        except Exception:  # noqa: BLE001
            pass
        if self.proc is not None:
            try:
                self.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None


def read_log_lines():
    path = os.environ.get("PLUGINHOST_LOG")
    if not path or not os.path.exists(path):
        return []
    with open(path, encoding="utf-8", errors="replace") as f:
        return [line.rstrip("\n") for line in f]


def wait_for_log(predicate, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        lines = read_log_lines()
        hit = [l for l in lines if predicate(l)]
        if hit:
            return hit
        time.sleep(0.5)
    return []


def dispatch(office, frame, command_url):
    """Runs a .uno: command on a frame via com.sun.star.frame.DispatchHelper."""
    helper = office.ctx.ServiceManager.createInstanceWithContext(
        "com.sun.star.frame.DispatchHelper", office.ctx)
    helper.executeDispatch(frame, command_url, "", 0, ())


def show_sidebar_deck(office, frame, deck_id="PluginHostDeck"):
    """Makes the sidebar show the given deck (SidebarDeck.<id> command)."""
    dispatch(office, frame, f".uno:SidebarDeck.{deck_id}")
