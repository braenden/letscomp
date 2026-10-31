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
cmake --build --preset "$preset" --target \
    LetsComp_AU LetsComp_VST3 LetsComp_CLAP LetsCompTests LetsCompAssistantTests \
    LetsCompAssist LetsCompProcessorTests
ctest --test-dir "$project_dir/build/$preset" --output-on-failure

artefacts="$project_dir/build/$preset/LetsComp_artefacts/Release"
for bundle in "$artefacts/AU/LetsComp.component" "$artefacts/CLAP/LetsComp.clap" "$artefacts/VST3/LetsComp.vst3"; do
    codesign --force --deep --sign - "$bundle"
done
print "Build complete:"
print "  AU:   $artefacts/AU/LetsComp.component"
print "  CLAP: $artefacts/CLAP/LetsComp.clap"
print "  VST3: $artefacts/VST3/LetsComp.vst3"
