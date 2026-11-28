"""F9 of docs/PLAN_FULL_API.md: checks one factory card before its result is accepted.

Usage:  python -I scripts/check_card.py cards/todo/<card>.md

Prints one line per check and a final verdict:
  PASS     every static check passed and the card's test command exited 0
  FAIL     a rule was broken (protected file changed, denied name used, file too long, ...)
  BLOCKED  the static checks passed but the test command cannot run yet (for example the F5 runner is missing)

Exit code: 0 only for PASS. Anything else is not accepted.
"""
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
REQUIRED = ["Members", "Edit only", "Read", "Tests", "Run", "Done when"]
PROTECTED_PREFIXES = (
    "tests/", "templates/", "docs/recipes/", "scripts/", "src/", "docs/API_COVERAGE.csv",
    "docs/API_INVENTORY.csv", "docs/API_USAGE.csv", "docs/adr/", "cards/",
)
# Names from PLAN_FULL_API.md section 2.2. Their use in an Api* class is a FAIL, whatever the allowlist says.
DENIED = [
    "__ph_call", "globalThis", "XStorable", "XComponentLoader", "XDispatchHelper", "XDispatchProvider",
    "XSimpleFileAccess", "createInstance", "createInstanceWithContext", "getServiceManager",
    "XConfigurationProvider", "eval(", "new Function", "XScript", "XMacro", "system(", "ShellExecute",
]
MAX_LINES = 400
MAX_MEMBERS = 10
LINE_RE = re.compile(r"^(?P<key>[A-Za-z][A-Za-z ]*?):\s*(?P<value>.*)$")


def parse_card(path):
    """Return {field: value} from the 'Key: value' lines at the top of the card."""
    fields = {}
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            m = LINE_RE.match(raw.strip())
            if m and m.group("key") in REQUIRED:
                fields[m.group("key")] = m.group("value").strip()
    return fields


def split_paths(value):
    return [p.strip() for p in value.split(",") if p.strip()]


def git_changed(paths):
    """Return the subset of paths that differ from HEAD (staged, unstaged or untracked)."""
    if not paths:
        return []
    out = subprocess.run(["git", "status", "--porcelain", "--", *paths], cwd=ROOT,
                         capture_output=True, text=True, check=False).stdout
    changed = []
    for line in out.splitlines():
        changed.append(line[3:].strip().strip('"'))
    return changed


def main(argv):
    if len(argv) != 1:
        print("usage: check_card.py <card.md>")
        return 2
    card = os.path.abspath(argv[0])
    if not os.path.isfile(card):
        print("FAIL card: file not found:", card)
        return 1
    results = []  # (ok, message)

    fields = parse_card(card)
    missing = [k for k in REQUIRED if k not in fields]
    results.append((not missing, "card fields present" if not missing else "card missing fields: " + ", ".join(missing)))
    if missing:
        return report(results, blocked=False)

    members = [m for m in split_paths(fields["Members"])]
    results.append((len(members) <= MAX_MEMBERS, f"members {len(members)} <= {MAX_MEMBERS}"))

    edit_files = split_paths(fields["Edit only"])
    bad_edit = [p for p in edit_files if p.startswith(PROTECTED_PREFIXES)]
    results.append((not bad_edit and bool(edit_files),
                    "edit scope avoids protected paths" if not bad_edit else "protected paths in Edit only: " + ", ".join(bad_edit)))

    test_files = split_paths(fields["Tests"])
    # templates/ and docs/recipes/ are protected by the edit-scope rule above, so only tests and expectations need the git check.
    protected_changed = git_changed([p for p in test_files + ["tests/expected"]])
    results.append((not protected_changed, "tests and expectations unchanged" if not protected_changed
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
    results.append((not too_long, f"edited files <= {MAX_LINES} lines and present" if not too_long
                    else "file problems: " + ", ".join(too_long)))
    results.append((not denied_hits, "no denied names in edited files" if not denied_hits
                    else "denied names: " + "; ".join(denied_hits)))

    waiver_ok, waiver_msg = check_waivers(os.path.splitext(os.path.basename(card))[0], members)
    results.append((waiver_ok, waiver_msg))

    static_ok = all(ok for ok, _ in results)
    if not static_ok:
        return report(results, blocked=False)

    run_cmd = fields["Run"]
    runner = run_cmd_target(run_cmd)
    if runner and not os.path.exists(os.path.join(ROOT, runner)):
        results.append((False, f"test runner missing: {runner} (F5 not built yet)"))
        return report(results, blocked=True)

    proc = subprocess.run(shlex.split(run_cmd), cwd=ROOT, capture_output=True, text=True, check=False)
    tail = (proc.stdout + proc.stderr).strip().splitlines()[-1:] or [""]
    results.append((proc.returncode == 0, f"test command exit {proc.returncode}: {tail[0]}"))
    return report(results, blocked=False)


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
    mine = [r for r in rows if r["card"].strip() == card_name]
    for r in mine:
        if r["rule"].strip() not in rules:
            bad.append(f"{r['member']}: unknown rule {r['rule']}")
        if not r["reason"].strip():
            bad.append(f"{r['member']}: empty reason")
        if r["member"].strip() not in members:
            bad.append(f"{r['member']}: not a member of this card")
    if bad:
        return False, "waivers invalid: " + "; ".join(bad)
    return True, f"waivers for this card: {len(mine)} valid"


def run_cmd_target(cmd):
    """The script path a Run line starts with, for example tests/run_member.py."""
    for tok in shlex.split(cmd):
        if tok.endswith(".py") or tok.endswith(".ps1") or tok.endswith(".js"):
            return tok
    return None


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
