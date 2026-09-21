"""Validate the built diagnostic bundle; never load kernel code."""
import hashlib
import json
import pathlib
import plistlib
import struct
import sys

bundle = pathlib.Path(sys.argv[1])
info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
assert info['CFBundlePackageType'] == 'KEXT'
assert info['CFBundleIdentifier'] == 'local.rtl8852be.probe'
assert info['CFBundleVersion'] == '0.0.1'
assert info['CFBundleExecutable'] == 'RTL8852BEProbe'
personality = info['IOKitPersonalities']['RTL8852BE-R16-Diagnostic']
assert personality['IOPCIMatch'] == '0xb85210ec'
assert personality['IOPCISecondaryMatch'] == '0x54701a3b'
assert personality['IOClass'] == 'RTL8852BEProbe'
binary = bundle / 'Contents/MacOS' / info['CFBundleExecutable']
data = binary.read_bytes()
magic, cpu, subtype, kind, ncmds, sizeofcmds, flags, reserved = struct.unpack_from('<8I', data)
assert magic == 0xfeedfacf, 'Not a 64-bit Mach-O'
assert cpu == 0x01000007, 'Not x86_64'
assert kind == 0xb, 'Not MH_KEXT_BUNDLE'
assert 32 + sizeofcmds <= len(data)
offset = 32
commands = []
symtab = None
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', data, offset)
    assert size >= 8 and offset + size <= 32 + sizeofcmds
    # A kernel bundle must not depend on userspace dynamic libraries or main().
    assert cmd not in (0xc, 0x80000018, 0x8000001f, 0x80000028)
    if cmd == 2:
        symtab = struct.unpack_from('<4I', data, offset + 8)
    commands.append(hex(cmd))
    offset += size
assert offset == 32 + sizeofcmds
assert symtab is not None
symoff, nsyms, stroff, strsize = symtab
assert symoff + nsyms * 16 <= len(data) and stroff + strsize <= len(data)
strings = data[stroff:stroff + strsize]
defined = set()
undefined = set()
for i in range(nsyms):
    index, ntype, section, desc, value = struct.unpack_from('<IBBHQ', data, symoff + i * 16)
    assert index < len(strings)
    end = strings.find(b'\0', index)
    assert end >= index
    name = strings[index:end].decode('utf-8')
    if ntype & 0xe0: continue
    (undefined if (ntype & 0x0e) == 0 else defined).add(name)
assert '_kmod_info' in defined
assert '__start' in defined and '__stop' in defined
assert any('RTL8852BEProbe' in name for name in defined)
report = {'architecture': 'x86_64', 'macho_type': 'MH_KEXT_BUNDLE',
          'bundle_identifier': info['CFBundleIdentifier'],
          'binary_sha256': hashlib.sha256(data).hexdigest(),
          'binary_bytes': len(data), 'load_commands': commands,
          'undefined_kernel_symbols': sorted(undefined),
          'diagnostic_only': True, 'wifi_operational': False,
          'hardware_tested': False, 'signed': False}
path = bundle.parent / 'bundle-verification.json'
path.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
