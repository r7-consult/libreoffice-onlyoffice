"""Stage 5 gate (draft): callCommand, watchdog, one-step Undo.

Pass:
  - the 100x100 write completes in <= 1000 ms as reported by the page (criterion 3);
  - one Undo after it leaves A1 empty (criterion 3, Undo part);
  - the infinite loop ends with TimeoutError and LibreOffice still answers UNO (criterion 4).
Numbers go to the log; docs/RESULTS.md records them.
"""
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import harness  # noqa: E402


def run():
    office = harness.Office().start()
    results = {}
    try:
        doc = office.new_document("calc")
        frame = doc.getCurrentController().getFrame()
        harness.show_sidebar_deck(office, frame, "PluginHostDeck")

        fill = harness.wait_for_log(lambda l: "|plugin-test|cmd_fill|" in l, timeout=60)
        timeout = harness.wait_for_log(lambda l: "|plugin-test|cmd_timeout|" in l, timeout=60)

        ms = None
        if fill:
            m = re.search(r'"ms":\s*(\d+)', fill[-1])
            ms = int(m.group(1)) if m else None
        results["fill_ms"] = ms
        results["fill_within_1s"] = ms is not None and ms <= 1000
        results["timeout_reported"] = bool(timeout) and "TimeoutError" in timeout[-1]

        a1_before = harness.cell_string(doc, 0, "A1")
        frame_dispatch = harness.dispatch(office, frame, ".uno:Undo")
        time.sleep(1)
        a1_after = harness.cell_string(doc, 0, "A1")
        results["a1_before_undo"] = a1_before
        results["a1_after_undo"] = a1_after
        results["undo_clears_fill"] = a1_before == "0" and a1_after == ""

        # Criterion 4: LibreOffice still answers UNO after the watchdog fired.
        results["uno_alive_after_timeout"] = doc.getSheets().getCount() >= 1
        doc.close(True)
    finally:
        office.stop()

    print(json.dumps(results, indent=2))
    return all([results["fill_within_1s"], results["timeout_reported"], results["undo_clears_fill"],
                results["uno_alive_after_timeout"]])


if __name__ == "__main__":
    result = run()
    print("stage5:", "PASS" if result else "FAIL")
    sys.exit(0 if result else 1)
