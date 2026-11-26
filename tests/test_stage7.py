"""Stage 7 gate (draft): a renderer failure does not take LibreOffice down, and the plugin restarts.

Steps:
  1. open Writer, show the plugin deck, wait for webhost|created|...|count=1;
  2. kill the WebView2 processes whose --user-data-dir is this plugin's data folder;
  3. expect "webhost|renderer_failed" in the log and LibreOffice still answering UNO;
  4. switch decks away and back: expect "webhost|created|...|count=2" (restart).
Uses PowerShell for the process list because the browser processes are not children of soffice's UNO API.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import harness  # noqa: E402


def kill_plugin_webview_processes():
    script = (
        "Get-CimInstance Win32_Process -Filter \"Name='msedgewebview2.exe'\" | "
        "Where-Object { $_.CommandLine -like '*PluginHost*data*' } | "
        "ForEach-Object { Stop-Process -Id $_.ProcessId -Force; $_.ProcessId }"
    )
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                         capture_output=True, text=True, timeout=60)
    return [line for line in out.stdout.split() if line.isdigit()]


def run():
    office = harness.Office().start()
    ok = False
    try:
        doc = office.new_document("writer")
        frame = doc.getCurrentController().getFrame()
        harness.show_sidebar_deck(office, frame, "PluginHostDeck")
        first = harness.wait_for_log(lambda l: "|webhost|created|" in l and "count=1" in l, timeout=30)
        if not first:
            print("stage7: plugin never started")
            return False

        killed = kill_plugin_webview_processes()
        print("stage7 killed processes:", killed)
        failed = harness.wait_for_log(lambda l: "|webhost|renderer_failed" in l, timeout=30)

        alive = doc.getText() is not None  # LibreOffice still answers UNO
        harness.show_sidebar_deck(office, frame, "PropertyDeck")
        time.sleep(1)
        harness.show_sidebar_deck(office, frame, "PluginHostDeck")
        restarted = harness.wait_for_log(lambda l: "|webhost|created|" in l and "count=2" in l, timeout=30)

        print("stage7 failed:", bool(failed), " alive:", alive, " restarted:", bool(restarted))
        ok = bool(killed) and bool(failed) and alive and bool(restarted)
        doc.close(True)
    finally:
        office.stop()
    return ok


if __name__ == "__main__":
    result = run()
    print("stage7:", "PASS" if result else "FAIL")
    sys.exit(0 if result else 1)
