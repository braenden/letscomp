#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
project_dir="${script_dir:h}"
preset="mac-arm64-release"

if [[ "${1:-}" == "--universal" ]]; then
    preset="mac-universal-release"
fi

if ! command -v cmake >/dev/null 2>&1; then
    print -u2 "CMake is required. Install it with: brew install cmake ninja"
    exit 1
fi

cmake --preset "$preset" -S "$project_dir"
cmake --build --preset "$preset" --target LetsComp_Standalone

app="$project_dir/build/$preset/LetsComp_artefacts/Release/Standalone/LetsComp.app"
if [[ ! -d "$app" ]]; then
    print -u2 "Standalone app was not created: $app"
    exit 1
fi

print "Opening LetsComp preview: $app"
# Ensure macOS launches the newly linked executable instead of merely focusing an older preview.
pkill -x LetsComp 2>/dev/null || true
open "$app"
