#!/usr/bin/env bash
# Removes the pluginhost extension for the current user with unopkg (no root, no --shared).
#
# NOT RUN YET. Written without a Linux host.
#
# Usage:
#   scripts/uninstall.sh [-p PROFILE_DIR]
#
#   -p PROFILE_DIR   the private profile the extension was installed into (same value as for install.sh).
#                    Without it the real LibreOffice profile is used.
#
# Settings: LO_PROGRAM (default /usr/lib/libreoffice/program) holds unopkg.
# The extension identifier is org.example.pluginhost (extension/description.xml).
set -euo pipefail

die() { echo "ERROR: $*" >&2; exit 1; }
run() { printf '+'; printf ' %q' "$@"; printf '\n'; "$@"; }

PROFILE_DIR="${PH_PROFILE_DIR:-}"
while getopts "p:h" opt; do
    case "${opt}" in
        p) PROFILE_DIR="${OPTARG}" ;;
        h) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) die "usage: uninstall.sh [-p PROFILE_DIR]" ;;
    esac
done
shift $((OPTIND - 1))

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
    [ -d "${PROFILE_DIR}" ] || die "profile folder not found: ${PROFILE_DIR}"
    PROFILE_ABS="$(cd "${PROFILE_DIR}" && pwd)"
    ENV_ARGS=("-env:UserInstallation=file://${PROFILE_ABS}")
    echo "profile: ${PROFILE_ABS}"
else
    echo "WARNING: no -p PROFILE_DIR given, removing from your real LibreOffice profile."
fi

run "${UNOPKG}" remove ${ENV_ARGS[@]+"${ENV_ARGS[@]}"} org.example.pluginhost
echo "Removed org.example.pluginhost."
