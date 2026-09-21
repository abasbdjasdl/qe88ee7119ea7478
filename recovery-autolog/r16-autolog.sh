#!/bin/sh
# One-shot recovery diagnostics. No NVRAM writes, repair, erase or partitioning.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/libexec
export PATH
umask 077
token=R16-FILE-CAPTURE-20260921-01
ram=/private/tmp/r16-autolog
[ -d "$ram" ] && exit 0
mkdir "$ram" 2>/dev/null || exit 75
exec > "$ram/collector.log" 2>&1
echo "$token"
date

# Bound commands so a hung storage query cannot block every other diagnostic.
run() {
    seconds=$1; file=$2; shift 2
    "$@" > "$ram/$file" 2>&1 &
    job=$!
    ( sleep "$seconds"; kill -TERM "$job" 2>/dev/null ) &
    watch=$!
    wait "$job"
    code=$?
    kill "$watch" 2>/dev/null
    wait "$watch" 2>/dev/null
    echo "$file exit=$code"
    return 0
}

target=
out=
find_target() {
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

# Only mount the small, known-size FAT recovery/EFI volumes. A unique marker
# placed by the Windows installer is additionally required before writing.
attempt=0
while [ "$attempt" -lt 6 ]; do
    find_target && break
    attempt=$((attempt + 1))
    run 12 disk-list.txt diskutil list
    awk '/MACRECOVERY|EFI/ {print $NF}' "$ram/disk-list.txt" | grep -E '^disk[0-9]+(s[0-9]+)?$' > "$ram/candidates.txt"
    while read dev; do
        run 8 volume-info.plist diskutil info -plist "$dev"
        size=$(PlistBuddy -c 'Print :TotalSize' "$ram/volume-info.plist" 2>/dev/null)
        [ -n "$size" ] || size=$(PlistBuddy -c 'Print :DiskSize' "$ram/volume-info.plist" 2>/dev/null)
        name=$(PlistBuddy -c 'Print :VolumeName' "$ram/volume-info.plist" 2>/dev/null)
        content=$(PlistBuddy -c 'Print :Content' "$ram/volume-info.plist" 2>/dev/null)
        if [ "$name:$size" != MACRECOVERY:2147483648 ] && [ "$content:$size" != EFI:272629760 ]; then continue; fi
        run 10 "mount-$dev.txt" diskutil mount "$dev"
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
    cp "$ram"/* "$out/" 2>/dev/null
    echo "COMPLETE $token" > "$out/status.txt"
    sync
    echo "Saved to $out"
else
    echo 'NO_WRITABLE_LOG_TARGET; logs remain in RAM.'
fi
exit 0
