# Native Wi-Fi dependency boundary, exact recovery KC

`audit_native_export_dependencies.py` read the x86_64 macOS 15.4.1 recovery
`BootKernelExtensions.kc`, SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
It resolves the 41 distinct referenced names (direct imports and permitted
compiler helpers) in the currently checked startup/registration/Infra/WCL/packet
*offline contracts* to the KC's embedded
component nlists. Every name has one defined `N_EXT`, non-`N_PEXT` candidate
inside its component's `LC_DYSYMTAB` extdef range;
there are no absent or ambiguous candidates in this limited inventory.

| KC owner | Contract symbols | In current `Info-Network.plist`? |
| --- | ---: | --- |
| `com.apple.iokit.IOSkywalkFamily` | 21 | No |
| `com.apple.driver.corecapture` | 7 | No |
| `com.apple.iokit.IO80211Family` | 6 | No |
| `com.apple.kernel` | 7 | Supplied by kernel/KPI context |

Examples are `IOSkywalkPacketBufferPool::allocatePacket` (Skywalk),
`CCPipe::withOwnerNameCapacity` (CoreCapture), and
`IO80211PostOffice::sendMail` (IO80211). The existing kext declares only the
BSD/IOKit/libkern/Mach KPIs, PCI and IONetworking families. Its personality is
still `R16RTL8852BE` on PCI ID `10ec:b852`, an Ethernet-style network service.
`tools/build_network_stack.py` compiles `src/network/*.cpp`, not the isolated
`tools/native_abi/*.cpp` probes, and its final
`ld -kext -undefined dynamic_lookup` permits unresolved private imports. Thus the missing three
libraries do not explain a *current* load failure; they block any claim that
linking the probes into the present bundle would provide a loadable native
service. [Apple's `OSBundleLibraries` documentation](https://developer.apple.com/documentation/bundleresources/information-property-list/osbundlelibraries)
requires dependencies on
drivers supplying symbols used at startup and says unresolved libraries can
prevent a driver loading. This audit has **not** established compatible bundle
versions, private export-set eligibility, runtime profile identity, actual
link/loading or native menu discovery. A KC nlist name is only a candidate.

The local `BaseSystemKernelExtensions.kc` is a different file and lacks these
three embedded component identifiers. That alone does not prove the running
Recovery system lacks them: this audit covers the separately captured Boot KC
and does not reconstruct the complete runtime collection set.

Reproduce the read-only check with:

```sh
python tools/native_abi/audit_native_export_dependencies.py \
  --kc /path/to/BootKernelExtensions.kc \
  --components /path/to/components.json \
  --out build/native-export-dependency-audit.json
```

## Distinct Ventura/OCLP fallback

The [AirportItlwm Sequoia workaround](https://github.com/5T33Z0/OCLP4Hackintosh/blob/main/Enable_Features/AirportItllwm_Sequoia.md)
blocks the system's new IOSkywalkFamily, injects older IOSkywalk plus
IO80211FamilyLegacy and **Ventura** AirportItlwm in order, applies an OCLP-Mod
root patch to an installed macOS volume, and reboots. It is not a way to make
the above Darwin 24.4 IO80211/Skywalk ABI probes link: replacing the family
changes the ABI target. Its AirportItlwm driver is for **Intel** devices; the
pinned local `AirportItlwm/Info.plist` matches `8086` PCI IDs and has no
`10ec:b852` RTL8852BE personality or Realtek device implementation. A custom
RTL8852BE legacy frontend would still have to be ported and checked against
the exact injected Ventura family binaries. The 24.4 KC symbol/slot proofs
cannot be carried over to those binaries.

The machine currently has macOS 15 Recovery but no verified booted installed
macOS volume. The fallback's root-patch and native-menu test stage therefore
cannot yet be claimed. [OCLP's update guide](https://github.com/dortania/OpenCore-Legacy-Patcher/blob/main/docs/UPDATE.md)
places root patching after OS installation and reapplication after updates;
[its FAQ](https://github.com/dortania/OpenCore-Legacy-Patcher/blob/main/docs/FAQ.md)
explains that root patches alter the sealed system volume. Nothing in this
fallback is currently installed or tested here. Preserve the working WPA2
Ethernet-style kext and Windows boot until either frontend has an actual
loadable implementation and a separate rollback plan.
