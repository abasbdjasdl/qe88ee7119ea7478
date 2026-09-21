# Reusing a macOS Wi-Fi stack for RTL8852BE

This is an unfinished port. There is no installable networking kext and no
demonstrated association, DHCP lease or Internet connection. The live diagnostic
0.0.13 remains unchanged. Network components must not be packaged as a functional
driver simply because the archive compiles.

## Code reused

- `OpenIntelWireless/itlwm` at `53c51c2cdd6e4b69beb91f310d74c53422b0f8bd`:
  the existing macOS/OpenBSD net80211 state machine, packet encapsulation,
  decapsulation, station authentication, RSN/EAPOL and software ciphers. The
  upstream files are fetched unchanged and built separately with their GPL and
  embedded BSD/ISC notices. Intel device backends and Intel firmware are excluded.
- `lwfinger/rtw89` at `d1fced1b8a741dc9f92b47c69489c24385945f6e`:
  the actual 8852B 24-byte TXWD + 24-byte TXWI construction functions and AX RXWD
  parser. `import_network_reference.py` preserves their function bodies and
  relevant structures/masks, with a small descriptor-only type adapter. This is
  not a Linux kernel compatibility layer. BSD option selected for these files.

The imported descriptor parser assumes trusted buffer lengths. The new wrapper
checks the PCI prefix offset, short/long descriptor size, shift, driver-info
overhead and payload size before exposing any slice. It uses an aligned local
descriptor copy and rejects ICV/CRC errors. It distinguishes Wi-Fi packets from
C2H and other event reports, rather than handing all DMA payloads to net80211.

`Net80211PacketBridge.cpp` hands complete Realtek frames (FCS removed) into the
existing receive state machine. TX prioritizes management traffic; Ethernet data
passes through existing encapsulation and software encryption. Packet/node
ownership is explicit. No key, association or DMA completion is invented.
Hardware-decrypted RX is rejected until CAM/key and replay metadata support
exists. The bridge must run under a controller command gate after net80211
attachment, using real channel/RSSI metadata; it does not initialize that host.

## Build and verification

`network-port.yml` builds the pinned protocol source and bridge with Apple kernel
headers. It produces a static archive and a relocatable-link check, verifies
that protocol/crypto references are resolved, and reports remaining kernel/host
imports. It is not a loadable kext. Source and licenses accompany the archive.
`network_descriptors_test.cpp` checks golden TX words, unaligned RX, every buffer
truncation, invalid lengths/fields, corruption flags, C2H separation and 100000
malformed-buffer cases under ASan/UBSan. These are software evidence only.

## Remaining real integration

1. Complete and preserve the RTL8852B power/MAC/PHY/RF/efuse/calibration sequence;
   the diagnostic subset currently shuts the chip down after probing.
2. Implement normal RXQ/RPQ and data/management TX rings, interrupts, TX release
   reports, reusable DMA ownership and error recovery. The one-shot CH12 bank
   is not the normal networking data path.
3. Adapt rtw89 firmware commands, event dispatch, channel/power/regulatory data,
   station/address CAM and association transitions to the net80211 callbacks.
4. Add IOEthernetController/workloop/timer lifecycle, interface statistics,
   control client for scan/join, key handling, disconnect and suspend/resume.
5. Verify on this hardware: firmware execution, scan results, real AP
   authentication, EAPOL exchange, DHCP, packet TX/RX and connection stability.

The missing hardware backend is substantial. Imported protocols and successful
compilation do not establish an operating radio or a working Internet connection.
No new reboot test is requested for these component-only changes.

## Licensing

Existing diagnostic and descriptor-wrapper code remains BSD-3-Clause. The
net80211 bridge is GPL-2.0-or-later, and an eventual combined binary must comply
with the upstream itlwm GPL license and retained per-file notices. See
`src/network/COPYING.itlwm`. The firmware keeps its separate Realtek license.
