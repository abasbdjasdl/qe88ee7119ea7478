#!/usr/bin/env python3
"""Read-only, exact-KC symbol-owner and kext dependency inventory.

This deliberately does not link a kext or infer that an N_EXT symbol belongs to
an export set available to a third-party extension.
"""
import argparse
import hashlib
import json
import plistlib
import struct
from collections import defaultdict
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def required_symbols():
    result = defaultdict(set)
    sources = (
        ('support-symbols.json', lambda x: (v['symbol'] for v in x['helpers'])),
        ('registration-symbols.json', lambda x: (v['symbol'] for v in x['helpers'])),
        ('startup-contract.json', lambda x: x['undefined_symbols']),
        ('infra-frontend-contract.json', lambda x: x['runtime_symbol_evidence']),
        ('native-wcl-event-sender-contract.json', lambda x: x['symbols']),
        ('native-packet-port-contract.json', lambda x: x['symbols']),
    )
    manifest = json.loads((HERE / 'darwin24_4.json').read_text())
    for filename, symbols in sources:
        contract = json.loads((HERE / filename).read_text())
        if contract['kernel_sha256'] != manifest['kernel_sha256']:
            raise ValueError('Mixed target profiles: ' + filename)
        for symbol in symbols(contract):
            result[symbol].add(filename)
    return manifest['kernel_sha256'], result


def kc_nlists(kc, component_map):
    data = kc.read_bytes()
    mapping = json.loads(component_map.read_text())
    entries = next((entry['entries'] for entry in mapping
                    if entry['file'] == kc.name and
                    entry['sha256'] == hashlib.sha256(data).hexdigest()), None)
    if entries is None:
        raise ValueError('Exact KC missing from matching component map')
    records = defaultdict(list)
    for entry in entries:
        offset = entry['offset']
        if struct.unpack_from('<I', data, offset)[0] != 0xfeedfacf:
            raise ValueError('Invalid embedded Mach-O: ' + entry['identifier'])
        count = struct.unpack_from('<I', data, offset + 16)[0]
        cursor = offset + 32
        symtab = None
        extdef = None
        for _ in range(count):
            command, size = struct.unpack_from('<II', data, cursor)
            if size < 8 or cursor + size > len(data):
                raise ValueError('Invalid load command: ' + entry['identifier'])
            if command == 2:  # LC_SYMTAB; each embedded image has its own nlist range.
                symtab = struct.unpack_from('<IIII', data, cursor + 8)
            if command == 0xb:  # LC_DYSYMTAB extdef range.
                extdef = struct.unpack_from('<II', data, cursor + 16)
            cursor += size
        if symtab is None or extdef is None:
            continue
        symoff, nsyms, stroff, strsize = symtab
        extstart, extcount = extdef
        if (symoff + 16 * nsyms > len(data) or stroff + strsize > len(data)
                or extstart + extcount > nsyms):
            raise ValueError('Invalid nlist bounds: ' + entry['identifier'])
        for index in range(nsyms):
            nameoff, kind, section, desc, address = struct.unpack_from(
                '<IBBHQ', data, symoff + 16 * index)
            if nameoff >= strsize or not address:
                continue
            start = stroff + nameoff
            end = data.find(b'\0', start, stroff + strsize)
            if end < start:
                raise ValueError('Invalid nlist string: ' + entry['identifier'])
            name = data[start:end].decode('utf8', errors='replace')
            records[name].append(dict(component=entry['identifier'],
                                      address=f'{address:#x}', n_type=kind,
                                      n_sect=section, n_desc=desc,
                                      defined_external=bool(kind & 1 and
                                          not kind & 0x10 and
                                          kind & 0x0e == 0x0e and section and
                                          extstart <= index < extstart + extcount)))
    return entries, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kc', required=True, type=Path)
    parser.add_argument('--components', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    kc = args.kc.resolve()
    expected_sha, required = required_symbols()
    actual_sha = hashlib.sha256(kc.read_bytes()).hexdigest()
    if actual_sha != expected_sha:
        raise ValueError('KC hash differs from all native contracts')
    entries, nlists = kc_nlists(kc, args.components or kc.parent / 'components.json')
    plist = plistlib.loads((ROOT / 'Info-Network.plist').read_bytes())
    declared = set(plist['OSBundleLibraries'])
    rows = []
    for name in sorted(required):
        candidates = [item for item in nlists.get(name, ()) if item['defined_external']]
        rows.append(dict(symbol=name, contracts=sorted(required[name]),
                         candidates=candidates,
                         candidate_components=sorted({item['component'] for item in candidates})))
    owners = sorted({component for row in rows
                     for component in row['candidate_components']
                     if component != 'com.apple.kernel'})
    report = dict(scope='Pinned KC nlist/dependency inventory only; not a kext link, '
                        'runtime export-set entitlement or native Wi-Fi proof',
                  kernel_sha256=actual_sha, target_components=len(entries),
                  current_bundle=plist['CFBundleIdentifier'],
                  current_libraries=plist['OSBundleLibraries'],
                  candidate_owner_components=owners,
                  owner_components_not_declared=sorted(set(owners) - declared),
                  symbol_count=len(rows),
                  no_defined_external=[row['symbol'] for row in rows
                                       if not row['candidates']],
                  multiple_defined_external=[row['symbol'] for row in rows
                                             if len(row['candidates']) > 1],
                  ambiguous_owners=[row['symbol'] for row in rows
                                    if len(row['candidate_components']) > 1],
                  symbols=rows)
    if args.out:
        out = args.out.resolve()
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in (
        'kernel_sha256', 'target_components', 'symbol_count',
        'candidate_owner_components', 'owner_components_not_declared',
        'no_defined_external', 'multiple_defined_external',
        'ambiguous_owners')}, indent=2))


if __name__ == '__main__':
    main()
