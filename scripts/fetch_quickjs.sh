#!/usr/bin/env bash
# Downloads the quickjs-ng 0.18.0 source into third_party/quickjs-ng (the folder src/CMakeLists.txt expects).
#
# NOT RUN YET. Written without a Linux host.
#
# The URL is the one in config/versions.json (third_party.quickjs_ng.url).
#
# Hash check:
#   The default value below was computed on Windows from third_party/quickjs-ng.tar.gz, a copy downloaded on
#   2026-11-26 from the same URL. It was NOT compared with any published hash. GitHub can regenerate archive files,
#   so a mismatch does not prove tampering, but it must be looked at before you go on.
#   - QUICKJS_SHA256=<hash>   use another expected hash.
#   - QUICKJS_SHA256=         (set but empty) no expected hash: the script refuses unless PH_ALLOW_UNVERIFIED=1.
#   - PH_ALLOW_UNVERIFIED=1   continue even when the hash is missing or different (the computed hash is printed).
#   - PH_FETCH_FORCE=1        replace an existing third_party/quickjs-ng.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

die() { echo "ERROR: $*" >&2; exit 1; }
run() { printf '+'; printf ' %q' "$@"; printf '\n'; "$@"; }
need_tool() { command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1 (${2:-install it with your package manager})"; }

QUICKJS_VERSION="0.18.0"
QUICKJS_URL="https://github.com/quickjs-ng/quickjs/archive/refs/tags/v${QUICKJS_VERSION}.tar.gz"
# PLACEHOLDER-CHECK: replace this value (or export QUICKJS_SHA256) with a hash you have verified yourself.
QUICKJS_SHA256="${QUICKJS_SHA256-28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd}"
ALLOW_UNVERIFIED="${PH_ALLOW_UNVERIFIED:-0}"
FORCE="${PH_FETCH_FORCE:-0}"

THIRD_PARTY="${ROOT}/third_party"
DEST="${THIRD_PARTY}/quickjs-ng"

need_tool curl "apt-get install curl"
need_tool tar "apt-get install tar"
need_tool sha256sum "part of coreutils"

if [ -f "${DEST}/CMakeLists.txt" ] && [ -f "${DEST}/quickjs.h" ] && [ "${FORCE}" != "1" ]; then
    echo "Already present: ${DEST} (set PH_FETCH_FORCE=1 to download again)"
    exit 0
fi

run mkdir -p "${THIRD_PARTY}"

WORK="$(mktemp -d "${THIRD_PARTY}/.fetch-quickjs.XXXXXX")"
cleanup() { rm -rf "${WORK}"; }
trap cleanup EXIT

ARCHIVE="${WORK}/quickjs-ng.tar.gz"
run curl --fail --location --silent --show-error --output "${ARCHIVE}" "${QUICKJS_URL}"

ACTUAL_SHA256="$(sha256sum "${ARCHIVE}" | cut -d ' ' -f 1)"
echo "SHA-256 of the download: ${ACTUAL_SHA256}"

if [ -z "${QUICKJS_SHA256}" ]; then
    if [ "${ALLOW_UNVERIFIED}" = "1" ]; then
        echo "WARNING: no expected hash given, continuing because PH_ALLOW_UNVERIFIED=1"
    else
        die "no expected hash (QUICKJS_SHA256 is empty). Check the hash printed above, then export QUICKJS_SHA256=<hash> or PH_ALLOW_UNVERIFIED=1."
    fi
elif [ "${ACTUAL_SHA256}" = "${QUICKJS_SHA256}" ]; then
    echo "Hash matches the expected value."
else
    echo "Expected:  ${QUICKJS_SHA256}" >&2
    if [ "${ALLOW_UNVERIFIED}" = "1" ]; then
        echo "WARNING: hash differs, continuing because PH_ALLOW_UNVERIFIED=1" >&2
    else
        die "hash differs from the expected value. If you trust the download, export QUICKJS_SHA256=${ACTUAL_SHA256} (or PH_ALLOW_UNVERIFIED=1)."
    fi
fi

run tar -xzf "${ARCHIVE}" -C "${WORK}"
# The GitHub tag archive unpacks to quickjs-<version>/ (checked on the Windows copy of the same archive).
SRC="${WORK}/quickjs-${QUICKJS_VERSION}"
[ -f "${SRC}/CMakeLists.txt" ] || die "unexpected archive layout: ${SRC}/CMakeLists.txt not found"
[ -f "${SRC}/quickjs.h" ] || die "unexpected archive layout: ${SRC}/quickjs.h not found"

if [ -e "${DEST}" ]; then
    run rm -rf "${DEST}"
fi
run mv "${SRC}" "${DEST}"

echo "quickjs-ng ${QUICKJS_VERSION} is in ${DEST}"
