# RTL8852BE firmware preparation and remaining dependencies

Reference driver commit: `d1fced1b8a741dc9f92b47c69489c24385945f6e`
in https://github.com/lwfinger/rtw89 . References below are to that revision.

## Confirmed hardware baseline

On the R16, PCI 10ec:b852 / 1a3b:5470, the 0.0.3 diagnostic read
SYS_CFG1=0x0C491D39 and SYS_STATUS1=0x1401F278. The digital cut field is 1.
`core.c:rtw89_read_chip_ver` separately reads XTAL_SI_CV for the analog revision;
that operation passed on the physical card in 0.0.4 (raw revision 0x11). PMCSR reported PCI D0, which
does not establish that the wireless MAC, firmware CPU or analog blocks are on.

## Implemented offline component

The pinned `rtw8852b_fw-1.bin` fixture has five container entries. Like
`rtw8852b.c` (`try_ce_fw=true`, format maximum 1) and `fw.c:rtw89_fw_recognize`,
the parser selects type 5 (normal CE) before type 1 for the exact digital cut,
and ignores manufacturing/WoWLAN/log images as upload candidates. All container
entry bounds and overlaps are checked, including non-selected entries. Duplicate
normal/CE candidates for that cut are rejected rather than selecting arbitrarily.

The cut-1 image has version 0.29.29.15, offset 96, size 326616, header 144,
three sections and 163 payload packets with a 2020-byte maximum. Cut 2 has a
separate image at offset 326712, size 326536. The module validates lengths,
header version, chip family, address arithmetic, optional checksum-trailer
length and exact end-of-image accounting. Extra signature blocks and unknown
section types/header versions are intentionally unsupported. It does not verify
the hardware checksum trailer or parse dynamic feature records.

`chunkAt` is an offline segmentation helper. It does not construct transport
descriptors, submit packets, enable DMA, or initiate any firmware handshake.
The downloaded binary stays unmodified; synthetic fixtures cover malformed data.
Kernel compatibility is checked by compiling a separate object, not by changing
the installed diagnostic kext. Host tests do not establish hardware compatibility.

## Why loading the binary is not the next single register write

1. `rtw8852b.c:rtw8852b_pwr_on_func` changes supply/isolation/platform controls,
   waits for ready/ONMAC transitions and accesses the XTAL serial interface.
   It also enables DMAC/CMAC functions and depends on EFUSE/revision state.
   A partial failure needs a supported shutdown path; restoring a register
   snapshot is not equivalent to undoing a hardware power sequence.
2. `mac.c:rtw89_mac_partial_init` powers the chip, enables HCI DMA, initializes
   DMAC/DLE/HFC, and invokes PCI `mac_pre_init` before firmware download.
3. `fw.c:__rtw89_fw_download_main` sends section payloads through the H2C
   transmit path. Consequently the PCI transmit descriptor/ring implementation,
   bounded DMA buffers and hardware ownership/completion handling are required.
   BAR addresses are not substitutes for device-visible DMA addresses.
4. Firmware CPU reset/start, header handshake, section submission and readiness
   checks must follow the reference protocol. Firmware version alone does not
   prove the MAC command/event ABI works.
5. EFUSE/MAC address, RF calibration, scan/association, security and integration
   with the macOS network stack still follow successful firmware startup.

Versions 0.0.4, 0.0.5 and 0.0.6 have now passed physical XTAL read,
supply-on/off, and DMA memory preparation tests respectively. Version 0.0.7
passed a stopped command-ring configuration/readback experiment with restoration;
see `ring-config.md`. It still never enables PCI bus mastering or uploads firmware.
Group useful observations in one boot; do not request a reboot to recheck an
already validated offline parser.

## Source and distribution

Firmware source commit and hashes are recorded in `firmware/provenance.json`.
The binary has its own Realtek license, including a limited patent grant; this
project's source license does not relicense the binary or expand that grant.
No firmware has been installed or uploaded to the chip by this stage.
Packet encoding and a portable transfer protocol now exist offline; see
`firmware-transport.md` for the unimplemented hardware backend and required
networking work.
