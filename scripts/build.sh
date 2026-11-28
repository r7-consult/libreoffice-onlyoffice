#!/usr/bin/env bash
# Configures and builds the pluginhost shared library with CMake (Ninja if present, otherwise Make). Release build.
#
# NOT RUN YET. Written without a Linux host; expect compile errors on the first run.
#
# Order: scripts/fetch_quickjs.sh, scripts/gen_uno_headers.sh, then this script.
#
# Settings (environment variables, all optional):
#   LO_SDK           SDK folder.                  Default: /usr/lib/libreoffice/sdk
#   PH_BUILD_DIR     CMake build folder.          Default: <repo>/build
#   PH_GEN_DIR       generated UNO headers.       Default: <build folder>/gen
#   PH_QUICKJS_DIR   quickjs-ng source folder.    Default: <repo>/third_party/quickjs-ng
#   PH_TEST_HOOKS    ON or OFF.                   Default: OFF. ON compiles the test switches (TestRunner service,
#                    PLUGINHOST_API_DIR); never ship an ON build.
#   PH_JOBS          parallel jobs.               Default: number of CPUs
#
# Output: src/CMakeLists.txt sets PREFIX "" and only RUNTIME_OUTPUT_DIRECTORY (Windows only). On Linux that gives
# <build folder>/pluginhost.so. If CMakeLists.txt is changed to the usual libpluginhost.so in <build folder>/lib, this
# script finds that too. It prints the path it found.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

die() { echo "ERROR: $*" >&2; exit 1; }
run() { printf '+'; printf ' %q' "$@"; printf '\n'; "$@"; }
need_tool() { command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1 (${2:-install it with your package manager})"; }

LO_SDK="${LO_SDK:-/usr/lib/libreoffice/sdk}"
BUILD_DIR="${PH_BUILD_DIR:-${ROOT}/build}"
GEN_DIR="${PH_GEN_DIR:-${BUILD_DIR}/gen}"
QUICKJS_DIR="${PH_QUICKJS_DIR:-${ROOT}/third_party/quickjs-ng}"
TEST_HOOKS="${PH_TEST_HOOKS:-OFF}"

case "${TEST_HOOKS}" in
    ON|OFF) ;;
    *) die "PH_TEST_HOOKS must be ON or OFF, got: ${TEST_HOOKS}" ;;
esac

if [ -n "${PH_JOBS:-}" ]; then
    JOBS="${PH_JOBS}"
elif command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS=2
fi

echo "LO_SDK=${LO_SDK}"
echo "PH_BUILD_DIR=${BUILD_DIR}"
echo "PH_GEN_DIR=${GEN_DIR}"
echo "PH_QUICKJS_DIR=${QUICKJS_DIR}"
echo "PH_TEST_HOOKS=${TEST_HOOKS}"
echo "jobs=${JOBS}"

# --- tools and inputs ---------------------------------------------------------
need_tool cmake "apt-get install cmake"
need_tool pkg-config "apt-get install pkg-config"
if ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
    die "no C++ compiler (c++ or g++) found. apt-get install build-essential"
fi

[ -d "${LO_SDK}" ] || die "LO_SDK folder not found: ${LO_SDK}. Install libreoffice-dev or set LO_SDK."
[ -d "${LO_SDK}/include" ] || die "${LO_SDK}/include not found (package libreoffice-dev-common). Find it with: find /usr -name 'sal' -path '*include*'"
[ -d "${LO_SDK}/lib" ] || die "${LO_SDK}/lib not found. The link step needs libuno_sal.so, libuno_cppu.so and friends there."
[ -f "${GEN_DIR}/com/sun/star/uno/XInterface.hpp" ] || die "generated UNO headers missing in ${GEN_DIR}. Run scripts/gen_uno_headers.sh first."
[ -f "${QUICKJS_DIR}/CMakeLists.txt" ] || die "quickjs-ng not found in ${QUICKJS_DIR}. Run scripts/fetch_quickjs.sh first."

# The Linux web host (src/webhost_linux_webkitgtk.cxx) needs these two pkg-config modules.
pkg-config --exists gtk+-3.0 || die "pkg-config module gtk+-3.0 not found. apt-get install libgtk-3-dev"
pkg-config --exists webkit2gtk-4.1 || die "pkg-config module webkit2gtk-4.1 not found. apt-get install libwebkit2gtk-4.1-dev"
echo "gtk+-3.0 $(pkg-config --modversion gtk+-3.0), webkit2gtk-4.1 $(pkg-config --modversion webkit2gtk-4.1)"

# --- generator ----------------------------------------------------------------
# An existing build folder keeps the generator it was configured with (CMake refuses a different one).
GENERATOR_ARGS=()
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "note: ${BUILD_DIR}/CMakeCache.txt exists, keeping its generator"
elif command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
else
    echo "note: ninja not found, using Unix Makefiles"
    GENERATOR_ARGS=(-G "Unix Makefiles")
fi

# --- configure and build ------------------------------------------------------
run cmake -S "${ROOT}/src" -B "${BUILD_DIR}" ${GENERATOR_ARGS[@]+"${GENERATOR_ARGS[@]}"} \
    -DCMAKE_BUILD_TYPE=Release \
    "-DLO_SDK=${LO_SDK}" \
    "-DPH_GEN_DIR=${GEN_DIR}" \
    "-DPH_QUICKJS_DIR=${QUICKJS_DIR}" \
    "-DPH_TEST_HOOKS=${TEST_HOOKS}"

run cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

# --- find the library ---------------------------------------------------------
LIB=""
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
if [ -z "${LIB}" ]; then
    LIB="$(find "${BUILD_DIR}" -maxdepth 2 -type f -name '*pluginhost.so' -not -path '*/quickjs-ng/*' | head -n 1 || true)"
fi
[ -n "${LIB}" ] || die "build finished but no pluginhost shared library was found under ${BUILD_DIR}"

echo "Built ${LIB}"
run ls -l "${LIB}"

# The office looks for this symbol when it loads the component (src/component.cxx).
if command -v nm >/dev/null 2>&1; then
    SYMBOLS="$(nm -D --defined-only "${LIB}")"
    if grep -q 'component_getFactory' <<<"${SYMBOLS}"; then
        echo "ok: component_getFactory is exported"
    else
        die "component_getFactory is not exported by ${LIB} (check SAL_DLLPUBLIC_EXPORT and visibility flags)"
    fi
else
    echo "note: nm not found, export check skipped"
fi
