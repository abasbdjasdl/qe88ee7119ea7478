#!/usr/bin/env python3
"""Compile an OFFLINE Darwin 24.4 declaration probe and check every vtable slot.

This produces an object, not a kext. No runtime registration or return-ABI claim
follows from a match: Itanium method names do not encode ordinary return types.
"""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'tools/native_abi/darwin24_4.json'
SCOPE = ('Offline emitted symbol/slot identity and compile-time native object sizes only; '
         'not return/calling ABI, kernel linkage, lifecycle, WCL or native menu validation')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pinned_checkout(path, revision):
    actual = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != revision or subprocess.check_output(['git', '-C', str(path), 'diff', 'HEAD', '--'], text=True):
        raise ValueError('Reference checkout differs from the pinned revision: ' + str(path))


def generate_overlay(manifest, upstream, sdk, out):
    airport = out / 'include/Airport'
    shutil.copytree(upstream / 'include/Airport', airport, dirs_exist_ok=True)
    sdktext = (sdk / 'Headers/IOKit/network/IONetworkController.h').read_text()
    for index, signature in [(6, 'allocatePacketNoWait(unsigned int)'),
                             (7, 'setHardwareAssists(unsigned int, unsigned int)')]:
        pattern = r'OSMetaClassDeclareReservedUnused\( IONetworkController,  ' + str(index) + r'\);'
        sdktext, count = re.subn(pattern, 'virtual void ' + signature +
            ' __attribute__((unavailable("Return ABI not verified; slot audit only")));', sdktext)
        if count != 1:
            raise ValueError('Pinned SDK reserved slot not found')
    sdkout = out / 'include/IOKit/network/IONetworkController.h'
    sdkout.parent.mkdir(parents=True, exist_ok=True)
    sdkout.write_text('#ifndef R16_NATIVE_ABI_AUDIT_ONLY\n#error "Offline audit only"\n#endif\n' + sdktext)
    for name, data in manifest['classes'].items():
        filename = 'IO80211ControllerV2.h' if name == 'IO80211Controller' else name + '.h'
        original = (upstream / 'include/Airport' / filename).read_text(encoding='utf8')
        prefix = original[:original.index('class ' + name + ' :')]
        prefix = prefix.replace('typedef UInt64 IO80211FlowQueueHash;', 'struct IO80211FlowQueueHash;')
        prefix = prefix.replace('typedef UInt if_link_status;', 'struct if_link_status;')
        prefix = prefix.replace('typedef UInt apple80211_offload_tcpka_enable_t;', 'struct apple80211_offload_tcpka_enable_t;')
        extras = ['#ifndef R16_NATIVE_ABI_AUDIT_ONLY', '#error "Experimental declarations require the offline audit"', '#endif',
                  'class IO80211FlowQueue; class IO80211PeerManager; class IO80211Controller; class CCLogStream;',
                  'class IO80211IORecursiveLock; class IO80211PostOffice;',
                  'struct bss_blacklist; struct appl80211_sleep_on_inactivity_config;',
                  'struct apple80211_data_path_interface_stats; struct apple80211_data_path_peer_stats;',
                  'struct apple80211_latency_all_ac; struct apple80211_platform_config;']
        if name == 'IO80211InfraProtocol':
            known = set(re.findall(r'\b(?:struct|class)\s+(\w+)', prefix))
            types = set(re.findall(r'\bapple80211\w+(?=\s*\*)', ' '.join(x['signature'] for x in data['methods'])))
            extras += ['struct ' + t + ';' for t in sorted(types - known)]
        lines = [prefix, *extras, 'class ' + name + ' : public ' + data['parent'] + ' {',
                 '    OSDeclareAbstractStructors(' + name + ')', 'public:']
        if name in ('IOSkywalkNetworkInterface', 'IOSkywalkEthernetInterface'):
            lines += ['    struct RegistrationInfo { uint8_t bytes[304]; } __attribute__((packed));']
        if name == 'IOSkywalkNetworkInterface':
            lines += ['    struct IOSkywalkTSOOptions;']
        for method in data['methods']:
            signature = method['signature'].replace('__va_list_tag*', 'va_list')
            annotation = (' __attribute__((unavailable("Return ABI not verified; declaration for slot audit only")))'
                          if method['return_evidence'] == 'unavailable-unknown-return' else '')
            lines += ['    // raw slot ' + str(method['slot']) + '; ' + method['return_evidence'],
                      '    virtual ' + method['return'] + ' ' + signature + annotation + (' = 0;' if method['pure'] else ';')]
        # Keep only explicitly identified nonvirtual lifecycle declarations.
        if name == 'IO80211SkywalkInterface':
            lines += ['    OSString* setInterfaceRole(unsigned int);', '    void* setInterfaceId(unsigned int);', '    int getInterfaceRole();']
        if name == 'IOSkywalkEthernetInterface':
            lines += ['    bool initRegistrationInfo(RegistrationInfo*, unsigned int, unsigned long);',
                      '    IOReturn registerEthernetInterface(RegistrationInfo const*, IOSkywalkPacketQueue**, unsigned int, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*, unsigned int);']
        if name == 'IO80211InfraInterface':
            lines += ['    IOReturn registerInfraEthernetInterface(IOSkywalkEthernetInterface::RegistrationInfo const*, IOSkywalkPacketQueue**, unsigned int, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*);']
        if name != 'IO80211InfraProtocol':
            lines += ['private:', f'    uint8_t reserved_[{data["size"]} - sizeof({data["parent"]})];']
        lines += ['};', f'static_assert(sizeof({name}) == {data["size"]}, "Target native object size mismatch");', '#endif']
        if name == 'IO80211Controller':
            lines += ['#endif']
        (airport / filename).write_text('\n'.join(lines) + '\n', encoding='utf8')


