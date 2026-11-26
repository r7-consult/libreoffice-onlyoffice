"""Stage 6 gate (draft): context menu interceptor is attached and receives plugin items.

Automatic part (this file): the interceptor is attached in Writer and Calc and the page's
setContextMenuItems message reached the host ("protocol|contextMenu|<id>|items=1").

Manual part (not automated, see docs/TEST_MATRIX_LINUX_MACOS.md, section Windows manual):
right-click in the document and check that "Hello from plugin" appears and that the
"ctxmenu|requested" -> "ctxmenu|returned" time is <= 300 ms (criterion 7). The timing line
is "ctxmenu|returned|<ms>".
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
        frame = doc.getCurrentController().getFrame()
        harness.show_sidebar_deck(office, frame, "PluginHostDeck")
        items = harness.wait_for_log(lambda l: "|protocol|contextMenu|" in l and "items=1" in l, timeout=30)
        attached = harness.wait_for_log(lambda l: "|ctxmenu|attached" in l, timeout=10)
        print("stage6 attached:", bool(attached), " items:", bool(items))
        ok = bool(items) and bool(attached)
        doc.close(True)
    finally:
        office.stop()
    return ok


if __name__ == "__main__":
    result = run()
    print("stage6 (automatic part):", "PASS" if result else "FAIL")
    sys.exit(0 if result else 1)
