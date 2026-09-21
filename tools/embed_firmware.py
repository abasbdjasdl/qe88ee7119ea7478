"""Embed the pinned, unchanged container and carry its license in Info.plist."""
import hashlib
from pathlib import Path
import plistlib

root = Path(__file__).resolve().parent.parent
source = root / 'firmware/rtw8852b_fw-1.bin'
blob = source.read_bytes()
digest = hashlib.sha256(blob).hexdigest()
assert digest == '5b68415e3bfe72715d63a70703d4471b04b5475b8ff69cfdc8cdf48233cd5d3a'
out = root / 'build/generated'
out.mkdir(parents=True, exist_ok=True)
rows = [','.join(f'0x{b:02x}' for b in blob[i:i+32]) for i in range(0, len(blob), 32)]
(out / 'EmbeddedFirmware.hpp').write_text(
    '// Unmodified Realtek firmware. License and source are embedded in Info.plist.\n'
    '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n'
    'namespace rtl8852be { namespace image {\nstatic const uint8_t bytes[]={\n'
    + ',\n'.join(rows) + '\n};\nconstexpr size_t length=sizeof(bytes);\n} }\n')
info = plistlib.loads((root / 'Info.plist').read_bytes())
info['FirmwareLicense'] = (root / 'firmware/LICENCE.rtlwifi_firmware.txt').read_text()
info['FirmwareSource'] = 'https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/2b8daaf611fbade74f26a5b58ec1defe6a02f5e0/rtw89/rtw8852b_fw-1.bin'
info['FirmwareSHA256'] = digest
info['FirmwareBytes'] = len(blob)
info['FirmwarePurpose'] = 'In-memory packet preparation only; not uploaded by this diagnostic.'
(out / 'Info.plist').write_bytes(plistlib.dumps(info))
print(f'Embedded unchanged firmware: {len(blob)} bytes, SHA256 {digest}; license included.')