def probe_source(manifest):
    lines = ['#include <Airport/Apple80211.h>', '// Offline objects: never link or instantiate these probe classes in a kext.']
    for name in manifest['classes']:
        pure, chain, current = {}, [], name
        while current in manifest['classes']:
            chain.insert(0, current)
            current = manifest['classes'][current]['parent']
        for ancestor in chain:
            for method in manifest['classes'][ancestor]['methods']:
                if method['pure']:
                    pure[method['signature']] = method
                else:
                    pure.pop(method['signature'], None)
        probe = 'R16Audit_' + name
        lines += ['class ' + probe + ' : public ' + name + ' { public:',
                  f'    {probe}() : {name}(nullptr) {{}}', '    ~' + probe + '() override;']
        for method in pure.values():
            signature, result = method['signature'].replace('__va_list_tag*', 'va_list'), method['return']
            value = '' if result == 'void' else (' nullptr' if '*' in result else (' false' if result == 'bool' else ' kIOReturnUnsupported'))
            lines += ['    ' + result + ' ' + signature + ' override { return' + value + '; }']
        lines += ['};', probe + '::~' + probe + '() {}',
                  f'static_assert(!__is_abstract({probe}), "Unimplemented pure callback");',
                  f'extern "C" {name}* R16Make_{name}() {{ return new {probe}; }}']
    return '\n'.join(lines) + '\n'


def object_vtables(path):
    data = path.read_bytes()
    if struct.unpack_from('<IiiI', data) != (0xfeedfacf, 0x1000007, 3, 1):
        raise ValueError('Expected x86_64 MH_OBJECT')
    sections, pos, symtab = [], 32, None
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        cmd, size = struct.unpack_from('<II', data, pos)
        if cmd == 0x19:
            for index in range(struct.unpack_from('<I', data, pos + 64)[0]):
                at = pos + 72 + 80 * index
                addr, length, off = struct.unpack_from('<QQI', data, at + 32)
                reloff, nrel = struct.unpack_from('<II', data, at + 56)
                sections.append(dict(addr=addr, size=length, offset=off, reloff=reloff, nrel=nrel))
        elif cmd == 2:
            symtab = struct.unpack_from('<IIII', data, pos + 8)
        if size < 8:
            raise ValueError('Invalid load command')
        pos += size
    if symtab is None:
        raise ValueError('Missing symbol table')
    so, count, stroff, strsize = symtab
    symbols = []
    for index in range(count):
        string, kind, section, _, address = struct.unpack_from('<IBBHQ', data, so + 16 * index)
        if string >= strsize:
            raise ValueError('Invalid symbol string')
        name = data[stroff + string:data.index(b'\0', stroff + string, stroff + strsize)].decode()
        symbols.append(dict(name=name, section=section, address=address, kind=kind))
    tables = {}
    for symbol in symbols:
        if not symbol['name'].startswith('__ZTV') or not symbol['section']:
            continue
        section = sections[symbol['section'] - 1]
        start = symbol['address'] - section['addr']
        relocs = {}
        for index in range(section['nrel']):
            offset, info = struct.unpack_from('<II', data, section['reloff'] + 8 * index)
            relocs[offset] = (symbols[info & 0xffffff]['name'] if (info >> 27) & 1 else None, info)
        end = min([s['address'] - section['addr'] for s in symbols
                   if s['section'] == symbol['section'] and s['address'] > symbol['address']] + [section['size']])
        entries = []
        for offset in range(start, end, 8):
            value = struct.unpack_from('<Q', data, section['offset'] + offset)[0]
            if value != 0:
                raise ValueError('Unexpected local/addend vtable pointer')
            if offset in relocs:
                name, info = relocs[offset]
                if info >> 28 or (info >> 24) & 1 or (info >> 25) & 3 != 3 or not (info >> 27) & 1:
                    raise ValueError('Unsupported vtable relocation')
                entries.append(name)
            else:
                entries.append(None)
        tables[symbol['name']] = entries
    return tables


def method_identity(symbol):
    if symbol is None:
        return None
    match = re.match(r'(__ZNK?)(\d+)', symbol)
    if not match:
        return symbol
    # Only the implementation's owning class differs for probe destructors and
    # pure overrides. Retain const and ALL member/argument/substitution encoding.
    return match[1] + symbol[match.end() + int(match[2]):]


