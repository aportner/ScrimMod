#!/usr/bin/env bash

set -euo pipefail

scrimmod_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
scrimmod_build_dir="${scrimmod_root}/build/linux-release"
scrimmod_default_cstrike_dir="${HOME:?HOME must be set}/hlds/cstrike"
scrimmod_cstrike_dir="${1:-${SCRIMMOD_CSTRIKE_DIR:-${scrimmod_default_cstrike_dir}}}"
scrimmod_install_dir="${scrimmod_cstrike_dir}/addons/ScrimMod"
scrimmod_artifact="${scrimmod_build_dir}/scrimmod_mm_i386.so"

if [[ ! -d "${scrimmod_cstrike_dir}" ]]; then
    echo "Counter-Strike directory not found: ${scrimmod_cstrike_dir}" >&2
    echo "Pass its path as the first argument or set SCRIMMOD_CSTRIKE_DIR." >&2
    exit 1
fi

if [[ ! -f "${scrimmod_artifact}" ]]; then
    echo "Plugin has not been built: ${scrimmod_artifact}" >&2
    echo "Run ./build-linux.sh first." >&2
    exit 1
fi

cmake --install "${scrimmod_build_dir}" \
    --prefix "${scrimmod_install_dir}" \
    --component plugin

if [[ ! -f "${scrimmod_install_dir}/scrimmod_mm_i386.so" ]]; then
    echo "CMake completed without installing the plugin." >&2
    exit 1
fi

echo
echo "Installed ScrimMod to:"
echo "  ${scrimmod_install_dir}/scrimmod_mm_i386.so"
echo
echo "Ensure this line is present in ${scrimmod_cstrike_dir}/addons/metamod/plugins.ini:"
echo "  linux addons/ScrimMod/scrimmod_mm_i386.so"
