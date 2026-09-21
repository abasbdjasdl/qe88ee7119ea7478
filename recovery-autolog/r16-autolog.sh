#!/bin/sh
# One-shot recovery diagnostics. No NVRAM writes, repair, erase or partitioning.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/libexec
export PATH
umask 077
token=R16-FILE-CAPTURE-20260921-01
mode=${1:-production}
test_uuid=${2:-}
case "$mode" in production|--test-success|--test-error|--test-timeout) ;; *) exit 64 ;; esac
ram=/private/tmp/r16-autolog-v2
[ "$mode" = production ] || ram="$ram-$mode"
[ -d "$ram" ] && exit 0
mkdir "$ram" 2>/dev/null || exit 75
exec > "$ram/collector.log" 2>&1
echo "$token"
date
out=
owned=
outcome=ERROR
parent=$$
deadline=360
[ "$mode" != --test-timeout ] || deadline=2

finish() {
    code=$1
    trap - EXIT HUP INT TERM
    [ ! -f "$ram/timed-out" ] || outcome=TIMEOUT
    [ "$code" -eq 0 ] || { [ "$outcome" = TIMEOUT ] || outcome=ERROR; }
    echo "$outcome exit=$code" > "$ram/reboot-reason.txt"
    if [ -n "$out" ]; then
        cp "$ram"/* "$out/" 2>/dev/null || outcome=ERROR_SAVING_LOGS
        echo "$outcome $token" > "$out/status.txt"
        echo 'Automatic restart requested; Windows remains the firmware default.' > "$out/reboot.txt"
    fi
    echo "R16 diagnostics: $outcome; restarting." > /dev/console 2>/dev/null
    sync
    if [ -n "$owned" ]; then
        /sbin/umount "$owned" > "$ram/unmount.txt" 2>&1
        sync
    fi
    if [ "$mode" = production ]; then
        sleep 10
        /sbin/reboot
        # Keep the independent deadline alive if the normal reboot fails.
        echo 'Normal reboot returned; deadline fallback remains active.'
    else
        echo "$outcome" > "$ram/reboot-requested-test.txt"
        kill "$deadline_pid" 2>/dev/null
    fi
}
trap 'finish "$?"' EXIT
trap 'exit 143' HUP INT TERM
(
    sleep "$deadline"
    echo TIMEOUT > "$ram/timed-out"
    kill -TERM "$parent" 2>/dev/null
    # If a blocked command prevents the shell trap, still try to reboot.
    sleep 15
    sync
    [ ! -f "$ram/owned-point" ] || /sbin/umount "$(cat "$ram/owned-point")"
    [ "$mode" != production ] || /sbin/reboot
) &
deadline_pid=$!

[ "$mode" != --test-error ] || exit 7
if [ "$mode" = --test-timeout ]; then sleep 5; exit 124; fi

# Bound commands so a hung storage query cannot block every other diagnostic.
run() {
    seconds=$1; file=$2; shift 2
    "$@" > "$ram/$file" 2>&1 &
    job=$!
    ( sleep "$seconds"; kill -TERM "$job" 2>/dev/null; sleep 2; kill -KILL "$job" 2>/dev/null ) &
    watch=$!
    wait "$job"
    code=$?
    kill "$watch" 2>/dev/null
    wait "$watch" 2>/dev/null
    echo "$file exit=$code"
    return 0
}

target=
find_target() {
    [ -z "$out" ] || return 0
    # Refuse ambiguous duplicate FAT mounts, rather than allocating through
    # independent caches for the same device.
    duplicates=$(mount | grep '(msdos' | awk '{print $1}' | sort | uniq -d)
    [ -z "$duplicates" ] || return 1
    for marker in /Volumes/*/r16-autolog/capture-target.txt /Volumes/*/EFI/OC/r16-autolog/capture-target.txt; do
        [ -f "$marker" ] || continue
        [ "$(cat "$marker")" = "$token" ] || continue
        candidate=${marker%/capture-target.txt}
        candidate_out="$candidate/logs-$(date +%Y%m%d-%H%M%S)-$$"
        if mkdir "$candidate_out" 2>/dev/null; then target=$candidate; out=$candidate_out; return 0; fi
    done
    return 1
}

run 15 disk-list-initial.txt diskutil list
run 10 system-version.txt sw_vers
run 10 kernel.txt uname -a
run 10 probe-initial.txt ioreg -r -c RTL8852BEProbe -l -w 0

# Match GPT partition UUIDs, then require the private marker before writing.
attempt=0
while [ "$attempt" -lt 6 ]; do
    find_target && break
    attempt=$((attempt + 1))
    run 12 disk-list.txt diskutil list
    printf '%s\n' /dev/disk*s* | sed 's|/dev/||' | grep -E '^disk[0-9]+s[0-9]+$' > "$ram/candidates.txt"
    while read dev; do
        run 8 "$dev.plist" diskutil info -plist "$dev"
        uuid=$(PlistBuddy -c 'Print :DiskUUID' "$ram/$dev.plist" 2>/dev/null | tr '[:lower:]' '[:upper:]')
        case "$uuid" in
            D228C57E-717B-4433-945D-BE8BE9852C75|56966F71-F263-4C6F-BCA7-912ED89ECDCA) ;;
            *) [ "$mode" = --test-success ] && [ -n "$test_uuid" ] && [ "$uuid" = "$test_uuid" ] || continue ;;
        esac
        # diskutil can leave a daemon-side mount pending after its client times
        # out. Never combine it with a second direct mount of the same device.
        if mount | grep -q "^/dev/$dev on "; then find_target && break; continue; fi
        point=/Volumes/R16Capture-$dev
        mkdir -p "$point" || continue
        run 10 "fat-$dev.txt" /sbin/mount -t msdos "/dev/$dev" "$point"
        if mount | grep -q "^/dev/$dev on $point "; then owned=$point; echo "$point" > "$ram/owned-point"; fi
        find_target && break
    done < "$ram/candidates.txt"
    find_target && break
    sleep 5
done

if [ -n "$out" ]; then
    echo "STARTED $token" > "$out/status.txt"
    cp "$ram"/* "$out/" 2>/dev/null
    sync
fi

run 15 probe-ioreg.txt ioreg -r -c RTL8852BEProbe -l -w 0
run 15 pci-ioreg.txt ioreg -r -c IOPCIDevice -l -w 0
run 15 media-ioreg.txt ioreg -r -c IOMedia -l -w 0
run 15 nvme-ioreg.txt ioreg -r -c IONVMeController -l -w 0
run 10 keyboard-ioreg.txt ioreg -r -c ApplePS2Keyboard -l -w 0
run 20 loaded-kexts.txt kextstat -l
run 15 disk-list-final.txt diskutil list
run 10 mounts.txt mount
run 10 launchd-self.txt launchctl print system/local.r16.autolog
run 15 kernel-messages.txt dmesg
if command -v log >/dev/null 2>&1; then
    run 20 relevant-log.txt log show --last 5m --style compact --predicate 'eventMessage CONTAINS "RTL8852BE" OR eventMessage CONTAINS "NVMe" OR eventMessage CONTAINS "msdos"'
fi
if grep -q '"DiagnosticOnly" = Yes' "$ram/probe-ioreg.txt"; then
    echo 'PROBE_MATCHED; Wi-Fi remains non-operational.' > "$ram/result.txt"
else
    echo 'PROBE_NOT_CONFIRMED; inspect kext and PCI records.' > "$ram/result.txt"
fi
if [ -n "$out" ]; then
    outcome=COMPLETE
else
    outcome=NO_WRITABLE_LOG_TARGET
    echo 'NO_WRITABLE_LOG_TARGET; logs remain in RAM.'
fi
exit 0
