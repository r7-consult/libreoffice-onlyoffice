"""Checks the extension's XML files.

1. Every XML file parses (well-formedness).
2. Every <prop> inside a set entry of our .xcu files is declared in the matching LibreOffice schema template,
   with the same oor:type when we state one. Needs the LibreOffice source tree: LO_SRC (default C:\\Oracle\\lo);
   step 2 is skipped (not failed) when the tree is absent.

Run: python -I tests/check_xml.py
"""
import glob
import os
import sys
import xml.dom.minidom

OOR = "http://openoffice.org/2001/registry"
HERE = os.path.dirname(os.path.abspath(__file__))
EXT = os.path.join(HERE, "..", "extension")
LO_SRC = os.environ.get("LO_SRC", r"C:\Oracle\lo")
SCHEMA = os.path.join(LO_SRC, "officecfg", "registry", "schema", "org", "openoffice", "Office")

# xcu file -> (schema file, {set name in xcu: template group name in schema})
CHECKS = {
    "Sidebar.xcu": (os.path.join(SCHEMA, "UI", "Sidebar.xcs"), {"DeckList": "Deck", "PanelList": "Panel"}),
    "Factories.xcu": (os.path.join(SCHEMA, "UI", "Factories.xcs"), {"UIElementFactories": "FactoryType"}),
    "ProtocolHandler.xcu": (os.path.join(SCHEMA, "ProtocolHandler.xcs"), {"HandlerSet": "Handler"}),
}


def oor(el, name):
    return el.getAttributeNS(OOR, name) or el.getAttribute("oor:" + name)


def children(el, tag):
    return [c for c in el.childNodes if c.nodeType == c.ELEMENT_NODE and c.tagName == tag]


def template_props(schema_path, group):
    doc = xml.dom.minidom.parse(schema_path)
    for templates in doc.getElementsByTagName("templates"):
        for g in children(templates, "group"):
            if oor(g, "name") == group:
                return {oor(p, "name"): oor(p, "type") for p in children(g, "prop")}
    raise KeyError(group)


CHECKED = [0]


def check_props(xcu_path, schema_path, sets):
    errors = []
    doc = xml.dom.minidom.parse(xcu_path)
    for node in doc.getElementsByTagName("node"):
        group = sets.get(oor(node, "name"))
        if not group:
            continue
        allowed = template_props(schema_path, group)
        for entry in children(node, "node"):
            for prop in children(entry, "prop"):
                CHECKED[0] += 1
                name, typ = oor(prop, "name"), oor(prop, "type")
                if name not in allowed:
                    errors.append("%s: %s/%s: prop %s not in schema %s" % (
                        os.path.basename(xcu_path), oor(node, "name"), oor(entry, "name"), name, group))
                elif typ and allowed[name] and typ != allowed[name]:
                    errors.append("%s: prop %s has type %s, schema says %s" % (
                        os.path.basename(xcu_path), name, typ, allowed[name]))
    return errors


def main():
    files = glob.glob(os.path.join(EXT, "*.xcu")) + glob.glob(os.path.join(EXT, "*.xml")) \
        + glob.glob(os.path.join(EXT, "*.components")) + glob.glob(os.path.join(EXT, "META-INF", "*.xml"))
    errors = []
    for f in sorted(files):
        try:
            xml.dom.minidom.parse(f)
        except Exception as e:  # noqa: BLE001
            errors.append("not well-formed: %s: %s" % (os.path.relpath(f, EXT), e))

    schema_checked = 0
    if os.path.isdir(SCHEMA):
        for xcu, (schema, sets) in CHECKS.items():
            errors += check_props(os.path.join(EXT, xcu), schema, sets)
            schema_checked += 1
    else:
        print("check_xml: schema check skipped, no LibreOffice source at", LO_SRC)

    for e in errors:
        print("ERROR", e)
    print("check_xml:", "PASS" if not errors else "FAIL",
          "%d files parsed, %d xcu files / %d props checked against schema" % (len(files), schema_checked, CHECKED[0]))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
