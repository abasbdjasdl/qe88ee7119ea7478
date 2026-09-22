#!/bin/sh
# Driver loading is owned by OpenCore. This worker only enables its exact interface.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/libexec
export PATH
umask 077
ram=$1
here=${0%/*}
phase() { printf '%s\n' "$1" > "$ram/network-phase.txt"; }
bounded() (
 seconds=$1; shift
 if [ "${end:-0}" -gt 0 ]; then
  remaining=$((end-$(date +%s)))
  [ "$remaining" -gt 0 ] || exit 124
  [ "$remaining" -ge "$seconds" ] || seconds=$remaining
 fi
 exec 9<&0
    "$@" <&9 9<&- & job=$!
 (sleep "$seconds"; kill -TERM "$job" 2>/dev/null; sleep 1; kill -KILL "$job" 2>/dev/null) >/dev/null 2>&1 & guard=$!
 trap 'kill -TERM "$job" "$guard" 2>/dev/null' HUP INT TERM
 wait "$job"; result=$?
 kill -TERM "$guard" 2>/dev/null; wait "$guard" 2>/dev/null
 exit "$result"
)
value() {
    ( bounded 3 /usr/sbin/ioreg -r -c R16RTL8852BE -a -d 2 2>/dev/null
      code=$?; printf 'query=%s ioreg_exit=%s\n' "$1" "$code" >&4 ) |
        bounded 3 /usr/bin/plutil -extract "$1" raw -o - - 2>/dev/null
    code=$?; printf 'query=%s plutil_exit=%s\n' "$1" "$code" >&4
    return "$code"
}
resolve() {
 [ "$(value 0.IOObjectClass)" = R16RTL8852BE ] || return 1
 [ -z "$(value 1.IOObjectClass)" ] || return 1
 found=; count=0; i=0
 while [ "$i" -lt 8 ]; do
  c=$(value "0.IORegistryEntryChildren.$i.IOObjectClass")
  [ -n "$c" ] || break
  if [ "$c" = IOEthernetInterface ]; then
   n=$(value "0.IORegistryEntryChildren.$i.BSD Name")
   case "$n" in en*) digits=${n#en}; case "$digits" in ''|*[!0-9]*) ;; *) found=$n; count=$((count+1));; esac;; esac
  fi
  i=$((i+1))
 done
 if [ "$i" -eq 8 ] && [ -n "$(value 0.IORegistryEntryChildren.8.IOObjectClass)" ]; then return 1; fi
 [ "$count" -eq 1 ] || return 1
 printf '%s\n' "$found"
}
[ -d "$ram" ] && [ -r "$here/collect_network_state.sh" ] || exit 64
phase waiting_for_concrete_controller_interface
exec 4>"$ram/registry-command-status.txt" || exit 73
start=$(date +%s); end=$((start+120)); target=; ready=no
# Time and attempt bounds both apply; neither an unknown interface nor en0 is used.
attempt=0
while [ "$attempt" -lt 40 ] && [ "$(date +%s)" -lt "$end" ]; do
 attempt=$((attempt+1))
 if [ -z "$target" ]; then
  target=$(resolve) || target=
  [ "$(date +%s)" -lt "$end" ] || break
  if [ -n "$target" ]; then
   printf 'interface=%s\n' "$target" > "$ram/network-enable.txt"
   phase enabling_target_interface
   bounded 5 /sbin/ifconfig "$target" up >> "$ram/network-enable.txt" 2>&1
   printf 'ifconfig_exit=%s\n' "$?" >> "$ram/network-enable.txt"
   phase requesting_target_dhcp
   bounded 8 /usr/sbin/ipconfig set "$target" DHCP >> "$ram/network-enable.txt" 2>&1
   printf 'dhcp_request_exit=%s\n' "$?" >> "$ram/network-enable.txt"
   phase waiting_for_target_link_and_ipv4
  fi
 fi
 if [ -n "$target" ]; then
  link=$(bounded 2 /sbin/ifconfig "$target" 2>/dev/null | awk '/^[[:space:]]*status:/{print $2;exit}')
  ip=$(bounded 2 /usr/sbin/ipconfig getifaddr "$target" 2>/dev/null)
  case "$ip" in ''|169.254.*|0.*) ;; *) if [ "$link" = active ]; then ready=yes; break; fi;; esac
 fi
 [ "$(date +%s)" -lt "$end" ] || break
 remaining=$((end-$(date +%s)))
 [ "$remaining" -gt 0 ] || break
 [ "$remaining" -ge 2 ] || remaining=1
 if [ "$remaining" -ge 2 ]; then sleep 2; else sleep 1; fi
done
wait_elapsed=$(($(date +%s)-start))
end=0
phase collecting_final_evidence
# User authorized this bounded network experiment. HEAD is bound to the verified
# target; collector independently resolves its owner again before making a request.
if [ "$ready" = yes ]; then
 /bin/sh "$here/collect_network_state.sh" "$ram/network-state.txt" --check-url https://example.com/
else
 /bin/sh "$here/collect_network_state.sh" "$ram/network-state.txt"
fi
capture=$?
printf 'worker_target=%s\nwait_link_ipv4=%s\nwait_seconds=%s\ncollector_exit=%s\n' "$target" "$ready" "$wait_elapsed" "$capture" > "$ram/network-result.txt"
if [ "$capture" -ne 0 ]; then phase capture_failed; exit "$capture"; fi
if grep -qx 'curl_exit=0' "$ram/network-state.txt" && grep -Eq '^http_code=[23][0-9][0-9]$' "$ram/network-state.txt"; then
 echo 'http_result=target_bound_https_response_received' >> "$ram/network-result.txt"
else
 echo 'http_result=not_confirmed' >> "$ram/network-result.txt"
fi
phase experiment_complete_results_require_review
exit 0