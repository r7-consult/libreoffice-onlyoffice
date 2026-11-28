#!/usr/bin/env bash
# Builds dist/pluginhost-linux_x86_64.oxt from extension/ and the built shared library.
#
# NOT RUN YET. Written without a Linux host.
#
# The tracked files in extension/ stay Windows files. Three of them are rewritten for Linux in the STAGING copy
# (build/oxt) with sed:
#   META-INF/manifest.xml   media type platform=windows_x86_64  ->  platform=linux_x86_64
#   pluginhost.components   uri="pluginhost.dll"                ->  uri="<name of the built library>"
#   description.xml         UPDATED_SUPPORTED_PLATFORM          ->  linux_x86_64
#
# About the library name in the components file: cppuhelper passes the uri to osl::Module as it is
# (cppuhelper/source/shlib.cxx: osl::Module mod(uri, ...)); it adds neither "lib" nor ".so". The SDK example
# (odk/examples/cpp/custompanel/Makefile) also names the file in uri="Linux_x86_64/<name>.uno.so". So the uri must be
# the exact file name that CMake produced. This script reads that name from the library it finds.
#
# Settings (environment variables, all optional):
#   PH_BUILD_DIR   CMake build folder. Default: <repo>/build
#   PH_LIB         path of the built library. Default: searched in the build folder (see build.sh)
#   PH_PLATFORM    platform string. Default from uname -m: linux_x86_64 or linux_aarch64
#                  (both are accepted by desktop/source/deployment/misc/dp_platform.cxx)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

die() { echo "ERROR: $*" >&2; exit 1; }
run() { printf '+'; printf ' %q' "$@"; printf '\n'; "$@"; }
# run_to FILE CMD...: like run, but the standard output of CMD goes to FILE.
run_to() { local out="$1"; shift; printf '+'; printf ' %q' "$@"; printf ' > %q\n' "${out}"; "$@" > "${out}"; }
need_tool() { command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1 (${2:-install it with your package manager})"; }

BUILD_DIR="${PH_BUILD_DIR:-${ROOT}/build}"
EXT_DIR="${ROOT}/extension"
STAGE="${BUILD_DIR}/oxt"
DIST_DIR="${ROOT}/dist"

need_tool zip "apt-get install zip"
need_tool sed
need_tool find

[ -d "${EXT_DIR}" ] || die "extension folder not found: ${EXT_DIR}"
for f in META-INF/manifest.xml pluginhost.components description.xml; do
    [ -f "${EXT_DIR}/${f}" ] || die "missing tracked file: ${EXT_DIR}/${f}"
done

# --- platform -----------------------------------------------------------------
if [ -n "${PH_PLATFORM:-}" ]; then
    PLATFORM="${PH_PLATFORM}"
else
    case "$(uname -m)" in
        x86_64)  PLATFORM="linux_x86_64" ;;
        aarch64) PLATFORM="linux_aarch64" ;;
        *) die "unsupported machine $(uname -m); set PH_PLATFORM to a name from dp_platform.cxx" ;;
    esac
fi
case "${PLATFORM}" in
    linux_*) ;;
    *) die "PH_PLATFORM must start with linux_, got: ${PLATFORM}" ;;
esac
case "${PLATFORM}" in
    *[!a-z0-9_]*) die "PH_PLATFORM contains characters outside a-z, 0-9, _: ${PLATFORM}" ;;
esac

# --- library ------------------------------------------------------------------
LIB="${PH_LIB:-}"
if [ -z "${LIB}" ]; then
    for candidate in \
        "${BUILD_DIR}/lib/libpluginhost.so" \
        "${BUILD_DIR}/libpluginhost.so" \
        "${BUILD_DIR}/lib/pluginhost.so" \
        "${BUILD_DIR}/pluginhost.so" \
        "${BUILD_DIR}/bin/libpluginhost.so" \
        "${BUILD_DIR}/bin/pluginhost.so"; do
        if [ -f "${candidate}" ]; then
            LIB="${candidate}"
            break
        fi
    done
