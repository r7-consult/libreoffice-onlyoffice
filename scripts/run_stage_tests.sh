#!/usr/bin/env bash
# Runs the tests that need no office and no compiled library, then prints a summary:
#   1. every tests/test_*.js with node
#   2. py_compile of every tests/*.py and scripts/*.py
#   3. tests/check_xml.py (XML well-formedness; the schema part is skipped without LO_SRC)
#
# NOT RUN YET. Written without a Linux host.
#
# NOT run here: tests/test_stage*.py. They start a real office and use Windows tools (see docs/LINUX_BUILD.md).
#
# Settings: LO_SRC (optional) = a LibreOffice source tree; tests/test_allowlist.js and tests/check_xml.py use it.
#           Without it they report SKIPPED.
# Exit code: 0 when nothing failed, 1 otherwise. SKIPPED is not a failure.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

die() { echo "ERROR: $*" >&2; exit 1; }
need_tool() { command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1 (${2:-install it with your package manager})"; }

need_tool node "apt-get install nodejs"
need_tool python3 "apt-get install python3"

cd "${ROOT}"
echo "node $(node --version), $(python3 --version 2>&1)"

SUMMARY=()
FAILED=0

# --- 1. Node tests ------------------------------------------------------------
shopt -s nullglob
JS_TESTS=(tests/test_*.js)
[ "${#JS_TESTS[@]}" -gt 0 ] || die "no tests/test_*.js found in ${ROOT}"

for t in "${JS_TESTS[@]}"; do
    echo
    echo "+ node ${t}"
    if OUT="$(node "${t}" 2>&1)"; then RC=0; else RC=$?; fi
    printf '%s\n' "${OUT}"
    if [ "${RC}" -ne 0 ]; then
        SUMMARY+=("FAIL     ${t} (exit ${RC})")
        FAILED=1
    elif grep -q 'SKIPPED' <<<"${OUT}"; then
        SUMMARY+=("SKIPPED  ${t}")
    else
        SUMMARY+=("PASS     ${t}")
    fi
done

# --- 2. py_compile ------------------------------------------------------------
# -X pycache_prefix keeps the .pyc files out of the source folders.
PYCACHE="${ROOT}/build/pycache"
mkdir -p "${PYCACHE}"
PY_FILES=(tests/*.py scripts/*.py)
echo
for f in "${PY_FILES[@]}"; do
    echo "+ python3 -I -X pycache_prefix=${PYCACHE} -m py_compile ${f}"
    if OUT="$(python3 -I -X "pycache_prefix=${PYCACHE}" -m py_compile "${f}" 2>&1)"; then
        SUMMARY+=("PASS     py_compile ${f}")
    else
        printf '%s\n' "${OUT}"
        SUMMARY+=("FAIL     py_compile ${f}")
        FAILED=1
    fi
done

# --- 3. XML check -------------------------------------------------------------
echo
echo "+ python3 -I tests/check_xml.py"
if OUT="$(python3 -I tests/check_xml.py 2>&1)"; then RC=0; else RC=$?; fi
printf '%s\n' "${OUT}"
if [ "${RC}" -ne 0 ]; then
    SUMMARY+=("FAIL     tests/check_xml.py (exit ${RC})")
    FAILED=1
elif grep -qi 'skipped' <<<"${OUT}"; then
    SUMMARY+=("PASS     tests/check_xml.py (schema check skipped, set LO_SRC to a LibreOffice source tree)")
else
    SUMMARY+=("PASS     tests/check_xml.py")
fi

# --- summary ------------------------------------------------------------------
echo
echo "==== summary ===="
for line in "${SUMMARY[@]}"; do
    echo "${line}"
done
echo "SKIPPED  tests/test_stage*.py (need a Linux port, see docs/LINUX_BUILD.md)"
if [ "${FAILED}" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS (no failures)"
