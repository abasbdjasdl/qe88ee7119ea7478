# RTL8852B DAV physical eFuse reader

`DavEfuseReader.hpp` implements the physical DAV read path and the 96-byte to
16-byte logical-bank decode. `MacDavEfuseIo.cpp` supplies the restricted PCI BAR2
MMIO backend and explicitly instantiates the reader for a macOS kernel build.
This is an initialization component, not evidence of a functioning network card.

## Authoritative source

Adapted under the Realtek BSD option from rtw89 commit
`d1fced1b8a741dc9f92b47c69489c24385945f6e`:

| Source | SHA-256 | Relevant behavior |
| --- | --- | --- |
| `efuse.c` | `293e3b7ff7884161e86685f74dd0d32c3ae5a86353a807a761d439eb9770fdde` | `rtw89_dump_physical_efuse_map_dav`, logical AX decoding |
| `mac.c` | `eff3ff31136b6accf7c6bcf3aaa6cb32e3a579bf5f117ae52f9782835094b728` | AX XTAL SI read/write encoding, 50 us/50 ms mailbox polling |
| `mac.h` | `e5b1a567638b69f0f0e469c048f0be5608f5e72c3e5aa31550b44a75506da9bd` | DAV XTAL registers and bit masks |
| `reg.h` | `ed7a3500553a068ab7e56487e4d667c5f3cf5c32e67820742eede9640de796f2` | `R_AX_WLAN_XTAL_SI_CTRL` and command fields |
| `rtw8852b.c` | `1ac54aff8acd780be4d525893b4c0f6a39370787c45b8625a990a45a44e31a16` | Physical DAV 96, logical DAV 16, security prefix/suffix 4 |

Each byte uses SI control `0x63 = 0x40`, low address `0x62`, the high-address
mask in `0x63`, then clears the mode mask to start a read. Poll `0x63` ready bit
5 before reading `0x7a`. The BAR mailbox is `0x270`; SI command bit 31 must clear
before another request. The DAV ready poll has a 10 ms deadline, and each SI
command has the upstream 50 ms bound. Frozen-clock iteration limits and a
5-second total-operation deadline are additional defensive bounds. The 10 ms DAV
deadline is propagated into nested SI polling; SI's 50 ms deadline begins before
its command write. Clock checks run before and after raw reads, so a late ready
value is rejected even if one MMIO read itself consumed the entire deadline.

The pinned DAV path does **not** toggle DDV power-cut or isolation bits. This
implementation therefore issues no DDV power writes and does not invent a DAV
rail sequence. The controller must provide the powered, accessible device and
serialize against power transitions and all other XTAL SI users.

## Integration contract

1. Hold the PCI provider and its correct BAR2 mapping for the entire synchronous
   reader call. Run under the controller's exclusive hardware/workloop ownership;
   raw adapter objects do not retain the provider or implement a lock.
2. Call `readLogical(physical, 96, logical, 16)`, preserving both arrays if a raw
   calibration provenance report is needed. Only `status == ok && logicalValid`
   authorizes use of the logical result. Append that logical bank after the
   2048-byte DDV logical bank when constructing the upstream 2064-byte map.
   The existing board parser currently consumes DDV board fields; DAV bytes do
   not replace the DDV MAC address, board type, gain or thermal fields.
3. The existing `decodeEfuse` parser receives the actual 96/16 bank bounds and a
   security-byte count of 4. This deliberately avoids the pinned generic decoder's
   reuse of DDV bounds for DAV. Duplicate words supersede earlier ones; an empty
   bank remains all `0xff`. Empty-bank decode success does not establish valid
   board calibration. Malformed decode leaves logical output untouched.
4. `bytesRead` is diagnostic progress; `validBytes` is zero after any physical
   read/cleanup failure. Already-read bytes are erased on failure. Logical output
   remains untouched after either read or decode failure and must not be consumed
   without checking `logicalValid`.
5. Cancellation leaves raw MMIO usable for draining. Cleanup does not restore an
   arbitrary OTP mode or send a reset; it waits for the existing SI operation and
   observes DAV read completion/prepared state. Cleanup has independent iteration
   and elapsed/cumulative-delay bounds (20,000 SI reads / 100 ms each for elapsed
   time and requested delay). Cleanup's separate clock starts after the normal
   operation ends; an expired read deadline does not prevent cleanup. A stuck
   engine or inaccessible device returns `cleanupFailed`, preserves `primary`,
   marks `requiresReset`, and poisons the reader against reuse. The controller must
   retain ownership and use its verified device recovery path before creating a
   new reader. It must not claim that failed cleanup restored power or engine state.
6. Add `MacDavEfuseIo.cpp` to the native components archive and add the test below
   to CI. Actual controller startup integration and hardware validation remain
   separate work; no EFI, boot selection or installed driver was modified here.

The native backend accepts only reads from mailbox `0x270` / data byte `0x271`
and exact DAV read protocol commands. It rejects other ports, addresses outside
0..95, SI modes 2/3, control values that would select OTP programming, a busy
mailbox, disabled PCI memory decoding and a removed (`0xffff`) PCI function.

## Verification

Host model (actual native adapter source also included against the existing
explicit IOKit mock):

```text
zig c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -I tests/network_rfk_fakes tests/network_dav_efuse_test.cpp -o build/network_dav_efuse_test.exe
build/network_dav_efuse_test.exe
```

Tests validate the full 96-byte transaction trace; all 454 raw I/O fault positions
on a 16-byte request, both rejected and already-applied failed writes; all 161
delay failures; cancellation; initial busy state; device removal; backwards and
frozen clocks; SI and DAV timeouts, including a 12 ms nested SI poll returning
DAV ready and a single 60 ms MMIO read returning SI ready; the whole-bank 5 s
deadline; cleanup poisoning; repeated reads; duplicate,
blank and malformed logical banks; output non-publication on failure; and native
BAR/PCI/command-whitelist checks. This is a software model, not hardware evidence.

Kernel compile:

```text
zig c++ -target x86_64-macos-none -mkernel -DKERNEL -DKERNEL_EXTENSION -fno-stack-protector -mno-red-zone -fno-exceptions -fno-rtti -std=gnu++14 -I ../MacKernelSDK/Headers -c src/network/MacDavEfuseIo.cpp -o build/MacDavEfuseIo.o
```
