#!/bin/bash
# Integration test on an isolated macOS CI runner and a disposable image file.
set -euo pipefail
cd "$(dirname "$0")/.."
test ! -e /Volumes/MACRECOVERY
test ! -e /private/tmp/r16-autolog
hdiutil create -size 4194304s -layout NONE -fs MS-DOS -volname MACRECOVERY build/recovery/test-log-volume.dmg
hdiutil attach build/recovery/test-log-volume.dmg -nobrowse -plist > build/recovery/test-log-attach.plist
device=$(python3 -c 'import plistlib; p=plistlib.load(open("build/recovery/test-log-attach.plist","rb")); print(next(x["dev-entry"] for x in p["system-entities"] if "mount-point" in x))')
mkdir -p /Volumes/MACRECOVERY/r16-autolog
printf '%s\n' R16-FILE-CAPTURE-20260921-01 > /Volumes/MACRECOVERY/r16-autolog/capture-target.txt
sudo diskutil unmount "$device"
sudo mkdir -p /usr/local/libexec
sudo install -o root -g wheel -m 755 recovery-autolog/r16-autolog.sh /usr/local/libexec/r16-autolog.sh
sudo install -o root -g wheel -m 644 recovery-autolog/local.r16.autolog.plist /Library/LaunchDaemons/local.r16.autolog.plist
sudo launchctl bootstrap system /Library/LaunchDaemons/local.r16.autolog.plist
done_ok=false
for i in {1..36}; do
  if sudo /bin/sh -c 'grep -q "^COMPLETE " /Volumes/MACRECOVERY/r16-autolog/logs-*/status.txt 2>/dev/null'; then done_ok=true; break; fi
  sleep 5
done
sudo launchctl bootout system/local.r16.autolog || true
if [ "$done_ok" != true ]; then
  sudo cat /private/tmp/r16-autolog/collector.log || true
  sudo cat /private/tmp/r16-autolog/volume-info.plist || true
  exit 1
fi
sudo /bin/sh -c 'test -s /Volumes/MACRECOVERY/r16-autolog/logs-*/disk-list-final.txt'
sudo /bin/sh -c 'test -s /Volumes/MACRECOVERY/r16-autolog/logs-*/loaded-kexts.txt'
hdiutil detach "$device"
echo 'PASS: launchd started collector, target image auto-mounted, complete diagnostics persisted.' > build/recovery/collector-integration-test.txt
