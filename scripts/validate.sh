#!/bin/zsh
set -u

failures=0
au="$HOME/Library/Audio/Plug-Ins/Components/LetsComp.component"
clap="$HOME/Library/Audio/Plug-Ins/CLAP/LetsComp.clap"
vst3="$HOME/Library/Audio/Plug-Ins/VST3/LetsComp.vst3"

if [[ -d "$au" ]] && command -v auval >/dev/null 2>&1; then
    print "Validating AU..."
    auval -v aufx LtCp Brnd || failures=$((failures + 1))
else
    print "AU validation skipped: install LetsComp.component and ensure auval is available."
fi

if [[ -d "$clap" ]] && command -v clap-validator >/dev/null 2>&1; then
    print "Validating CLAP..."
    clap-validator validate "$clap" || failures=$((failures + 1))
else
    print "CLAP validation skipped: clap-validator is optional and was not found, or the plug-in is not installed."
fi

pluginval_command=""
if command -v pluginval >/dev/null 2>&1; then
    pluginval_command="$(command -v pluginval)"
elif [[ -x "/Applications/pluginval.app/Contents/MacOS/pluginval" ]]; then
    pluginval_command="/Applications/pluginval.app/Contents/MacOS/pluginval"
fi

if [[ -d "$vst3" ]] && [[ -n "$pluginval_command" ]]; then
    print "Validating VST3..."
    "$pluginval_command" --strictness-level 5 --validate-in-process --skip-gui-tests "$vst3" || failures=$((failures + 1))
elif [[ -d "$vst3" ]] && command -v validator >/dev/null 2>&1; then
    print "Validating VST3..."
    validator "$vst3" || failures=$((failures + 1))
else
    print "VST3 validation skipped: pluginval/VST3 validator is optional and was not found, or the plug-in is not installed."
fi

exit "$failures"
