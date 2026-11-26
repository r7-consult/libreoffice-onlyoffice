"""Stage 4 gate (draft): executeMethod round trip and the handshake.

Needs PLUGINHOST_TEST_MODE=1 (run_all.ps1 sets it) so the hello fixture runs its checks.
Pass: log has the handshake for hello, and "plugin-test|gst|\"abc\"" after selecting "abc" in Writer.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import harness  # noqa: E402


def run():
    office = harness.Office().start()
    ok = False
    try:
        doc = office.new_document("writer")
        harness.select_all_text(doc)
        frame = doc.getCurrentController().getFrame()
        harness.show_sidebar_deck(office, frame, "PluginHostDeck")
        handshake = harness.wait_for_log(lambda l: "|protocol|handshake|hello" in l, timeout=30)
        gst = harness.wait_for_log(lambda l: "|plugin-test|gst|" in l and '"abc"' in l, timeout=30)
        print("stage4 handshake:", bool(handshake), " gst:", bool(gst))
        ok = bool(handshake) and bool(gst)
        doc.close(True)
    finally:
        office.stop()
    return ok


if __name__ == "__main__":
    result = run()
    print("stage4:", "PASS" if result else "FAIL")
    sys.exit(0 if result else 1)
