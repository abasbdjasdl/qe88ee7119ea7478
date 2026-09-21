#!/bin/bash
# Integration test on an isolated macOS CI runner and a disposable image file.
set -euo pipefail
cd "$(dirname "$0")/.."
test ! -e /Volumes/MACRECOVERY
test ! -e /private/tmp/r16-autolog-v2---test-success
hdiutil create -size 2g -layout NONE -fs MS-DOS -volname MACRECOVERY build/recovery/test-log-volume.dmg
hdiutil attach build/recovery/test-log-volume.dmg -nobrowse -plist > build/recovery/test-log-attach.plist
device=$(python3 -c 'import plistlib; p=plistlib.load(open("build/recovery/test-log-attach.plist","rb")); print(next(x["dev-entry"] for x in p["system-entities"] if "mount-point" in x))')
mkdir -p /Volumes/MACRECOVERY/r16-autolog
printf '%s\n' R16-FILE-CAPTURE-20260921-01 > /Volumes/MACRECOVERY/r16-autolog/capture-target.txt
sudo diskutil unmount "$device"
sudo mkdir -p /usr/local/libexec
sudo install -o root -g wheel -m 755 recovery-autolog/r16-autolog.sh /usr/local/libexec/r16-autolog.sh
sudo install -o root -g wheel -m 644 recovery-autolog/local.r16.autolog.plist /Library/LaunchDaemons/local.r16.autolog.plist
sudo /usr/libexec/PlistBuddy -c 'Set :ProgramArguments:1 /usr/local/libexec/r16-autolog.sh' /Library/LaunchDaemons/local.r16.autolog.plist
sudo /usr/libexec/PlistBuddy -c 'Add :ProgramArguments:2 string --test-success' /Library/LaunchDaemons/local.r16.autolog.plist
sudo launchctl bootstrap system /Library/LaunchDaemons/local.r16.autolog.plist
done_ok=false
for i in {1..36}; do
  if sudo test -f /private/tmp/r16-autolog-v2---test-success/reboot-requested-test.txt; then done_ok=true; break; fi
  sleep 5
done
sudo launchctl bootout system/local.r16.autolog || true
if [ "$done_ok" != true ]; then
  sudo cat /private/tmp/r16-autolog-v2---test-success/collector.log || true
  sudo cat /private/tmp/r16-autolog-v2---test-success/volume-info.plist || true
  exit 1
fi
sudo grep -q '^COMPLETE$' /private/tmp/r16-autolog-v2---test-success/reboot-requested-test.txt
sudo /bin/sh -c 'grep -q "^COMPLETE " /Volumes/MACRECOVERY/r16-autolog/logs-*/status.txt'
sudo /bin/sh -c 'test -s /Volumes/MACRECOVERY/r16-autolog/logs-*/disk-list-final.txt'
sudo /bin/sh -c 'test -s /Volumes/MACRECOVERY/r16-autolog/logs-*/loaded-kexts.txt'
hdiutil detach "$device"
sudo /bin/sh /usr/local/libexec/r16-autolog.sh --test-error || test "$?" = 7
sudo grep -q '^ERROR$' /private/tmp/r16-autolog-v2---test-error/reboot-requested-test.txt
sudo /bin/sh /usr/local/libexec/r16-autolog.sh --test-timeout || test "$?" = 143
sudo grep -q '^TIMEOUT$' /private/tmp/r16-autolog-v2---test-timeout/reboot-requested-test.txt
echo 'PASS: launchd collection, automatic FAT mount, saved logs; success/error/timeout each requested reboot (reboot mocked in CI).' > build/recovery/collector-integration-test.txt
