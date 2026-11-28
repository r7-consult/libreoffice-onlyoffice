#!/usr/bin/env python3
"""Headless runner for the tests of one card (PLAN_FULL_API section 5, F5).

Run:  python -I tests/run_member.py --card cards/todo/<name>.md
      (scripts/check_card.py runs this command; its exit code decides PASS or FAIL)

For each test file in the card's "Tests:" line it:
  1. starts LibreOffice headless (or connects to one already running on the port),
  2. opens a blank fixture document for the area (tests/members/<area>/...),
  3. calls the TestRunner service (org.example.pluginhost.TestRunner, planned in PLAN_FULL_API F5) with the
     test source, which runs setup and run inside the engine and returns the result as JSON,
  4. compares {"value": ...} with tests/expected/<id>.json.

Expectations are never written here. A missing expectation is a failure.

Exit codes:
  0  every test passed
  1  at least one test failed, or has no expectation
  2  the runner cannot run here: LibreOffice, the uno module or the TestRunner service is missing

Settings (environment variables, all optional):
  LO_PROGRAM          LibreOffice program folder with soffice and the uno module.
                      Default: C:\\Program Files\\LibreOffice\\program
  PLUGINHOST_API_DIR  folder with api_shim.js, bridge.js and bridge_allowlist.json. Default: extension\\js of this repo.
  PLUGINHOST_TEST_PORT  UNO socket port. Default: 2083.

Not verified yet: this file has not been run. It needs the build machine, the TestRunner service and LibreOffice.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVICE = "org.example.pluginhost.TestRunner"
EXPECTED_DIR = os.path.join(ROOT, "tests", "expected")
FACTORY_BY_AREA = {
    "cell": "private:factory/scalc",
    "word": "private:factory/swriter",
    "slide": "private:factory/simpress",
}
DEFAULT_LO_PROGRAM = r"C:\Program Files\LibreOffice\program"
DEFAULT_PORT = 2083
CONNECT_TIMEOUT_S = 60


def exit_cannot_run(message):
    print("CANNOT RUN: " + message)
    sys.exit(2)


def parse_card(path):
    """Reads 'Key: value' lines of a card. Returns a dict with the Tests list and the Members list."""
    fields = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"^([A-Za-z ]+):\s*(.*)$", line.strip())
            if m:
                fields[m.group(1).strip()] = m.group(2).strip()
    tests = [t.strip() for t in fields.get("Tests", "").split(",") if t.strip()]
    if not tests:
        exit_cannot_run("card has no Tests line: " + path)
    return {"tests": tests, "members": fields.get("Members", "")}


def area_of(test_rel):
    """tests/members/<area>/<class>/<file>.js -> area. Falls back to 'common'."""
    parts = test_rel.replace("\\", "/").split("/")
    if len(parts) >= 3 and parts[0] == "tests" and parts[1] == "members":
        return parts[2]
    return "common"


def load_test(test_rel):
    """Returns (id, source) of one test file. The file must define TEST with an id."""
    path = os.path.join(ROOT, test_rel)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    m = re.search(r'\bid:\s*"([^"]+)"', text)
    if not m:
        raise ValueError("test has no id: " + test_rel)
    return m.group(1), text


def driver_source(test_text):
    """The engine evaluates this expression; it must return a value, not a function."""
    return "(function () {\n" + test_text + "\nreturn TEST.run(TEST.setup(Api));\n})()"


def expected_value(test_id):
    path = os.path.join(EXPECTED_DIR, test_id + ".json")
    if not os.path.exists(path):
        return None, "no expectation at tests/expected/" + test_id + ".json"
    with open(path, encoding="utf-8") as fh:
        return json.load(fh), None


def start_office(lo_program, port):
    """Starts soffice headless with a socket listener, unless one already listens on the port."""
    soffice = os.path.join(lo_program, "soffice.exe")
    if not os.path.exists(soffice):
        exit_cannot_run("soffice.exe not found in " + lo_program + " (set LO_PROGRAM)")
    accept = "--accept=socket,host=localhost,port=%d;urp;StarOffice.ComponentContext" % port
    return subprocess.Popen([soffice, "--headless", "--norestore", "--nologo", "--nodefault", accept])


def connect(port):
    import uno  # provided by LibreOffice's program folder, see main()

    local = uno.getComponentContext()
    resolver = local.ServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", local)
    url = "uno:socket,host=localhost,port=%d;urp;StarOffice.ComponentContext" % port
    deadline = time.time() + CONNECT_TIMEOUT_S
    last = None
    while time.time() < deadline:
        try:
            return resolver.resolve(url)
        except Exception as exc:  # the office is still starting
            last = exc
            time.sleep(1)
    exit_cannot_run("no LibreOffice on port %d after %d s: %s" % (port, CONNECT_TIMEOUT_S, last))


def open_fixture(desktop, area):
    factory = FACTORY_BY_AREA.get(area, FACTORY_BY_AREA["cell"])
    return desktop.loadComponentFromURL(factory, "_blank", 0, ())


def run_one(runner, desktop, area, test_rel):
    """Returns (passed, message) for one test file."""
    test_id, test_text = load_test(test_rel)
    expected, missing = expected_value(test_id)
    if missing:
        return False, test_id + ": " + missing
    doc = open_fixture(desktop, area)
    import uno  # on sys.path since main() added the LibreOffice program folder

    try:
        raw = uno.invoke(runner, "run", (driver_source(test_text), "{}"))
    except Exception as exc:
        return False, test_id + ": engine error: " + str(exc)
    finally:
        try:
            doc.close(True)
        except Exception:
            pass
    try:
        actual = json.loads(raw)
    except (TypeError, ValueError):
        return False, test_id + ": result is not JSON: " + repr(raw)[:120]
    if actual != expected:
        return False, test_id + ": expected %s, got %s" % (json.dumps(expected), json.dumps(actual))
    return True, test_id + ": value matches"


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", required=True, help="path of the card, for example cards/todo/cell-ApiRange-03.md")
    args = ap.parse_args(argv)

    card = parse_card(os.path.join(ROOT, args.card) if not os.path.isabs(args.card) else args.card)

    lo_program = os.environ.get("LO_PROGRAM", DEFAULT_LO_PROGRAM)
    port = int(os.environ.get("PLUGINHOST_TEST_PORT", DEFAULT_PORT))
    os.environ.setdefault("PLUGINHOST_API_DIR", os.path.join(ROOT, "extension", "js"))
    # Test-only switch: without it the TestRunner service is not created (see docs/TEST_RUNNER.md).
    os.environ["PLUGINHOST_TEST_SERVICE"] = "1"
    if not os.path.isdir(lo_program):
        exit_cannot_run("LibreOffice program folder not found: " + lo_program + " (set LO_PROGRAM)")
    sys.path.insert(0, lo_program)

    office = start_office(lo_program, port)
    try:
        ctx = connect(port)
    except SystemExit:
        office.terminate()
        raise
    smgr = ctx.ServiceManager
    runner = smgr.createInstanceWithContext(SERVICE, ctx)
    if runner is None:
        office.terminate()
        exit_cannot_run("service " + SERVICE + " is not registered (F5 C++ part not built)")
    desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)

    results = []
    for test_rel in card["tests"]:
        area = area_of(test_rel)
        try:
            ok, msg = run_one(runner, desktop, area, test_rel)
        except (OSError, ValueError) as exc:
            ok, msg = False, test_rel + ": " + str(exc)
        results.append(ok)
        print(("PASS " if ok else "FAIL ") + msg)

    try:
        desktop.terminate()
    except Exception:
        pass
    passed = sum(results)
    print("PASS %d/%d" % (passed, len(results)) if all(results) else "FAIL %d/%d" % (passed, len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
