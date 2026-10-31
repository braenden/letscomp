#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
project_dir="${script_dir:h}"
preset="${1:-mac-arm64-release}"
artefacts="$project_dir/build/$preset/LetsComp_artefacts/Release"

typeset -A sources destinations
sources[AU]="$artefacts/AU/LetsComp.component"
sources[CLAP]="$artefacts/CLAP/LetsComp.clap"
sources[VST3]="$artefacts/VST3/LetsComp.vst3"
destinations[AU]="$HOME/Library/Audio/Plug-Ins/Components"
destinations[CLAP]="$HOME/Library/Audio/Plug-Ins/CLAP"
destinations[VST3]="$HOME/Library/Audio/Plug-Ins/VST3"

for format in AU CLAP VST3; do
    source_bundle="${sources[$format]}"
    destination_dir="${destinations[$format]}"
    [[ -d "$source_bundle" ]] || { print -u2 "Missing $source_bundle; run scripts/build_mac.sh first."; exit 1; }
    mkdir -p "$destination_dir"
    ditto "$source_bundle" "$destination_dir/${source_bundle:t}"
    xattr -cr "$destination_dir/${source_bundle:t}"
    codesign --force --deep --sign - "$destination_dir/${source_bundle:t}"
    print "Installed $format to $destination_dir/${source_bundle:t}"
done

killall -9 AudioComponentRegistrar 2>/dev/null || true
print "Installation complete. Rescan plug-ins in FL Studio's Plugin Manager."
