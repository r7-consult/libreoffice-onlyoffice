"""Parses every XML file of the extension (well-formedness only). Run: python tests/check_xml.py"""
import glob
import os
import sys
import xml.dom.minidom

root = os.path.join(os.path.dirname(__file__), "..", "extension")
files = glob.glob(os.path.join(root, "*.xcu")) + glob.glob(os.path.join(root, "*.xml")) \
    + glob.glob(os.path.join(root, "*.components")) + glob.glob(os.path.join(root, "META-INF", "*.xml"))
bad = 0
for f in sorted(files):
    try:
        xml.dom.minidom.parse(f)
    except Exception as e:  # noqa: BLE001
        bad += 1
        print("BAD", os.path.relpath(f, root), e)
print("check_xml:", "PASS" if not bad else "FAIL", len(files), "files")
sys.exit(1 if bad else 0)
