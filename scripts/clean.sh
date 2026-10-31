#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
project_dir="${script_dir:h}"
build_dir="$project_dir/build"
if [[ -d "$build_dir" ]]; then
    rm -rf "$build_dir"
    print "Removed $build_dir"
else
    print "Nothing to clean."
fi

