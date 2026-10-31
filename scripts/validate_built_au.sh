#!/bin/zsh
set -euo pipefail

project_dir="${0:A:h:h}"
built_component="$project_dir/build/mac-arm64-release/LetsComp_artefacts/Release/AU/LetsComp.component"
temporary_component="$HOME/Library/Audio/Plug-Ins/Components/LetsComp.component"

[[ -d "$built_component" ]] || { print -u2 "Missing built AU: $built_component"; exit 1; }
[[ ! -e "$temporary_component" ]] || {
    print -u2 "Refusing to overwrite an existing user-local LetsComp.component"
    exit 1
}

cleanup() {
    rm -rf "$temporary_component"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mkdir -p "${temporary_component:h}"
ditto "$built_component" "$temporary_component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1
auval -v aufx LtCp Brnd -strict