fi
if [ -z "${LIB}" ]; then
    LIB="$(find "${BUILD_DIR}" -maxdepth 2 -type f -name '*pluginhost.so' -not -path '*/quickjs-ng/*' -not -path "${STAGE}/*" | head -n 1 || true)"
fi
[ -n "${LIB}" ] && [ -f "${LIB}" ] || die "built library not found under ${BUILD_DIR}. Run scripts/build.sh first (or set PH_LIB)."

LIB_NAME="$(basename "${LIB}")"
case "${LIB_NAME}" in
    *[!A-Za-z0-9._-]*) die "unexpected characters in library file name: ${LIB_NAME}" ;;
esac

OUT="${DIST_DIR}/pluginhost-${PLATFORM}.oxt"
echo "platform=${PLATFORM}"
echo "library=${LIB}"
echo "staging=${STAGE}"
echo "output=${OUT}"

# --- staging ------------------------------------------------------------------
# STAGE is under BUILD_DIR/oxt; refuse anything that does not end in /oxt before deleting it.
case "${STAGE}" in
    */oxt) ;;
    *) die "refusing to delete staging folder with unexpected name: ${STAGE}" ;;
esac
run rm -rf "${STAGE}"
run mkdir -p "${STAGE}" "${DIST_DIR}"
run cp -a "${EXT_DIR}/." "${STAGE}/"
run cp -p "${LIB}" "${STAGE}/${LIB_NAME}"

# --- Linux versions of the three platform files (from the tracked Windows files) ----------
run_to "${STAGE}/META-INF/manifest.xml" \
    sed -e "s|platform=windows_x86_64|platform=${PLATFORM}|g" "${EXT_DIR}/META-INF/manifest.xml"

run_to "${STAGE}/pluginhost.components" \
    sed -e "s|uri=\"pluginhost\\.dll\"|uri=\"${LIB_NAME}\"|g" "${EXT_DIR}/pluginhost.components"

run_to "${STAGE}/description.xml" \
    sed -e "s|UPDATED_SUPPORTED_PLATFORM|${PLATFORM}|g" "${EXT_DIR}/description.xml"

# --- check the result ---------------------------------------------------------
grep -q "platform=${PLATFORM}\"" "${STAGE}/META-INF/manifest.xml" \
    || die "manifest.xml: platform=${PLATFORM} not found after sed (did the tracked file change?)"
if grep -qi 'windows' "${STAGE}/META-INF/manifest.xml"; then
    die "manifest.xml still mentions windows"
fi
grep -q "uri=\"${LIB_NAME}\"" "${STAGE}/pluginhost.components" \
    || die "pluginhost.components: uri=\"${LIB_NAME}\" not found after sed (did the tracked file change?)"
if grep -q '\.dll' "${STAGE}/pluginhost.components"; then
    die "pluginhost.components still mentions .dll"
fi
grep -q "<platform value=\"${PLATFORM}\"" "${STAGE}/description.xml" \
    || die "description.xml: <platform value=\"${PLATFORM}\"> not found after sed"
if grep -q 'UPDATED_SUPPORTED_PLATFORM' "${STAGE}/description.xml"; then
    die "description.xml still contains UPDATED_SUPPORTED_PLATFORM"
fi
[ -f "${STAGE}/${LIB_NAME}" ] || die "library missing in staging folder"

# --- zip ----------------------------------------------------------------------
run rm -f "${OUT}"
# File list relative to the staging folder, sorted, so the archive has the files at its root.
(
    cd "${STAGE}"
    find . -type f | sed -e 's|^\./||' | LC_ALL=C sort > "${BUILD_DIR}/oxt-files.txt"
    printf '+ zip -X %q -@ < %q\n' "${OUT}" "${BUILD_DIR}/oxt-files.txt"
    zip -X "${OUT}" -@ < "${BUILD_DIR}/oxt-files.txt"
)

echo
echo "Files added to ${OUT}:"
cat "${BUILD_DIR}/oxt-files.txt"
echo
run ls -l "${OUT}"
