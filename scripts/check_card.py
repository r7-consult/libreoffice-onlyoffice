"""F9 of docs/PLAN_FULL_API.md: checks one factory card before its result is accepted.

Usage:  python -I scripts/check_card.py cards/todo/<card>.md

Prints one line per check and a final verdict:
  PASS     every static check passed and the card's test command exited 0
  FAIL     a rule was broken (protected file changed, denied name used, file too long, ...)
  BLOCKED  the static checks passed but the test command cannot run yet (the runner is missing, or it exited 2:
           no LibreOffice, no uno module, no TestRunner service)

Exit code: 0 only for PASS. Anything else is not accepted.

The card is written by a weak model, so nothing in it is trusted: every path is normalised before it is compared, the
Run line must be the one standard command (it is rebuilt here, never taken from the card), and the check fails closed
when git cannot answer.
"""
import os
import posixpath
import re
import shlex
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
REQUIRED = ["Members", "Edit only", "Read", "Tests", "Run", "Done when"]
PROTECTED_PREFIXES = (
    "tests/", "templates/", "docs/recipes/", "scripts/", "src/", "docs/API_COVERAGE.csv",
    "docs/API_INVENTORY.csv", "docs/API_USAGE.csv", "docs/adr/", "cards/",
    "extension/js/bridge", "extension/js/api_shim.js",
)
# What git must report as unchanged (exact files and folders: a pathspec is not a prefix). Not cards/: a card file is
# new until it is committed.
GIT_WATCHED = [
    "tests", "templates", "docs/recipes", "scripts", "src", "docs/API_COVERAGE.csv", "docs/API_INVENTORY.csv",
    "docs/API_USAGE.csv", "docs/adr", "extension/js/bridge.js", "extension/js/bridge_boot.js",
    "extension/js/bridge_allowlist.json", "extension/js/api_shim.js",
]
# A card may only edit Api class files. Everything else is out of scope even if it is not in the protected list.
EDIT_PREFIX = "extension/js/api/"
TEST_PREFIX = "tests/members/"
# Names from PLAN_FULL_API.md section 2.2. Their use in an Api* class is a FAIL, whatever the allowlist says.
DENIED = [
    "__ph_call", "globalThis", "XStorable", "XComponentLoader", "XDispatchHelper", "XDispatchProvider",
    "XSimpleFileAccess", "createInstance", "createInstanceWithContext", "getServiceManager",
    "XConfigurationProvider", "eval(", "new Function", "XScript", "XMacro", "system(", "ShellExecute",
]
# Spacing and escape-hatch variants of the names above. Static checks cannot be complete (string concatenation gets
# past any of them); the host denylist in bridge.js and bridge_native.cxx is the real barrier.
DENIED_PATTERNS = [
    r"\beval\s*\(", r"\bnew\s+Function\b", r"\bFunction\s*\(", r"__proto__", r"\bimport\s*\(", r"\brequire\s*\(",
    r"\.constructor\b", r"\[\s*[\"']constructor[\"']\s*\]", r"\bsystem\s*\(", r"\bReflect\b",
]
MAX_LINES = 400
MAX_MEMBERS = 10
RUN_TIMEOUT_S = 900
LINE_RE = re.compile(r"^(?P<key>[A-Za-z][A-Za-z ]*?):\s*(?P<value>.*)$")
MEMBER_RE = re.compile(r"^(?P<cls>[A-Za-z][A-Za-z0-9]*)\.(?P<name>[A-Za-z][A-Za-z0-9_]*)$")


def parse_card(path, duplicates=None):
    """Return {field: value} from the 'Key: value' lines at the top of the card.

    A field that appears twice is recorded in `duplicates`: tests/run_member.py reads the last one, so two lines
    could show this checker one value and the runner another.
    """
    fields = {}
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            m = LINE_RE.match(raw.strip())
            if m and m.group("key") in REQUIRED:
                key = m.group("key")
                if key in fields and duplicates is not None:
                    duplicates.append(key)
                fields[key] = m.group("value").strip()
    return fields


def split_paths(value):
    return [p.strip() for p in value.split(",") if p.strip()]


def norm_rel(path):
    """Normalise a repository-relative path from a card: forward slashes, no '.' or '..' parts.

    Returns None for anything that is not a plain relative path: absolute paths, drive letters, '..' escapes,
    glob characters and git pathspec magic (':'), control characters.
    """
    if not path or any(c in path for c in ":*?[]\0\r\n"):
        return None
    p = posixpath.normpath(path.replace("\\", "/"))
    if p in (".", "") or p.startswith("/") or p == ".." or p.startswith("../"):
        return None
    return p


def under(path, prefix):
    """Case-insensitive prefix test (Windows file systems ignore case)."""
    return path.lower().startswith(prefix.lower())


