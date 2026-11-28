#!/usr/bin/env bash
# Generates the UNO C++ headers (com/sun/star/...) with cppumaker. Linux counterpart of gen_uno_headers.ps1.
#
# NOT RUN YET. Written without a Linux host; expect to fix paths on the first run.
#
# Inputs, from the installed LibreOffice (Debian/Ubuntu: packages libreoffice-dev, libreoffice-dev-common):
#   <LO_PROGRAM>/types.rdb            UNO base types (URE)
#   <LO_PROGRAM>/types/offapi.rdb     office API types
#   <LO_SDK>/bin/cppumaker            the generator
#
# Settings (environment variables, all optional):
#   LO_SDK         SDK folder.            Default: /usr/lib/libreoffice/sdk
#   LO_PROGRAM     office program folder. Default: /usr/lib/libreoffice/program
#   PH_GEN_DIR     output folder.         Default: <repo>/build/gen   (same name as the CMake variable)
#   LO_TYPES_RDB   explicit path of types.rdb, if it is not in LO_PROGRAM
#   LO_OFFAPI_RDB  explicit path of offapi.rdb, if it is not in LO_PROGRAM/types
#
# Like gen_uno_headers.ps1 this passes no -T type list: every type is generated, so there is no list that can drift
# from the #include lines in src/.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

die() { echo "ERROR: $*" >&2; exit 1; }
run() { printf '+'; printf ' %q' "$@"; printf '\n'; "$@"; }

LO_SDK="${LO_SDK:-/usr/lib/libreoffice/sdk}"
LO_PROGRAM="${LO_PROGRAM:-/usr/lib/libreoffice/program}"
GEN_DIR="${PH_GEN_DIR:-${ROOT}/build/gen}"

echo "LO_SDK=${LO_SDK}"
echo "LO_PROGRAM=${LO_PROGRAM}"
echo "PH_GEN_DIR=${GEN_DIR}"

[ -d "${LO_SDK}" ] || die "LO_SDK folder not found: ${LO_SDK}. Install libreoffice-dev (Debian/Ubuntu) or set LO_SDK."
[ -d "${LO_PROGRAM}" ] || die "LO_PROGRAM folder not found: ${LO_PROGRAM}. Install libreoffice-core or set LO_PROGRAM."

# cppumaker: the SDK folder first, then PATH.
CPPUMAKER="${LO_SDK}/bin/cppumaker"
if [ ! -x "${CPPUMAKER}" ]; then
    if command -v cppumaker >/dev/null 2>&1; then
        CPPUMAKER="$(command -v cppumaker)"
        echo "note: ${LO_SDK}/bin/cppumaker is missing, using ${CPPUMAKER} from PATH"
    else
        die "cppumaker not found in ${LO_SDK}/bin or on PATH. Install libreoffice-dev and libreoffice-dev-common."
    fi
fi

# First existing file of the arguments; prints it. Returns 1 when none exists.
first_file() {
    local f
    for f in "$@"; do
        if [ -f "${f}" ]; then
            printf '%s' "${f}"
            return 0
        fi
    done
    return 1
}

if [ -n "${LO_TYPES_RDB:-}" ]; then
    TYPES_RDB="${LO_TYPES_RDB}"
else
    # First candidate is the upstream layout (odk/settings/std.mk: URE_MISC=OFFICE_PROGRAM_PATH).
    # The other two are older Debian/URE layouts. Unverified guesses; set LO_TYPES_RDB if all fail.
    TYPES_RDB="$(first_file "${LO_PROGRAM}/types.rdb" \
        "/usr/lib/ure/share/misc/types.rdb" \
        "${LO_SDK}/ure-link/share/misc/types.rdb")" \
        || die "types.rdb not found in ${LO_PROGRAM} (or the URE folders). Run: find /usr/lib -name types.rdb  and set LO_TYPES_RDB."
fi
OFFAPI_RDB="${LO_OFFAPI_RDB:-${LO_PROGRAM}/types/offapi.rdb}"

[ -f "${TYPES_RDB}" ] || die "types.rdb not found: ${TYPES_RDB}"
[ -f "${OFFAPI_RDB}" ] || die "offapi.rdb not found: ${OFFAPI_RDB}. Run: find /usr/lib -name offapi.rdb  and set LO_OFFAPI_RDB."

# cppumaker loads the URE libraries (libuno_sal.so.3 and friends) from the office program folder
# (same as odk/config/setsdkenv_unix.sh.in: LD_LIBRARY_PATH=<sdk>/lib:<ure lib dir>).
export LD_LIBRARY_PATH="${LO_PROGRAM}:${LO_SDK}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

run mkdir -p "${GEN_DIR}"

# -Gc: write only files whose content changed. -O<dir>: output root (the tree is <dir>/com/sun/star/...).
run "${CPPUMAKER}" -Gc "-O${GEN_DIR}" "${TYPES_RDB}" "${OFFAPI_RDB}"

# Sanity check: two headers the sources depend on.
for h in com/sun/star/uno/XInterface.hpp com/sun/star/ui/XToolPanel.hpp; do
    [ -f "${GEN_DIR}/${h}" ] || die "cppumaker ran but ${GEN_DIR}/${h} is missing"
done

echo "Generated headers in ${GEN_DIR}"
