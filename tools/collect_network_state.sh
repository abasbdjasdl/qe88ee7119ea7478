#!/bin/sh
# One read-only snapshot. Reboot/loading/association remain the caller's job.
set -u
umask 077

valid_interface() {
    case "$1" in en*) n=${1#en}; case "$n" in ''|*[!0-9]*) return 1;; esac;; *) return 1;; esac
}
classify() {
    if [ "$1" != yes ]; then printf '%s\n' no_unique_target_interface
    elif [ "$2" != active ]; then printf '%s\n' target_link_not_active
    elif [ -z "$3" ]; then printf '%s\n' target_link_active_no_ipv4
    else case "$3" in 169.254.*|0.*) printf '%s\n' target_link_active_no_routable_ipv4;;
        *) printf '%s\n' target_link_active_ipv4_present;; esac
    fi
}
if [ "${1-}" = --self-test ]; then
    valid_interface en0 && valid_interface en123 || exit 1
    for bad in en en0x 'en0;echo' wlan0 ''; do valid_interface "$bad" && exit 1; done
    [ "$(classify no active 192.0.2.1)" = no_unique_target_interface ] || exit 1
    [ "$(classify yes inactive 192.0.2.1)" = target_link_not_active ] || exit 1
    [ "$(classify yes active '')" = target_link_active_no_ipv4 ] || exit 1
    [ "$(classify yes active 169.254.1.2)" = target_link_active_no_routable_ipv4 ] || exit 1
    [ "$(classify yes active 192.0.2.1)" = target_link_active_ipv4_present ] || exit 1
    printf '%s\n' 'PASS: interface validation and evidence classification; no live commands run'
    exit 0
fi
if [ "$#" != 1 ] && [ "$#" != 3 ]; then
    echo 'usage: sh collect_network_state.sh /absolute/new-report.txt [--check-url https://host/path]' >&2
    exit 64
