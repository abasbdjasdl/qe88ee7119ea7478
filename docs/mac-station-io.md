# Native station register and packet adapters

`MacStationIo` implements AX RTL8852B station-only port, RX-filter and table
window access using pinned rtw89 commit
`d1fced1b8a741dc9f92b47c69489c24385945f6e`. It requires PCI 10ec:b852,
the actual BAR2 mapping of at least 0x40020 bytes, CMAC0 enabled, workloop gate
ownership and the radio owner's scheduler-paused acknowledgement. It additionally
checks the real CTN_TXEN register before station writes. This object never owns
the radio scheduler or substitutes a callback for its register acknowledgement.

Table seeding uses the shared `StationTables` source-derived DMAC/CMAC sequence
through the actual indirect address/data window. The external shared-window
owner predicate prevents concurrent use. Local address, BSSID and AID CAM writes
remain the shared firmware `NativeProgrammer` owner's responsibility.

Port updates follow `rtw89_mac_port_update`: no-link/station type, RX BSSID and
TSF controls, beacon interval, HIQ/DTIM, TBTT, early beacon and MBSSID-drop fields.
A previously enabled port waits its actual beacon interval plus 1 ms through
`servicePort`, without sleeping the gate. Port operations are bounded to 2 s;
connected intervals must be 1–1000 TU and DTIM nonzero. AP beacon draining and
DBCC are rejected. A partially failed update requires reset. Scan filtering
temporarily clears the source scan flags and restores the exact saved value.

`StationIoCore` supplies the portable legacy TX/EDCA/PPDU algorithms. TX accepts
only non-QoS, non-fragmented three-address legacy frames and 20 MHz channels.
Software-encrypted frames retain their original bytes; no hardware cipher is
enabled. `hdr_llc_len` is 24 / 2 for these data frames, matching
`rtw89_core_tx_update_llc_hdr`, which uses IEEE 802.11 header length alone.
Management/null frames use the source management queue and hardware sequence
policy. Data uses the existing software sequence. The lowest valid peer basic
rate supplies the fixed legacy rate; no HT/VHT/HE or rate-adaptation claim follows.

EDCA encodes the actual source 12-byte H2C layout. The caller must submit
category 1 / class 9 / function 0x0f to the shared command bus and wait for its
real DONE ACK. It is not a direct register update or successful association.
The controller passes net80211's selected-node beacon interval/basic rates and
legacy BE contention parameters. DTIM uses the selected node's observed period,
or pinned net80211's bootstrap value 1 if the selected probe response had no TIM;
that fallback is not evidence that the AP advertised DTIM 1. Power save is not
advertised by this controller.

PHY parsing bounds the optional MAC report, users, RX counters, PLCP header and
every AX PHY IE. RSSI A/B are extracted from PHY header word 1. The 8852B source
conversion is `max(A,B)/2 - 110` dBm, normalized to 0–100 for net80211. IE01
provides channel evidence when present; a report without channel evidence needs
the radio owner's independently verified current channel. The controller clears
sample/cache state across tune/admission boundaries and retains at most eight
first RX frames for a matching real PPDU counter/rate report for 250 ms.

Validation: portable UBSan tests exercise TX rejection and encoding, EDCA wire
bytes, 40 MAC/PHY report layouts, malformed/truncated reports, RSSI/channel
decoding and PPDU-cache lifetime. Both native translation units compile against
the pinned MacKernelSDK/itlwm headers. These are not MMIO, AP association,
firmware acknowledgement or network-throughput tests.
