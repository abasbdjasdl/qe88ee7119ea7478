# Ventura legacy AirPort scan-result slice

`LegacyScanResult.hpp` translates an actual completed RTL8852BE scan-cache
`nativescan::Entry` into the Ventura `apple80211_scan_result` payload used by
AirportItlwm v2.3.0 and the restored legacy IO80211 stack. The production
Ethernet kext does not compile this directory. There is no IO80211Controller
personality, interface registration, scan-completion event, or native Wi-Fi
menu in this slice.

The ABI input is pinned to OpenIntelWireless/itlwm commit
`53c51c2cdd6e4b69beb91f310d74c53422b0f8bd`, whose
`include/Airport/apple80211_var.h` matches the v2.3.0 header (SHA-256
`3ac3f7bb1893ad35a04bf243ad3b48eafdb2d67593fd0485ba15703233a3d9d1`).
Build with `-D__IO80211_TARGET=130000`, that source's `include` directory
*before* MacKernelSDK, and the existing pinned MacKernelSDK. The adapter
checks the 1164-byte object size and the offsets it writes. MacKernelSDK's
other `IOKit/80211/apple80211_var.h` exposes a pointer-shaped older result
and is **not** interchangeable.

The bounded encoder preserves the observed SSID (including embedded NUL),
BSSID, band/channel, beacon interval, capabilities, supported rates, measured
dBm, age, and raw RSN/WPA security TLVs. It rejects unknown/percent-only
signal, malformed IEs, and over-capacity output. Noise and SNR are left unset
because the current backend has no verified noise measurement. It does not
claim that an RSSI sample belongs to the exact received MPDU.

The separate [AirPort_RTW88](https://github.com/xnoah222/Airport_RTW88)
controller is the closer Realtek frontend reference: its `AirportRTW88.cpp`
shows IO80211 request dispatch and scan completion events. Its rtw88 hardware
backend and fixed-noise/cache-only scan shortcuts are not part of this port.
The existing RTL8852BE `MacNetworkController.cpp` remains the only hardware
owner and must be adapted to the IO80211 lifecycle before an interface can be
published. In particular, `runControlAction()` intentionally rejects calls
from the hardware workloop gate, so its current public status/scan methods
cannot simply be called from an IO80211 request handler on that gate.

`tests/legacy_airport_scan_result_test.cpp` compiles against the actual pinned
headers as a Darwin object. It also runs on Windows against the minimal
`tests/legacy_airport_fakes` structure definition; that run checks translation
behavior, while the actual-header compilation checks the target layout. Both
are narrower than a kext load or macOS UI test.
