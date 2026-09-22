#!/bin/sh
# One-shot credential-safe network experiment; existing marker/UUID authority.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/libexec
export PATH
umask 077
token=R16-FILE-CAPTURE-20260921-01
mode=${1:-production}
tu=${2:-}
case "$mode" in production|--test-success|--test-error|--test-timeout) ;; *) exit 64 ;; esac
mark() (
 [ "$mode" = production ] || exit 0
 /usr/sbin/nvram -s "r16-$1-v1=R16S1-20260922B-$2" >/dev/null 2>&1 & n=$!
 (sleep 2;kill -TERM "$n" 2>/dev/null;sleep 1;kill -KILL "$n" 2>/dev/null) >/dev/null 2>&1 & g=$!
 wait "$n";kill "$g" 2>/dev/null;wait "$g" 2>/dev/null
 exit 0
)
mark entry ENTRY
ram=/private/tmp/r16-autolog-v2
[ "$mode" = production ] || ram="$ram-$mode"
[ ! -d "$ram" ] || { mark entry GUARD_EXIT; exit 0; }
mkdir "$ram" 2>/dev/null || { mark entry RAM_FAIL; exit 75; }
mark phase RAM_READY
exec > "$ram/collector.log" 2>&1
echo "$token"
date
out=
owned=
oc=ERROR
parent=$$
deadline=480
[ "$mode" != --test-timeout ] || deadline=2

# Bound commands so a hung storage query cannot block every other diagnostic.
run() {
    se=$1; file=$2; shift 2
    "$@" > "$ram/$file" 2>&1 &
    job=$!
    ( sleep "$se"; kill -TERM "$job" 2>/dev/null; sleep 2; kill -KILL "$job" 2>/dev/null ) &
    watch=$!
    wait "$job"
    code=$?
    kill "$watch" 2>/dev/null
    wait "$watch" 2>/dev/null
    echo "$file exit=$code"
    return 0
}

