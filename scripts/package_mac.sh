#!/bin/zsh
set -euo pipefail
export COPYFILE_DISABLE=1

# Builds a universal (Apple silicon and Intel) release and packages the AU,
# VST3 and CLAP as a zip and as an installer in build/packages.
#
#   PLUGIN_SIGN_IDENTITY     "Developer ID Application: ..." signs the plug-ins.
#                            Unset, they are signed ad hoc for local testing only.
#   INSTALLER_SIGN_IDENTITY  "Developer ID Installer: ..." signs the installer.
#   NOTARY_PROFILE           notarytool keychain profile. Set, the plug-ins (and
#                            a signed installer) are notarized and stapled.

script_dir="${0:A:h}"
project_dir="${script_dir:h}"
preset="mac-universal-release"

version="$(sed -nE 's/^project\(LetsComp VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$project_dir/CMakeLists.txt")"
[[ -n "$version" ]] || { print -u2 "Could not read LetsComp version from CMakeLists.txt"; exit 1; }
package_root="$project_dir/build/packages"
stage_root="$package_root/stage"
plugins_root="$stage_root/Library/Audio/Plug-Ins"
zip_root="$package_root/LetsComp-$version"
zip_file="$package_root/LetsComp-$version-macOS.zip"
notary_zip="$package_root/notarize.zip"
final_package="$package_root/LetsComp-$version-macOS.pkg"
artefacts="$project_dir/build/$preset/LetsComp_artefacts/Release"

plugin_identity="${PLUGIN_SIGN_IDENTITY:--}"
installer_identity="${INSTALLER_SIGN_IDENTITY:-}"
notary_profile="${NOTARY_PROFILE:-}"
if [[ -n "$notary_profile" && "$plugin_identity" == "-" ]]; then
    print -u2 "NOTARY_PROFILE needs PLUGIN_SIGN_IDENTITY set to a Developer ID Application identity."
    exit 1
fi

notarize() {
    local file="$1"
    print "Notarizing ${file:t}, which usually takes a few minutes..."
    local result submission_status submission_id
    result="$(xcrun notarytool submit "$file" --keychain-profile "$notary_profile" --wait --output-format json)"
    submission_status="$(print -r -- "$result" | plutil -extract status raw -o - -)"
    if [[ "$submission_status" != "Accepted" ]]; then
        submission_id="$(print -r -- "$result" | plutil -extract id raw -o - - 2>/dev/null || true)"
        print -u2 "Notarization of ${file:t} ended as: $submission_status"
        [[ -z "$submission_id" ]] || print -u2 "See why with: xcrun notarytool log $submission_id --keychain-profile $notary_profile"
        exit 1
    fi
}

"$script_dir/build_mac.sh" --universal

rm -rf "$package_root"
mkdir -p "$plugins_root/Components" "$plugins_root/VST3" "$plugins_root/CLAP" "$zip_root"
bundles=(
    "$plugins_root/Components/LetsComp.component"
    "$plugins_root/VST3/LetsComp.vst3"
    "$plugins_root/CLAP/LetsComp.clap"
)
/usr/bin/ditto --norsrc --noextattr --noqtn "$artefacts/AU/LetsComp.component" "${bundles[1]}"
/usr/bin/ditto --norsrc --noextattr --noqtn "$artefacts/VST3/LetsComp.vst3" "${bundles[2]}"
/usr/bin/ditto --norsrc --noextattr --noqtn "$artefacts/CLAP/LetsComp.clap" "${bundles[3]}"

for bundle in "${bundles[@]}"; do
    architectures=" $(lipo -archs "$bundle/Contents/MacOS/LetsComp") "
    [[ "$architectures" == *" arm64 "* && "$architectures" == *" x86_64 "* ]] \
        || { print -u2 "${bundle:t} is not universal:$architectures"; exit 1; }
    xattr -cr "$bundle"
    if [[ "$plugin_identity" == "-" ]]; then
        codesign --force --options runtime --timestamp=none --sign - "$bundle"
    else
        codesign --force --options runtime --timestamp --sign "$plugin_identity" "$bundle"
    fi
    codesign --verify --strict "$bundle"
done

# codesign may recreate AppleDouble sidecars on this volume; remove them after signing.
xattr -cr "$stage_root"
find "$stage_root" -type f -name '._*' -delete

if [[ -n "$notary_profile" ]]; then
    /usr/bin/ditto -c -k --keepParent "$plugins_root" "$notary_zip"
    notarize "$notary_zip"
    rm -f "$notary_zip"
    for bundle in "${bundles[@]}"; do
        xcrun stapler staple -q "$bundle"
        xcrun stapler validate -q "$bundle"
    done
fi

for bundle in "${bundles[@]}"; do
    /usr/bin/ditto "$bundle" "$zip_root/${bundle:t}"
done
# No extended attributes, so other unzip tools see no ._ files.
/usr/bin/ditto -c -k --norsrc --noextattr --noacl --keepParent "$zip_root" "$zip_file"
rm -rf "$zip_root"

# One package per format, so the installer can offer a choice. Bundles are
# not relocatable: an older copy elsewhere on the disk must not be updated
# instead of the one in the plug-in folder.
components_root="$package_root/components"
mkdir -p "$components_root"
formats=(au vst3 clap)
for index in 1 2 3; do
    format="${formats[$index]}"
    bundle="${bundles[$index]}"
    root="$package_root/root-$format"
    relative="${bundle#$stage_root/}"
    mkdir -p "$root/${relative:h}"
    /usr/bin/ditto "$bundle" "$root/$relative"
    component_list="$package_root/$format.plist"
    pkgbuild --analyze --root "$root" "$component_list"
    entry=0
    while plutil -extract "$entry" xml1 -o /dev/null "$component_list" 2>/dev/null; do
        plutil -replace "$entry.BundleIsRelocatable" -bool NO "$component_list"
        (( ++entry ))
    done
    script_arguments=()
    [[ "$format" == au ]] && script_arguments=(--scripts "$project_dir/packaging/scripts")
    pkgbuild \
        --root "$root" \
        --component-plist "$component_list" \
        "${script_arguments[@]}" \
        --identifier "io.github.braenden.letscomp.$format" \
        --version "$version" \
        --install-location / \
        "$components_root/LetsComp-$format.pkg"
    rm -rf "$root" "$component_list"
done

distribution="$package_root/distribution.xml"
sed "s/@VERSION@/$version/g" "$project_dir/packaging/distribution.xml" > "$distribution"
productbuild_arguments=(--distribution "$distribution" --package-path "$components_root"
                        --resources "$project_dir/packaging/resources")
[[ -z "$installer_identity" ]] || productbuild_arguments+=(--sign "$installer_identity")
productbuild "${productbuild_arguments[@]}" "$final_package"
rm -rf "$components_root" "$distribution"

if [[ -n "$notary_profile" && -n "$installer_identity" ]]; then
    notarize "$final_package"
    xcrun stapler staple -q "$final_package"
    spctl --assess --type install "$final_package"
fi
rm -rf "$stage_root"

print "\nCreated:"
print "  $zip_file"
print "  $final_package"
if [[ -z "$notary_profile" ]]; then
    print "\nNot notarized: macOS will block these on other Macs. Set NOTARY_PROFILE for a release."
elif [[ -z "$installer_identity" ]]; then
    print "\nThe zip is notarized. The installer is unsigned, so share the zip, or set INSTALLER_SIGN_IDENTITY."
else
    print "\nBoth are signed, notarized and stapled."
fi
