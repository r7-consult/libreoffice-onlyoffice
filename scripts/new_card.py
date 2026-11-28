"""F7 of docs/PLAN_FULL_API.md: generates one factory card with its member stubs and generated tests.

Usage:
  python -I scripts/new_card.py --area cell --class ApiRange --n 3 --kind getter \
      --items "GetValue:Value,GetFormula:Formula"

  --kind   getter | setter | collection | constructor | execute
  --items  Name:UnoProperty for getter/setter; Name:Iface:Method for collection/constructor/execute
           (for execute, Name is the executeMethod name)

Rules enforced here (see PLAN_FULL_API.md section 4):
  - The class file must already exist: Sonnet writes the first card of every new class.
  - A member block already present in the class file is skipped, never rewritten.
  - An existing test file is skipped, never rewritten.
  - Every name must exist in docs/API_INVENTORY.csv, so the card traces back to the sources.
  - Nothing is run: running the card is scripts/check_card.py.
"""
import argparse
import csv
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TEMPLATES = os.path.join(ROOT, "templates")
INVENTORY = os.path.join(ROOT, "docs", "API_INVENTORY.csv")
KINDS = {"getter", "setter", "collection", "constructor", "execute"}


def load_inventory():
    """Return {member name: row id} for every inventory row, so each name must be known."""
    with open(INVENTORY, newline="", encoding="utf-8") as fh:
        return {row["member"]: row["id"] for row in csv.DictReader(fh)}


def parse_items(kind, text):
    """Turn --items into a list of dicts with NAME, PROP, IFACE, METHOD, RETURNS."""
    items = []
    for raw in text.split(","):
        parts = [p.strip() for p in raw.split(":")]
        if kind in ("getter", "setter"):
            if len(parts) != 2:
                raise ValueError(f"expected Name:Property, got {raw!r}")
            items.append({"NAME": parts[0], "PROP": parts[1]})
        else:
            if len(parts) != 3:
                raise ValueError(f"expected Name:Iface:Method, got {raw!r}")
            items.append({"NAME": parts[0], "IFACE": parts[1], "METHOD": parts[2], "PROP": ""})
    return items


def read_template(name):
    with open(os.path.join(TEMPLATES, name), encoding="utf-8") as fh:
        return fh.read()


def fill(text, values):
    for key, val in values.items():
        text = text.replace("{{" + key + "}}", val)
    return text


def add_member_blocks(class_path, cls, kind, items):
    """Append each member block that is not yet in the class file. Returns the names added."""
    with open(class_path, encoding="utf-8") as fh:
        existing = fh.read()
    template = read_template(f"member_{kind}.js")
    added = []
    chunks = []
    for it in items:
        if f"// BEGIN {cls}.{it['NAME']}" in existing or f"// BEGIN executeMethod.{it['NAME']}" in existing:
            continue
        values = {"CLASS": cls, "RETURNS": cls, **it}
        chunks.append(fill(template, values))
        added.append(it["NAME"])
    if chunks:
        with open(class_path, "a", encoding="utf-8", newline="\n") as fh:
            fh.write("\n" + "\n".join(chunks))
    return added


def write_tests(test_dir, cls, kind, items, ids, extras):
    """Create one test file per member. Existing files are kept. Returns the relative paths."""
    template = read_template(f"test_{kind}.js")
    os.makedirs(test_dir, exist_ok=True)
    paths = []
    for it in items:
        name = it["NAME"]
        rel = os.path.relpath(os.path.join(test_dir, name + ".js"), ROOT).replace("\\", "/")
        full = os.path.join(ROOT, rel)
        paths.append(rel)
        if os.path.exists(full):
            continue
        values = {"CLASS": cls, "ID": ids[name], **extras, **it}
        with open(full, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(fill(template, values))
    return paths


def write_card(area, cls, n, kind, items, class_rel, test_paths):
    card_dir = os.path.join(ROOT, "cards", "todo")
    os.makedirs(card_dir, exist_ok=True)
    name = f"{area}-{cls}-{n:02d}"
    path = os.path.join(card_dir, name + ".md")
    if os.path.exists(path):
        raise FileExistsError(path)
    names = ", ".join(f"{cls}.{it['NAME']}" for it in items)
    read = "docs/recipes/" + {"cell": "calc", "word": "writer", "slide": "impress"}.get(area, "common") + ".md"
    text = (
        f"# Card {name}\n"
        f"Members: {names}\n"
        f"Edit only: {class_rel}\n"
        f"Read: {read}\n"
        f"Tests: {', '.join(test_paths)}\n"
        f"Run: python -I tests/run_member.py --card cards/todo/{name}.md\n"
        f"Done when: python -I scripts/check_card.py cards/todo/{name}.md prints PASS\n"
    )
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return os.path.relpath(path, ROOT).replace("\\", "/")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--area", required=True, choices=["cell", "word", "slide", "pdf", "form", "common"])
    ap.add_argument("--class", dest="cls", required=True)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--kind", required=True, choices=sorted(KINDS))
    ap.add_argument("--items", required=True)
    ap.add_argument("--setup", default="api.GetDocument()",
                    help="JS expression that returns the test target, for example api.GetActiveSheet().GetRange(\"A1\")")
    ap.add_argument("--arg", default="\"sample\"", help="JS literal passed to constructor and setter tests")
    ap.add_argument("--readback", default="\"set-ok\"",
                    help="JS expression read after a setter runs, for example ctx.target.GetValue()")
    args = ap.parse_args(argv)
    test_extras = {"SETUP": args.setup, "ARG": args.arg, "READBACK": args.readback}

    class_rel = f"extension/js/api/{args.area}/{args.cls}.js"
    class_path = os.path.join(ROOT, class_rel)
    if not os.path.isfile(class_path):
        print(f"refused: {class_rel} does not exist. Sonnet writes the first card of a new class.", file=sys.stderr)
        return 1
    try:
        items = parse_items(args.kind, args.items)
    except ValueError as e:
        print("refused:", e, file=sys.stderr)
        return 1
    if len(items) > 10:
        print("refused: a card holds at most 10 members", file=sys.stderr)
        return 1

    inventory = load_inventory()
    unknown = [it["NAME"] for it in items if it["NAME"] not in inventory]
    if unknown:
        print("refused: not in docs/API_INVENTORY.csv:", ", ".join(unknown), file=sys.stderr)
        return 1
    ids = {it["NAME"]: inventory[it["NAME"]] for it in items}

    added = add_member_blocks(class_path, args.cls, args.kind, items)
    test_dir = os.path.join(ROOT, "tests", "members", args.area, args.cls)
    test_paths = write_tests(test_dir, args.cls, args.kind, items, ids, test_extras)
    card = write_card(args.area, args.cls, args.n, args.kind, items, class_rel, test_paths)
    print(f"members added to {class_rel}: {', '.join(added) or 'none (already present)'}")
    print(f"tests: {len(test_paths)} in tests/members/{args.area}/{args.cls}/")
    print(f"card: {card}")
    print("Next: commit the generated tests before running the card, so check_card.py sees them as unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
