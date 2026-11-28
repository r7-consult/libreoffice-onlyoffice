#!/usr/bin/env bash
# Installs the .oxt for the current user with unopkg (no root, no --shared).
#
# NOT RUN YET. Written without a Linux host.
#
# Usage:
#   scripts/install.sh [-p PROFILE_DIR] [OXT_FILE]
#
#   -p PROFILE_DIR   private LibreOffice profile folder (passed as -env:UserInstallation=file://PROFILE_DIR).
#                    Without it the extension goes into your real LibreOffice profile (~/.config/libreoffice).
#                    PROFILE_DIR must use only letters, digits and . _ - / (it becomes a file: URL).
#   OXT_FILE         default: dist/pluginhost-linux_x86_64.oxt (linux_aarch64 on ARM)
#
# Settings: LO_PROGRAM (default /usr/lib/libreoffice/program) holds unopkg.
# Close every LibreOffice window that uses the same profile first: unopkg and soffice lock the profile.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

die() { echo "ERROR: $*" >&2; exit 1; }
run() { printf '+'; printf ' %q' "$@"; printf '\n'; "$@"; }

PROFILE_DIR="${PH_PROFILE_DIR:-}"
while getopts "p:h" opt; do
    case "${opt}" in
        p) PROFILE_DIR="${OPTARG}" ;;
        h) sed -n '2,16p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) die "usage: install.sh [-p PROFILE_DIR] [OXT_FILE]" ;;
    esac
done
shift $((OPTIND - 1))

case "$(uname -m)" in
    x86_64)  PLATFORM="linux_x86_64" ;;
    aarch64) PLATFORM="linux_aarch64" ;;
    *) PLATFORM="linux_$(uname -m)" ;;
esac
OXT="${1:-${ROOT}/dist/pluginhost-${PLATFORM}.oxt}"
[ -f "${OXT}" ] || die "package not found: ${OXT}. Run scripts/pack.sh first."

LO_PROGRAM="${LO_PROGRAM:-/usr/lib/libreoffice/program}"
UNOPKG="${LO_PROGRAM}/unopkg"
if [ ! -x "${UNOPKG}" ]; then
    if command -v unopkg >/dev/null 2>&1; then
        UNOPKG="$(command -v unopkg)"
        echo "note: ${LO_PROGRAM}/unopkg is missing, using ${UNOPKG} from PATH"
    else
        die "unopkg not found in ${LO_PROGRAM} or on PATH. Install libreoffice-core or set LO_PROGRAM."
    fi
fi

ENV_ARGS=()
if [ -n "${PROFILE_DIR}" ]; then
    case "${PROFILE_DIR}" in
        *[!A-Za-z0-9._/-]*) die "profile path may contain only letters, digits and . _ - /  (got: ${PROFILE_DIR})" ;;
    esac
    run mkdir -p "${PROFILE_DIR}"
    PROFILE_ABS="$(cd "${PROFILE_DIR}" && pwd)"
    ENV_ARGS=("-env:UserInstallation=file://${PROFILE_ABS}")
    echo "profile: ${PROFILE_ABS}"
else
    echo "WARNING: no -p PROFILE_DIR given, installing into your real LibreOffice profile."
fi

# --force replaces an installed version of the same extension. No --shared: that would need root.
run "${UNOPKG}" add --force ${ENV_ARGS[@]+"${ENV_ARGS[@]}"} "${OXT}"
run "${UNOPKG}" list ${ENV_ARGS[@]+"${ENV_ARGS[@]}"}
echo "Installed. Look for the identifier org.example.pluginhost in the list above."