def git_changed(paths):
    """Return (changed, error). `changed` are paths that differ from HEAD (staged, unstaged or untracked).

    Fails closed: if git cannot run or exits non-zero, `error` says so and the caller must FAIL.
    """
    if not paths:
        return [], None
    try:
        proc = subprocess.run(["git", "--literal-pathspecs", "status", "--porcelain", "--", *paths], cwd=ROOT,
                              capture_output=True, text=True, check=False, timeout=120)
    except (OSError, subprocess.SubprocessError) as exc:
        return [], "git cannot run: " + str(exc)
    if proc.returncode != 0:
        return [], "git status failed (exit %d): %s" % (proc.returncode, proc.stderr.strip()[:200])
    changed = []
    for line in proc.stdout.splitlines():
        name = line[3:].strip().strip('"')
        if "__pycache__" in name:
            continue
        changed.append(name)
    return changed, None


def main(argv):
    if len(argv) != 1:
        print("usage: check_card.py <card.md>")
        return 2
    card = os.path.abspath(argv[0])
    if not os.path.isfile(card):
        print("FAIL card: file not found:", card)
        return 1
    results = []  # (ok, message)

    duplicates = []
    fields = parse_card(card, duplicates)
    missing = [k for k in REQUIRED if k not in fields]
    results.append((not missing, "card fields present" if not missing else "card missing fields: " + ", ".join(missing)))
    if missing:
        return report(results, blocked=False)
    results.append((not duplicates, "no field appears twice" if not duplicates
                    else "field appears twice: " + ", ".join(sorted(set(duplicates)))))

    members = split_paths(fields["Members"])
    parsed = [MEMBER_RE.match(m) for m in members]
    bad_members = [m for m, p in zip(members, parsed) if not p]
    results.append((1 <= len(members) <= MAX_MEMBERS and not bad_members,
                    f"members {len(members)} in 1..{MAX_MEMBERS}, each Class.Name" if not bad_members
                    else "members must look like Class.Name: " + ", ".join(bad_members)))

    # Edit scope: normalised, inside extension/js/api/, outside every protected prefix.
    edit_files, bad_edit = [], []
    for raw in split_paths(fields["Edit only"]):
        rel = norm_rel(raw)
        if rel is None or not under(rel, EDIT_PREFIX) or not rel.endswith(".js") \
                or any(under(rel, p) for p in PROTECTED_PREFIXES):
            bad_edit.append(raw)
        else:
            edit_files.append(rel)
    results.append((not bad_edit and bool(edit_files),
                    f"edit scope is Api class files under {EDIT_PREFIX}" if not bad_edit and edit_files
                    else "edit scope not allowed: " + (", ".join(bad_edit) or "(empty)")))

    # Tests: normalised, inside tests/members/, present, and one per member (tests/members/<area>/<Class>/<Name>.js).
    test_files, bad_tests = [], []
    for raw in split_paths(fields["Tests"]):
        rel = norm_rel(raw)
        if rel is None or not under(rel, TEST_PREFIX) or not rel.endswith(".js") \
                or not os.path.isfile(os.path.join(ROOT, rel)):
            bad_tests.append(raw)
        else:
            test_files.append(rel)
    wanted = {f"{p.group('cls')}/{p.group('name')}.js".lower() for p in parsed if p}
    have = {"/".join(t.split("/")[-2:]).lower() for t in test_files}
    untested = sorted(wanted - have)
    tests_ok = bool(not bad_tests and test_files and not untested)
    results.append((tests_ok,
                    "every member has a test under tests/members/" if tests_ok
                    else "tests problem: " + "; ".join(
                        ([f"not a test file: {', '.join(bad_tests)}"] if bad_tests else []) +
                        ([f"no test for: {', '.join(untested)}"] if untested else []) or ["no tests listed"])))
    classes = {os.path.splitext(posixpath.basename(f))[0].lower() for f in edit_files}
    foreign = sorted({p.group("cls") for p in parsed if p and p.group("cls").lower() not in classes})
    results.append((not foreign, "members belong to the edited class files" if not foreign
                    else "members of classes that are not in Edit only: " + ", ".join(foreign)))

    # Protected files must equal HEAD: tests, expectations, runner, templates, scripts, bridge, src, docs tables.
    watched = GIT_WATCHED + test_files
    protected_changed, git_error = git_changed(watched)
    if git_error:
        results.append((False, git_error))
    else:
        results.append((not protected_changed, "protected files and tests unchanged" if not protected_changed
                        else "protected files changed: " + ", ".join(protected_changed)))

    too_long, denied_hits = [], []
    for rel in edit_files:
        full = os.path.join(ROOT, rel)
        if not os.path.isfile(full):
            too_long.append(rel + " (missing)")
            continue
        with open(full, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        if text.count("\n") + 1 > MAX_LINES:
            too_long.append(rel)
        for name in DENIED:
            if name in text:
                denied_hits.append(f"{rel}: {name}")
        for pattern in DENIED_PATTERNS:
            if re.search(pattern, text):
                denied_hits.append(f"{rel}: /{pattern}/")
    results.append((not too_long, f"edited files <= {MAX_LINES} lines and present" if not too_long
                    else "file problems: " + ", ".join(too_long)))
    results.append((not denied_hits, "no denied names in edited files" if not denied_hits
                    else "denied names: " + "; ".join(denied_hits)))

    waiver_ok, waiver_msg = check_waivers(os.path.splitext(os.path.basename(card))[0], members)
    results.append((waiver_ok, waiver_msg))

    # The Run line is not executed from the card: it must be the standard command for this very card.
    card_rel = os.path.relpath(card, ROOT).replace("\\", "/")
    run_ok = run_line_ok(fields["Run"], card_rel)
    results.append((run_ok, "Run line is the standard runner command" if run_ok
                    else "Run line must be: python -I tests/run_member.py --card " + card_rel))

    static_ok = all(ok for ok, _ in results)
    if not static_ok:
        return report(results, blocked=False)

    runner = os.path.join(ROOT, "tests", "run_member.py")
    if not os.path.exists(runner):
        results.append((False, "test runner missing: tests/run_member.py (F5 not built yet)"))
        return report(results, blocked=True)

    try:
        proc = subprocess.run([sys.executable, "-I", runner, "--card", card], cwd=ROOT, capture_output=True,
                              text=True, check=False, timeout=RUN_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        results.append((False, f"test command timed out after {RUN_TIMEOUT_S} s"))
        return report(results, blocked=False)
    tail = (proc.stdout + proc.stderr).strip().splitlines()[-1:] or [""]
    results.append((proc.returncode == 0, f"test command exit {proc.returncode}: {tail[0]}"))
    # run_member.py exits 2 when it cannot run here (no LibreOffice, no uno, no TestRunner service): not a test failure.
    return report(results, blocked=proc.returncode == 2)


def run_line_ok(run_cmd, card_rel):
    """True when the Run line is exactly `python -I tests/run_member.py --card <path of this card>`."""
    try:
        parts = shlex.split(run_cmd)
    except ValueError:
        return False
    if len(parts) != 5 or parts[:4] != ["python", "-I", "tests/run_member.py", "--card"]:
        return False
    target = norm_rel(parts[4])
    return target is not None and (target.lower() == card_rel.lower() or (
        under(target, "cards/") and posixpath.basename(target).lower() == posixpath.basename(card_rel).lower()))


MLR_HEADING = re.compile(r"^#### (MLR-\d+) ", re.MULTILINE)
WAIVER_FILE = os.path.join(ROOT, "tests", "mlr_waivers.csv")
WAIVER_COLUMNS = ["card", "member", "rule", "reason"]


def known_mlr_rules():
    """Rule IDs exactly as ADR-003 defines them. The checker reads them, so the two cannot drift apart."""
    adr = os.path.join(ROOT, "docs", "adr", "ADR-003-mapped-limited-rules.md")
    if not os.path.isfile(adr):
        return set()
    with open(adr, encoding="utf-8") as fh:
        return set(MLR_HEADING.findall(fh.read()))


def check_waivers(card_name, members):
    """Waivers (tests/mlr_waivers.csv, written by a human or Sonnet, never by a card) must cite a real rule and a reason.

    Returns (ok, message). A missing waiver file means no waivers, which is valid.
    """
    if not os.path.isfile(WAIVER_FILE):
        return True, "no waivers recorded"
    import csv
    with open(WAIVER_FILE, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None or [c.strip() for c in reader.fieldnames] != WAIVER_COLUMNS:
            return False, "tests/mlr_waivers.csv header must be: " + ",".join(WAIVER_COLUMNS)
        rows = list(reader)
    rules = known_mlr_rules()
    if not rules:
        return False, "docs/adr/ADR-003-mapped-limited-rules.md has no MLR rules"
    bad = []
    mine = [r for r in rows if (r["card"] or "").strip() == card_name]
    for r in mine:
        if (r["rule"] or "").strip() not in rules:
            bad.append(f"{r['member']}: unknown rule {r['rule']}")
        if not (r["reason"] or "").strip():
            bad.append(f"{r['member']}: empty reason")
        if (r["member"] or "").strip() not in members:
            bad.append(f"{r['member']}: not a member of this card")
    if bad:
        return False, "waivers invalid: " + "; ".join(bad)
    return True, f"waivers for this card: {len(mine)} valid"


def report(results, blocked):
    for ok, msg in results:
        print(("ok    " if ok else "FAIL  ") + msg)
    if all(ok for ok, _ in results):
        print("PASS")
        return 0
    if blocked:
        print("BLOCKED")
        return 1
    print("FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
