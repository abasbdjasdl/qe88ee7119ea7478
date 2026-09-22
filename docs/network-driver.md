# RTL8852BE integrated experimental driver

`R16RTL8852BE` is the concrete PCI personality. Its `BootService` connects the
two-cycle firmware/calibration loader, native MAC and PHY initialization,
radio/BT calibration, PCI DMA/MSI controller, station table programming and the
pinned itlwm net80211/WPA2 stack. The build now links a separate
`RTL8852BENetwork.kext`; the installed diagnostic and EFI are not changed by a build.

The initial connection profile is deliberately small: one infrastructure station,
2.4 GHz channels 1–11, 20 MHz, fixed legacy rates, open or WPA2-CCMP networking,
software encryption, and an Ethernet interface presented to macOS. macOS supplies
IP, ARP and DHCP once the real authenticated controlled port becomes active.
There is no fabricated scan result, association response or DHCP success signal.
Concurrent Bluetooth, 5 GHz/DFS, HT/VHT/HE, QoS/aggregation, WPA3 and sleep/resume are
outside this initial profile.

The channel profile takes the initiation-allowed 1–11 subset of Linux v6.12
`net/wireless/reg.c`'s world domain. Power programming intersects the unchanged
rtw89 WORLD power table with an additional **0 dBm conducted** ceiling. It does
not infer the user's country or grant access to the other chip-supported channels.

## Execution and ownership

The first firmware power cycle reads physical eFuse/PHY calibration and firmware
capabilities. After verified DMA/CPU/power shutdown, a second cycle uses those
calibration values, downloads unchanged vendor firmware and keeps its CPU alive.
Fresh runtime rings and MSI then enable the one global firmware command bus.
Offload configuration completes only on the real matching firmware DONE ACK.

Native radio setup applies BB/RF tables, initializes PHY controls, and performs
RCK/DACK/RXDC under explicit ownership. Initial home-channel and later association
actions program channel/power plus RXDC/IQK/TSSI/DPK before opening normal traffic.
Scans use the source's TSSI scan path and restore the saved home calibration.
Bluetooth policy explicitly selects the source's WLAN-only mode; it does not
pretend to implement dynamic coexistence notifications.

MACID/port/CAM/CMAC/EDCA updates are serialized with radio work. Published TX frames
must drain before changing the channel or station tables. Real AP authentication
and association frames enter net80211; Join/CAM acknowledgements alone never open
the controlled port. WPA2 requires net80211's real RSN port-valid result.

The global command sequence allocator currently allows 256 commands in a physical
firmware epoch. Exhaustion requires verified recovery; counters are not silently
reused. The initial scan/association path is designed to fit this bound. Repeated
scans/reconnects are not an unlimited service claim.

## Build and local configuration

GitHub's network workflow runs the component fault tests, imports pinned source
constants, compiles native kernel adapters and the protocol stack, and links an
x86_64 Mach-O KEXT bundle. It checks unresolved protocol/local symbols, the concrete
personality, kmod entry points, and an unchanged embedded firmware hash. Compilation
and link checks do **not** prove loading or RF/network operation on hardware.

Public artifacts contain no SSID or keys. To produce a local configured copy, use
`tools/configure_network_bundle.py` with a raw SSID file and a passphrase file
(neither file includes a trailing newline), or explicitly select an open network.
The tool derives a 32-byte PSK outside the kernel and stores only that PSK and the
SSID in the copied personality. The resulting local bundle contains credentials
and must not be uploaded to the public repository. The tool neither installs the
bundle nor changes boot settings.

Hardware-tested, load-verified and Wi-Fi-operational fields stay false until their
respective real evidence exists. Source, firmware and GPL notices accompany the
experimental artifact.