fi
report=$1
case "$report" in /*) ;; *) echo 'An absolute output file is required.' >&2; exit 64;; esac
url=
if [ "$#" = 3 ]; then
    [ "$2" = --check-url ] || exit 64
    url=$3
    case "$url" in http://*|https://*) ;; *) exit 64;; esac
    # Avoid credentials/query tokens, URL option confusion and multiline values.
    case "$url" in *'@'*|*'?'*|*'#'*|*' '*|*'
'*) echo 'Use a credential-free HTTP(S) URL without query/fragment.' >&2; exit 64;; esac
fi
# Do not overwrite an old experiment or follow a pre-existing report symlink.
if ! (set -C; : > "$report") 2>/dev/null; then echo 'Output must be a new writable file.' >&2; exit 73; fi
exec 3>"$report" || exit 73
exec 4>"$report.command-status.txt" || exit 73

# Every actual command has a bounded lifetime, including both sides of the
# registry pipeline. No raw registry bytes are ever redirected to any file.
bounded() (
    seconds=$1; shift
    exec 9<&0
    "$@" <&9 9<&- & command_pid=$!
    (sleep "$seconds"; kill -TERM "$command_pid" 2>/dev/null; sleep 1; kill -KILL "$command_pid" 2>/dev/null) >/dev/null 2>&1 & guard_pid=$!
    trap 'kill -TERM "$command_pid" "$guard_pid" 2>/dev/null' 1 2 15
    wait "$command_pid"; result=$?
    kill -TERM "$guard_pid" 2>/dev/null
    wait "$guard_pid" 2>/dev/null
    exit "$result"
)
registry_value() {
    ( bounded 3 /usr/sbin/ioreg -r -c R16RTL8852BE -a -d 2 2>/dev/null
      code=$?; printf 'query=%s ioreg_exit=%s\n' "$1" "$code" >&4 ) |
        bounded 3 /usr/bin/plutil -extract "$1" raw -o - - 2>/dev/null
    code=$?; printf 'query=%s plutil_exit=%s\n' "$1" "$code" >&4
    return "$code"
}
record() {
    label=$1; shift
    printf '\n[%s]\n' "$label" >&3
    bounded 5 "$@" >&3 2>/dev/null; status=$?
    printf 'command_exit=%s\n' "$status" >&3
}
printf 'format=R16-network-capture-v1\ncollector=read_only_snapshot\n' >&3
printf 'utc=%s\n' "$(bounded 3 /bin/date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null)" >&3
printf 'os=%s\n' "$(bounded 3 /usr/bin/sw_vers -productVersion 2>/dev/null)" >&3
record kext_load /usr/sbin/kextstat -l -b local.rtl8852be.network

provider_value() {
    ( bounded 3 /usr/sbin/ioreg -r -k R16NetworkTestId -a -d 1 2>/dev/null
      code=$?; printf 'provider_query=%s ioreg_exit=%s\n' "$1" "$code" >&4 ) |
        bounded 3 /usr/bin/plutil -extract "$1" raw -o - - 2>/dev/null
    code=$?; printf 'provider_query=%s plutil_exit=%s\n' "$1" "$code" >&4
    return "$code"
}
printf '\n[persistent_pci_startup]\n' >&3
test_id=$(provider_value 0.R16NetworkTestId) || test_id=
second_id=$(provider_value 1.R16NetworkTestId) || second_id=
if [ "$test_id" = NETWORK-START-02 ] && [ -z "$second_id" ]; then
    printf 'provider_test_id=NETWORK-START-02\n' >&3
    for key in R16NetworkStage R16NetworkStartFailed R16ProbeStage R16ProbeError R16ProbePciError R16ProbePowerError R16ProbePowerAddress R16ProbePowerExpected R16ProbePowerActual R16ProbePowerReads R16ProbePowerWrites R16ProbeDownloadStatus R16ProbePollAddress R16ProbePollMask R16ProbePollWanted R16ProbePollActual R16MacStage R16MacError R16MacAddress R16MacExpected R16MacActual R16PrepStage R16PrepError R16PrepAddress R16PrepWanted R16PrepActual R16FwOperation R16FwPhase R16FwFailedPhase R16FwControl R16FwIndex R16FwQuiesced R16FwReleased R16FwRetained R16BankStatus R16BankSlot R16DmaAddress R16DmaExpected R16DmaActual R16DmaBusy R16DmaPolls R16DmaMasked R16DmaMasterOff R16DmaIdle R16DmaStopped R16RadioPhase R16RadioStage R16RadioStep R16RadioError R16RadioCommandError R16RadioCommandsUsed R16RadioUploadPath R16RadioUploadPage R16BtInitStage R16BtInitError R16BtInitAddress R16BtLeaseStage R16BtLeaseError R16BtLeaseAddress R16BtLeaseHeld R16BtLeaseConsumed R16BtLeaseEnded R16RfkNativeLease R16RfkStage R16RfkError R16RfkSpace R16RfkPath R16RfkAddress R16RfkMask R16RfkValue R16RfkOperations R16RfkPolls R16PhyStage R16PhyError R16PhyAddress R16PhyExpected R16PhyActual R16ChannelStage R16ChannelError R16ChannelAddress R16RadioIoStatus R16RadioSnapshotComplete R16BtInitValue R16BtInitRfPath R16BtInitRfAddress R16BtInitRfExpected R16BtInitOperations R16BtInitPolls R16BtInitRadioStatus R16BtRfReadAddress R16BtRfReadValue R16BtRfWriteAddress R16BtRfWriteValue R16BtRfWriteCommand; do
        value=$(provider_value "0.$key") || value=unknown
        case "$value" in true|false|unknown) ;; ''|*[!0-9]*) value=invalid;; esac
        printf '%s=%s\n' "$key" "$value" >&3
    done
else printf 'provider_test_id=missing_or_ambiguous\n' >&3
fi

target=; matched=no
if [ -x /usr/sbin/ioreg ] && [ -x /usr/bin/plutil ]; then
    owner=$(registry_value 0.IOObjectClass) || owner=
    second=$(registry_value 1.IOObjectClass) || second=
    printf '\ncontroller_class=%s\n' "$owner" >&3
    if [ "$owner" = R16RTL8852BE ] && [ -z "$second" ]; then
        failure=$(registry_value 0.R16Failure) || failure=
        [ -z "$failure" ] || printf 'driver_failure=%s\n' "$failure" >&3
        count=0; index=0
        # attachInterface places the Ethernet interface directly below this
        # concrete controller. Unknown/deeper layouts are not guessed as en0.
        while [ "$index" -lt 8 ]; do
            class=$(registry_value "0.IORegistryEntryChildren.$index.IOObjectClass") || class=
            [ -n "$class" ] || break
            if [ "$class" = IOEthernetInterface ]; then
                name=$(registry_value "0.IORegistryEntryChildren.$index.BSD Name") || name=
                if valid_interface "$name"; then target=$name; count=$((count+1)); fi
            fi
            index=$((index+1))
        done
        if [ "$index" -eq 8 ]; then
            extra=$(registry_value 0.IORegistryEntryChildren.8.IOObjectClass) || extra=
            [ -z "$extra" ] || count=99
        fi
        if [ "$count" -eq 1 ]; then matched=yes; else target=; fi
        printf 'matching_interface_count=%s\n' "$count" >&3
    else printf 'registry_owner=missing_or_ambiguous\n' >&3
    fi
else printf 'registry_tools=unavailable\n' >&3
fi
printf 'target_interface=%s\n' "$target" >&3
link=unknown; ipv4=
if [ "$matched" = yes ]; then
    record target_ifconfig /sbin/ifconfig "$target"
    link=$(bounded 3 /sbin/ifconfig "$target" 2>/dev/null | /usr/bin/awk '/^[[:space:]]*status:/{print $2;exit}')
    ipv4=$(bounded 3 /usr/sbin/ipconfig getifaddr "$target" 2>/dev/null) || ipv4=
    printf 'target_link=%s\ntarget_ipv4=%s\n' "$link" "$ipv4" >&3
    # Never dump a DHCP packet: it may include hostnames or other identifiers.
    for option in server_identifier lease_time subnet_mask router domain_name_server; do
        record "target_dhcp_$option" /usr/sbin/ipconfig getoption "$target" "$option"
    done
    printf '\n[target_ipv4_routes]\n' >&3
    bounded 5 /usr/sbin/netstat -rn -f inet 2>/dev/null |
        /usr/bin/awk -v iface="$target" '{for(i=1;i<=NF;i++)if($i==iface){print;break}}' >&3
fi
printf '\n[system_default_route_separate_from_target]\n' >&3
default=$(bounded 4 /sbin/route -n get default 2>/dev/null |
    /usr/bin/awk '/^[[:space:]]*(interface|gateway):/{print $1,$2}')
printf '%s\n' "$default" >&3
default_if=$(printf '%s\n' "$default" | /usr/bin/awk '$1=="interface:"{print $2;exit}')
if [ "$matched" = yes ] && [ "$default_if" = "$target" ]; then
    printf 'default_route_uses_target=yes\n' >&3
else printf 'default_route_uses_target=no_or_unknown\n' >&3
fi

printf '\n[driver_numeric_dmesg_diagnostics]\n' >&3
# Only reconstruct known numeric/native-phase diagnostics. Do not write raw
# dmesg, arbitrary Wi-Fi messages, registry dumps, SSIDs or key material.
bounded 5 /sbin/dmesg 2>/dev/null | /usr/bin/awk '
 /RTL8852BE:? (radio failure:|probe failure stage=|probe MAC stage=|probe cycle preparation stage=|probe download PCI stage=)/ {
   p=index($0,"RTL8852BE"); s=substr($0,p); n=split(s,a," ");
   kind="radio_failure"; if(s ~ /probe failure/)kind="probe_failure";
   if(s ~ /probe MAC/)kind="probe_mac"; if(s ~ /probe cycle/)kind="probe_preparation";
   if(s ~ /probe download/)kind="probe_download_pci";
   out="RTL8852BE diagnostic=" kind;
   for(i=1;i<=n;i++) {
     if(a[i] ~ /^phase=[A-Za-z0-9_-]+$/)out=out " " a[i];
     else if(a[i] ~ /^(stage|step|error|cmd|used|epoch|cut|cycle|address|wanted|actual|failureAddress|fw|operation|failedPhase|lastControl|lastIndex|retained|pciError|pciAddress|pciValue|powerError|powerAddress|pollAddress|pollMask|pollWanted|pollActual|pollReason|btinit|btlease|held|consumed|ended|native|rfk|space|path|mask|value|ops|polls|phy|expected|channel|radioio)=[0-9a-fA-Fx\/@-]+$/)out=out " " a[i];
   }
   print out; if(++shown>=40)exit;
 }' >&3
printf '\nsummary=%s\n' "$(classify "$matched" "$link" "$ipv4")" >&3
printf 'internet_reachability=not_tested\n' >&3
if [ -n "$url" ]; then
    if [ "$matched" != yes ]; then printf 'optional_url_check=skipped_no_unique_interface\n' >&3
    else
        printf '\n[explicit_optional_url_check_bound_to_target]\n' >&3
        bounded 12 /usr/bin/curl --interface "$target" --connect-timeout 4 --max-time 8 \
            --silent --head --output /dev/null --write-out 'http_code=%{http_code}\n' -- "$url" >&3 2>/dev/null
        printf 'curl_exit=%s\n' "$?" >&3
    fi
fi
printf 'capture_complete=yes\nreboot_owner=caller\n' >&3
exec 3>&-
exit 0
