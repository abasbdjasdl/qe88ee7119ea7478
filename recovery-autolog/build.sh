#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/recovery build/recovery-mount build/recovery-verify
base="$PWD/build/recovery/BaseSystem.dmg"
mount="$PWD/build/recovery-mount"
verify="$PWD/build/recovery-verify"
curl --fail --location --retry 3 --output "$base" 'http://oscdn.apple.com/content/downloads/04/11/082-33203/orvwro1v8xhjrakr7tvl5hu1s1ew3epxne/RecoveryImage/BaseSystem.dmg'
echo "7314eb401f5e84087f621b3599f0ad21ca3cdcc2685ea2da7f76806792328e20  $base" | shasum -a 256 -c -
hdiutil verify "$base"
sudo hdiutil attach "$base" -shadow "$PWD/build/recovery/edit.shadow" -nobrowse -owners on -mountpoint "$mount" -plist > build/recovery/attach.plist
device=$(python3 -c 'import plistlib; p=plistlib.load(open("build/recovery/attach.plist","rb")); print(next(x["dev-entry"] for x in p["system-entities"] if "mount-point" in x))')
echo "Mounted image volume: $device"
sudo mkdir -p "$mount/usr/local/libexec"
sudo install -o root -g wheel -m 755 recovery-autolog/r16-autolog.sh "$mount/usr/local/libexec/r16-autolog.sh"
sudo install -o root -g wheel -m 644 recovery-autolog/local.r16.autolog.plist "$mount/System/Library/LaunchDaemons/local.r16.autolog.plist"
plutil -lint "$mount/System/Library/LaunchDaemons/local.r16.autolog.plist"
/bin/sh -n "$mount/usr/local/libexec/r16-autolog.sh"
test "$(/usr/libexec/PlistBuddy -c 'Print :ProductVersion' "$mount/System/Library/CoreServices/SystemVersion.plist")" = 15.4.1
sync
sudo diskutil unmount "$device"
raw="${device/\/dev\/disk/\/dev\/rdisk}"
sudo dd if="$raw" of=build/recovery/modified.hfs bs=1m
sudo chown "$(id -u):$(id -g)" build/recovery/modified.hfs
hdiutil detach "$device"
python3 recovery-autolog/dmg_patch.py make "$base" build/recovery/modified.hfs build/recovery/recovery-autolog.patch.zip
python3 recovery-autolog/dmg_patch.py apply "$base" build/recovery/recovery-autolog.patch.zip build/recovery/Diagnostic.dmg
hdiutil verify build/recovery/Diagnostic.dmg
hdiutil attach build/recovery/Diagnostic.dmg -readonly -nobrowse -mountpoint "$verify"
cmp recovery-autolog/r16-autolog.sh "$verify/usr/local/libexec/r16-autolog.sh"
cmp recovery-autolog/local.r16.autolog.plist "$verify/System/Library/LaunchDaemons/local.r16.autolog.plist"
hdiutil detach "$verify"
shasum -a 256 build/recovery/recovery-autolog.patch.zip > build/recovery/PATCH-SHA256SUMS.txt
git rev-parse HEAD > build/recovery/source-commit.txt
echo 'Image verified and custom files re-mounted/read successfully; hardware boot pending.' > build/recovery/verification.txt
