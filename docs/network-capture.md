# One-shot integrated network capture

`tools/collect_network_state.sh` is a staged collector, not an installed recovery
startup hook. It does not load/unload drivers, change networking, associate,
reboot or touch EFI. The existing caller owns its waiting period and final
reboot on completion/error. Invoke it after the driver's normal startup and an
appropriate association/DHCP observation interval, using a **new absolute output
file** in the caller's existing experiment directory:

```sh
/bin/sh /path/to/collect_network_state.sh "$experiment_dir/network-state.txt"
```

The parent directory must already exist. The collector refuses to overwrite an
existing report. Exit 0 means capture finished, not that networking succeeded.
Unavailable/failed commands are not positive networking evidence. Each command
has a 3–5 second limit (TERM, then KILL after another second), and enumeration
is capped at eight direct children. There is no polling/retry loop for association.
The optional HTTP command has its own 12-second outer bound. A severely stalled
machine can take several minutes across all independent command limits; the
caller should retain its overall experiment watchdog.

**Replace or skip the old recovery-autolog `pci-ioreg`/raw tree captures and
unfiltered `log show` before using a credential-configured network driver.**
`R16SSID`/`R16PSK` are registry properties; dumping the controller or its PCI
subtree would persist credentials. This script never writes raw registry XML
to disk, even temporarily. It pipes targeted class output into `plutil -extract`
and writes only whitelisted class/interface/failure scalars. It does not rely
on grep as an XML sanitizer. Raw DHCP packets and raw dmesg are also excluded;
only specific DHCP options and reconstructed native diagnostic fields are saved.
Reports still contain intentional network facts such as MAC/IP/gateway addresses.

The collector requires exactly one `R16RTL8852BE` owner and one direct
`IOEthernetInterface` child with an `en` plus digits BSD name. Missing tools,
unknown tree shapes, multiple owners/interfaces or a truncated child enumeration
yield no target; it never guesses `en0`. It records that target's link, IPv4,
DHCP options and IPv4 routes. The system default route is separately labeled and
compared with the target; a different active interface cannot satisfy the target
summary. An IPv4 address is not itself proof of DHCP success or Internet access,
and 169.254/0.x addresses are not treated as usable IPv4 evidence.

Default capture makes no intentional outbound network request. An explicit
optional check can bind a HEAD request to the verified target interface:

```sh
/bin/sh /path/to/collect_network_state.sh "$experiment_dir/network-check.txt" \
  --check-url https://example.com/
```

Do not add this flag without authorization for the network request. The URL is
not logged; credential-bearing URLs, queries and fragments are rejected. Curl
exit status and HTTP code remain separate evidence. DNS resolution may use the
system resolver, so this is not a target-interface DNS test. No redirect is
followed. Recovery may omit curl or have incomplete trust roots.

Portable checks (no live Mac commands):

```sh
sh -n tools/collect_network_state.sh
sh tools/collect_network_state.sh --self-test
```

The self-test covers interface-name validation and truthful evidence summaries.
It does not emulate macOS IORegistry/plutil output, command termination behavior
or prove execution in the recovery image. Real hook installation and one-shot
reboot integration remain separate work.
