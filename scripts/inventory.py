"""F1 + F2 of docs/PLAN_FULL_API.md: API inventory and usage from public ONLYOFFICE sources.

Stdlib only, no build step. Usage:

  python scripts/inventory.py --examples <office-js-api clone> --plugins <sdkjs-plugins clone> --out docs

Writes:
  docs/API_INVENTORY.csv  one row per member name found in call sites
  docs/API_USAGE.csv      one row per member name: how many plugins call it

Limits (deliberate, see PLAN_FULL_API.md section 10, entry F1-src):
  - Names come from call sites (".Name(" and executeMethod("Name")), not from sdkjs declarations.
    Signatures, properties and events are not extracted yet.
  - read|edit is written as UNCLASSIFIED. It needs a classification pass before the bridge allowlist exists.
  - PascalCase is the heuristic for ONLYOFFICE methods; a few non-ONLYOFFICE calls may slip in.
"""
import argparse
import csv
import os
import re
import sys
from collections import defaultdict

MEMBER = re.compile(r"\.([A-Z][A-Za-z0-9_]*)\s*\(")
EXEC = re.compile(r"executeMethod\(\s*[\"']([A-Za-z0-9_]+)[\"']")
AREA_BY_FOLDER = {"Cell": "cell", "Word": "word", "Slide": "slide", "Pdf": "pdf",
                  "Forms": "form", "Enumerations": "common", "Plugins": "plugin"}
SKIP_DIRS = {".git", "node_modules", "archive"}
TEXT_EXT = {".js", ".html", ".htm", ".ts", ".jsx"}
MAX_BYTES = 2 * 1024 * 1024  # minified bundles are skipped


def walk_sources(root):
    """Yield (relative_path, text) for text source files under root."""
    for dirpath, dirnames, filenames in os.walk(root):
        # Sorted, so first_source in the CSV does not depend on the file system's listing order.
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for name in sorted(filenames):
            if os.path.splitext(name)[1].lower() not in TEXT_EXT:
                continue
            full = os.path.join(dirpath, name)
            try:
                if os.path.getsize(full) > MAX_BYTES:
                    continue
                with open(full, encoding="utf-8", errors="replace") as fh:
                    yield os.path.relpath(full, root).replace("\\", "/"), fh.read()
            except OSError:
                continue


def members_in(text):
    """Return (set of method names, set of executeMethod names) found in one file."""
    return set(MEMBER.findall(text)), set(EXEC.findall(text))


def build_inventory(examples_root):
    rows = defaultdict(lambda: {"areas": set(), "files": 0, "first": ""})
    exec_rows = defaultdict(lambda: {"areas": set(), "files": 0, "first": ""})
    files_seen = 0
    for rel, text in walk_sources(examples_root):
        files_seen += 1
        parts = rel.split("/")
        # Examples/<Area>/... -> area from the second component; anything else is "other".
        key = parts[1] if parts[0] == "Examples" and len(parts) > 1 else parts[0]
        area = AREA_BY_FOLDER.get(key, "other")
        methods, execs = members_in(text)
        for name in methods:
            r = rows[name]
            r["areas"].add(area)
            r["files"] += 1
            r["first"] = r["first"] or rel
        for name in execs:
            r = exec_rows[name]
            r["areas"].add(area)
            r["files"] += 1
            r["first"] = r["first"] or rel
    return rows, exec_rows, files_seen


def plugin_dirs(plugins_root):
    """Plugin roots are directories that contain config.json, skipping archive and hidden dirs."""
    found = []
    for dirpath, dirnames, filenames in os.walk(plugins_root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS and not d.startswith("."))
        if "config.json" in filenames:
            found.append(dirpath)
            dirnames[:] = []  # do not descend into a plugin
    return sorted(found)


def build_usage(plugins_root):
    """Map member name -> set of plugin ids that call it; same for executeMethod names."""
    method_users = defaultdict(set)
    exec_users = defaultdict(set)
    plugins = plugin_dirs(plugins_root)
    for pdir in plugins:
        pid = os.path.relpath(pdir, plugins_root).replace("\\", "/")
        for _, text in walk_sources(pdir):
            methods, execs = members_in(text)
            for name in methods:
                method_users[name].add(pid)
            for name in execs:
                exec_users[name].add(pid)
    return method_users, exec_users, len(plugins)


def write_inventory(path, rows, exec_rows):
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["id", "kind", "member", "editors", "example_files", "in_api_examples",
                    "signature", "read_edit", "first_source"])
        n = 0
        for kind, table in (("method", rows), ("executeMethod", exec_rows)):
            for name in sorted(table, key=lambda k: (-table[k]["files"], k)):
                n += 1
                r = table[name]
                w.writerow([f"M{n:04d}", kind, name, ";".join(sorted(r["areas"])), r["files"],
                            yes_no(in_api_examples(r)), "", "UNCLASSIFIED", r["first"]])
    return n


def in_api_examples(row):
    """True when the name occurs in an ONLYOFFICE API example folder (not only in plugin code)."""
    return bool(row["areas"] - {"other", "plugin"})


def yes_no(flag):
    return "yes" if flag else "no"


def write_usage(path, method_users, exec_users, plugin_count, api_names):
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["member", "kind", "plugin_count", "plugin_share", "in_api_examples", "plugins"])
        items = [("method", k, v) for k, v in method_users.items()] + \
                [("executeMethod", k, v) for k, v in exec_users.items()]
        items.sort(key=lambda t: (-len(t[2]), t[1]))
        for kind, name, users in items:
            share = len(users) / plugin_count if plugin_count else 0.0
            # Names absent from the API example folders are most likely helpers bundled in plugins.
            api = api_names.get((kind, name), False)
            w.writerow([name, kind, len(users), f"{share:.3f}", yes_no(api), ";".join(sorted(users))])
        return len(items)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--examples", required=True, help="office-js-api clone (Examples/ is read)")
    ap.add_argument("--plugins", required=True, help="sdkjs-plugins clone")
    ap.add_argument("--out", default="docs", help="output directory")
    args = ap.parse_args(argv)

    rows, exec_rows, files_seen = build_inventory(args.examples)
    if files_seen == 0:
        print("inventory: no source files under", args.examples, file=sys.stderr)
        return 1
    method_users, exec_users, plugin_count = build_usage(args.plugins)
    if plugin_count == 0:
        print("usage: no plugin directories (config.json) under", args.plugins, file=sys.stderr)
        return 1

    os.makedirs(args.out, exist_ok=True)
    inv = write_inventory(os.path.join(args.out, "API_INVENTORY.csv"), rows, exec_rows)
    api_names = {("method", n): in_api_examples(r) for n, r in rows.items()}
    api_names.update({("executeMethod", n): in_api_examples(r) for n, r in exec_rows.items()})
    use = write_usage(os.path.join(args.out, "API_USAGE.csv"), method_users, exec_users, plugin_count, api_names)
    print(f"inventory: {files_seen} example files, {inv} rows -> {args.out}/API_INVENTORY.csv")
    print(f"usage: {plugin_count} plugins, {use} rows -> {args.out}/API_USAGE.csv")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
