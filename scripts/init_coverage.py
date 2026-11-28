"""Creates docs/API_COVERAGE.csv from docs/API_INVENTORY.csv (one row per inventory row).

Usage: python -I scripts/init_coverage.py

Every row starts as TODO. Only a card (through check_card.py) may move a row to DONE or MAPPED-LIMITED, and only
together with a passing test. Refuses to overwrite an existing coverage file, so progress is never reset by accident.
Columns follow PLAN_FULL_API.md section 0 and ADR-003 (the mlr column).
"""
import csv
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
INVENTORY = os.path.join(ROOT, "docs", "API_INVENTORY.csv")
COVERAGE = os.path.join(ROOT, "docs", "API_COVERAGE.csv")
COLUMNS = ["id", "kind", "member", "editors", "state", "mlr", "card", "test"]


def main():
    if os.path.exists(COVERAGE):
        print("refused: docs/API_COVERAGE.csv already exists; it is never reset by this script")
        return 1
    with open(INVENTORY, newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    with open(COVERAGE, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(COLUMNS)
        for r in rows:
            w.writerow([r["id"], r["kind"], r["member"], r["editors"], "TODO", "", "", ""])
    print(f"wrote {len(rows)} rows to docs/API_COVERAGE.csv (all TODO)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