def audit_slots(manifest, tables):
    report, errors = {}, []
    for name, spec in manifest['classes'].items():
        probe = 'R16Audit_' + name
        actual = tables.get('__ZTV' + str(len(probe)) + probe, [])
        expected = spec['table']
        if len(actual) != len(expected):
            errors.append([name, 'count', len(actual), len(expected)])
        for index, (compiled, target) in enumerate(zip(actual, expected)):
            pure = target == '___cxa_pure_virtual'
            if pure:
                target = spec['pure_implementations'][str(index)]
            if pure or index in (2, 3):
                if not compiled or not re.match(r'__ZNK?' + str(len(probe)) + re.escape(probe), compiled):
                    errors.append([name, index, 'Unexpected implementation owner', compiled])
            same = (method_identity(compiled) == method_identity(target)
                    if pure or index in (2, 3) else compiled == target)
            if not same:
                errors.append([name, index, compiled, target])
        report[name] = {'slots': len(actual), 'mismatches': sum(e[0] == name for e in errors)}
    return dict(scope=SCOPE, kernel_sha256=manifest['kernel_sha256'], classes=report, mismatches=errors,
                excluded=['ordinary return ABI', 'nonvirtual registration helpers',
                          'RegistrationInfo fields and alignment', 'symbol-set exports and kernel linking',
                          'runtime construction and teardown', 'native menu or authentication functionality'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=pathlib.Path)
    parser.add_argument('sdk', type=pathlib.Path)
    parser.add_argument('--zig', type=pathlib.Path, help='Windows cross compiler; default uses xcrun clang++')
    parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/native-sequoia-contract')
    args = parser.parse_args()
    upstream, sdk, out = args.upstream.resolve(), args.sdk.resolve(), args.out.resolve()
    manifest = json.loads(MANIFEST.read_text())
    out.mkdir(parents=True, exist_ok=True)
    # Invalidate earlier success BEFORE any failure-prone work. Never audit an
    # old object after a failed build of new declarations.
    for name in ('probe.o', 'slot-audit.json', 'slot-failure.json', 'metadata.json'):
        (out / name).unlink(missing_ok=True)
    pinned_checkout(upstream, manifest['upstream_commit'])
    pinned_checkout(sdk, manifest['sdk_commit'])
    generate_overlay(manifest, upstream, sdk, out)
    (out / 'probe.cpp').write_text(probe_source(manifest))
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else ['xcrun', 'clang++']
    target = 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0'
    flags = ['-target', target, '-mkernel', '-DKERNEL', '-DKERNEL_EXTENSION', '-D__PRIVATE_SPI__',
             '-DIO80211FAMILY_V2', '-D__IO80211_TARGET=140400', '-DR16_NATIVE_ABI_AUDIT_ONLY=1',
             '-fno-exceptions', '-fno-rtti', '-std=gnu++14', '-Wno-deprecated-register',
             '-Wno-inconsistent-missing-override', '-include', str(upstream / 'itlwm/PrivateSPI.pch'),
             '-I' + str(out / 'include'), '-I' + str(upstream / 'include'),
             '-I' + str(upstream / 'itl80211'), '-I' + str(upstream / 'itl80211/openbsd'), '-I' + str(sdk / 'Headers')]
    with (out / 'compiler-vtables.txt').open('w') as stdout, (out / 'compiler-diagnostics.txt').open('w') as stderr:
        subprocess.run([*compiler, *flags, '-Xclang', '-fdump-vtable-layouts', '-c',
                        str(out / 'probe.cpp'), '-o', str(out / 'probe.o')], stdout=stdout, stderr=stderr, check=True)
    tables = object_vtables(out / 'probe.o')
    (out / 'object-vtables.json').write_text(json.dumps(tables, indent=2))
    report = audit_slots(manifest, tables)
    if report['mismatches']:
        (out / 'slot-failure.json').write_text(json.dumps(report, indent=2))
        raise RuntimeError('Native slot mismatches: ' + repr(report['mismatches'][:10]))
    inputs = [ROOT / 'tools/build_native_sequoia.py', MANIFEST]
    generated = [out / 'probe.cpp', out / 'probe.o', *sorted((out / 'include').rglob('*.h'))]
    metadata = dict(scope=SCOPE, manifest_sha256=digest(MANIFEST), kernel_sha256=manifest['kernel_sha256'],
                    upstream=manifest['upstream_commit'], sdk=manifest['sdk_commit'],
                    port_revision=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
                    port_inputs_sha256={p.relative_to(ROOT).as_posix(): digest(p) for p in inputs},
                    generated_sha256={p.relative_to(out).as_posix(): digest(p) for p in generated})
    (out / 'metadata.json').write_text(json.dumps(metadata, indent=2))
    (out / 'slot-audit.json').write_text(json.dumps(report, indent=2))
    print('Exact target vtable audit passed:', {c: d['slots'] for c, d in report['classes'].items()})


if __name__ == '__main__':
    main()
