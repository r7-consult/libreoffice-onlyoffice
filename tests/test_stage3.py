"""Stage 3 gate (draft): the plugin page survives panel switches without a restart.

Pass: exactly one "webhost|created" for the session, at least 20 "webhost|reattached",
no "webhost|destroyed" while switching, and the page counter keeps growing.
Not compiled or run yet (see docs/PROGRESS.md).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import harness  # noqa: E402


def run():
    office = harness.Office().start()
    ok = False
    try:
        doc = office.new_document("writer")
        frame = doc.getCurrentController().getFrame()
        harness.show_sidebar_deck(office, frame, "PluginHostDeck")
        created = harness.wait_for_log(lambda l: "|webhost|created|" in l, timeout=30)
        if not created:
            print("stage3: no webhost|created in log")
            return False

        for _ in range(20):
            harness.show_sidebar_deck(office, frame, "PropertyDeck")
            time.sleep(0.3)
            harness.show_sidebar_deck(office, frame, "PluginHostDeck")
            time.sleep(0.3)

        time.sleep(3)
        lines = harness.read_log_lines()
        n_created = sum(1 for l in lines if "|webhost|created|" in l)
        n_reattached = sum(1 for l in lines if "|webhost|reattached|" in l)
        n_destroyed = sum(1 for l in lines if "|webhost|destroyed" in l)
        print(f"stage3: created={n_created} reattached={n_reattached} destroyed={n_destroyed}")
        ok = n_created == 1 and n_reattached >= 20 and n_destroyed == 0
        doc.close(True)
    finally:
        office.stop()
    return ok


if __name__ == "__main__":
    result = run()
    print("stage3:", "PASS" if result else "FAIL")
    sys.exit(0 if result else 1)