finish() {
    code=$1
    mark phase FINISH
    trap - EXIT HUP INT TERM
    [ ! -f "$ram/timed-out" ] || oc=TIMEOUT
    [ "$code" -eq 0 ] || { [ "$oc" = TIMEOUT ] || oc=ERROR; }
    echo "$oc exit=$code" > "$ram/reboot-reason.txt"
    if [ -n "$out" ]; then
        cp "$ram"/* "$out/" 2>/dev/null || oc=ERROR_SAVING_LOGS
        echo "$oc $token" > "$out/status.txt"
        echo 'Restart requested.' > "$out/reboot.txt"
    fi
    echo "R16 diagnostics: $oc; restarting." > /dev/console 2>/dev/null
    run 5 sync.txt /bin/sync
    if [ -n "$owned" ]; then
        run 8 unmount.txt /sbin/umount "$owned"
        run 5 sync.txt /bin/sync
    fi
    if [ "$mode" = production ]; then
        sleep 10
        mark phase REBOOT
        /sbin/reboot
        # Keep the independent deadline alive if the normal reboot fails.
        echo 'Reboot returned.'
        wait "$dp"
    else
        echo "$oc" > "$ram/reboot-requested-test.txt"
        kill "$dp" 2>/dev/null
    fi
}
trap 'finish "$?"' EXIT
trap 'exit 143' HUP INT TERM
(
    sleep "$deadline"
    mark phase DEADLINE
    echo TIMEOUT > "$ram/timed-out"
    # Keep the parent alive: launchd may terminate its process group on exit.
    [ ! -f "$ram/out-point" ] || out=$(cat "$ram/out-point")
    [ -z "$out" ] || echo "TIMEOUT $token" > "$out/status.txt"
    run 5 deadline-sync.txt /bin/sync
    [ ! -f "$ram/owned-point" ] || run 8 deadline-unmount.txt /sbin/umount "$(cat "$ram/owned-point")"
    [ "$mode" != production ] || /sbin/reboot
) &
dp=$!

# Visible startup signal, after the independent deadline is armed.
# Recovery contains Terminal but not the normal macOS open/osascript tools.
# Never wait for the application or require the user to close it.
if [ "$mode" = production ]; then
 /System/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal > "$ram/startup-window.log" 2>&1 &
fi

[ "$mode" != --test-error ] || exit 7
if [ "$mode" = --test-timeout ]; then sleep 5; exit 124; fi


target=
ft() {
    [ -z "$out" ] || return 0
    # Refuse ambiguous duplicate FAT mounts, rather than allocating through
    # independent caches for the same device.
    du=$(mount | grep '(msdos' | awk '{print $1}' | sort | uniq -d)
    [ -z "$du" ] || return 1
    for marker in /Volumes/*/r16-autolog/capture-target.txt /Volumes/*/EFI/OC/r16-autolog/capture-target.txt; do
        [ -f "$marker" ] || continue
        [ "$(cat "$marker")" = "$token" ] || continue
        ca=${marker%/capture-target.txt}
        co="$ca/network-logs-$(date +%Y%m%d-%H%M%S)-$$"
        if mkdir "$co" 2>/dev/null; then target=$ca; out=$co; return 0; fi
    done
    return 1
}

mark phase STORAGE
run 15 disk-list-initial.txt diskutil list
run 10 system-version.txt sw_vers
run 10 kernel.txt uname -a

# Match GPT partition UUIDs, then require the private marker before writing.
attempt=0
while [ "$attempt" -lt 6 ]; do
    ft && break
    attempt=$((attempt + 1))
    run 12 disk-list.txt diskutil list
    printf '%s\n' /dev/disk*s* | sed 's|/dev/||' | grep -E '^disk[0-9]+s[0-9]+$' > "$ram/candidates.txt"
    while read dev; do
        run 8 "$dev.plist" diskutil info -plist "$dev"
        uuid=$(PlistBuddy -c 'Print :DiskUUID' "$ram/$dev.plist" 2>/dev/null | tr '[:lower:]' '[:upper:]')
        case "$uuid" in
            D228C57E-717B-4433-945D-BE8BE9852C75|56966F71-F263-4C6F-BCA7-912ED89ECDCA) ;;
            *) [ "$mode" = --test-success ] && [ -n "$tu" ] && [ "$uuid" = "$tu" ] || continue ;;
        esac
        # diskutil can leave a daemon-side mount pending after its client times
        # out. Never combine it with a second direct mount of the same device.
        if mount | grep -q "^/dev/$dev on "; then ft && break; continue; fi
        point=/Volumes/R16Capture-$dev
        mkdir -p "$point" || continue
        run 10 "fat-$dev.txt" /sbin/mount -t msdos "/dev/$dev" "$point"
        if mount | grep -q "^/dev/$dev on $point "; then owned=$point; echo "$point" > "$ram/owned-point"; fi
        ft && break
    done < "$ram/candidates.txt"
    ft && break
    sleep 5
done

if [ -n "$out" ]; then
    mark phase TARGET
    echo "$out" > "$ram/out-point"
    echo "STARTED_NETWORK $token" > "$out/status.txt"
    cp "$ram"/* "$out/" 2>/dev/null
    run 5 sync.txt /bin/sync
fi

if [ -z "$out" ]; then
    oc=NO_WRITABLE_LOG_TARGET
    echo 'No log target.'
    exit 3
fi
if [ "$mode" != production ]; then
    echo 'TEST_ONLY' > "$ram/network-result.txt"
    oc=COMPLETE
    exit 0
fi
worker="$target/network-input/run-network-test.sh"
collector="$target/network-input/collect_network_state.sh"
expected_worker=698db6967f6e2aeda782e147627d09a3c0180cf7467652dd48b659ca70bf8400
expected_collector=78b58f9ea4a640e7d38cab6cee6626d1ecc0f7af51bb7984e39a2de131de5c1d
for script in "$worker" "$collector"; do
    [ -f "$script" ] || { echo 'Missing input.'; exit 4; }
    actual=$(/sbin/sha256 -q "$script" 2>/dev/null)
    ex=$expected_collector
    [ "$script" != "$worker" ] || ex=$expected_worker
    [ "$actual" = "$ex" ] || { echo 'Bad digest.'; exit 5; }
done
echo SCRIPT_DIGESTS_VERIFIED > "$out/launcher-phase.txt"

mark phase WORKER
/bin/sh "$worker" "$out"
we=$?
printf 'worker_exit=%s\n' "$we" > "$out/worker-exit.txt"
[ "$we" -eq 0 ] || exit "$we"
oc=COMPLETE
exit 0
