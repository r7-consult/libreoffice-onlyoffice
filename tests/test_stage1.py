"""Stage 1 gate: extension installs and the sidebar panel is created in Writer and Calc.

Run with the LibreOffice python: scripts\\run_all.ps1 (runs all test_stage*.py)
Pass condition: log contains "panel|created" for Writer AND Calc.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
import harness  # noqa: E402


def run():
    results = {}
    office = harness.Office().start()
    try:
        for kind in ("writer", "calc"):
            doc = office.new_document(kind)
            frame = doc.getCurrentController().getFrame()
            harness.show_sidebar_deck(office, frame, "PluginHostDeck")
            hits = harness.wait_for_log(lambda l: "|panel|created|" in l, timeout=30)
            results[kind] = bool(hits)
            doc.close(True)
    finally:
        office.stop()
    return results


if __name__ == "__main__":
    res = run()
    for kind, ok in res.items():
        print(f"stage1 {kind}: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if all(res.values()) else 1)
