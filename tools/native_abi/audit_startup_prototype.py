#!/usr/bin/env python3
"""Compile but never link/run the pinned native startup prototype.

Pass the existing generated native overlay; this tool does not mutate it or the
production build. It checks all imports and five actual member-pointer constants.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import struct
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def constants(path, names):
    data = path.read_bytes()
    if struct.unpack_from('<IiiI', data) != (0xfeedfacf, 0x1000007, 3, 1):
        raise ValueError('Expected x86_64 Mach-O object')
    pos, sections, symtab = 32, [], None
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, length = struct.unpack_from('<II', data, pos)
        if length < 8 or pos + length > len(data):
            raise ValueError('Invalid load command')
        if command == 0x19:
            for index in range(struct.unpack_from('<I', data, pos + 64)[0]):
                start = pos + 72 + 80 * index
                sections.append(struct.unpack_from('<QQI', data, start + 32))
        if command == 2:
            symtab = struct.unpack_from('<IIII', data, pos + 8)
        pos += length
    if symtab is None:
        raise ValueError('No symbol table')
    offset, count, stroff, strsize = symtab
    found = {}
    for index in range(count):
        string, kind, section, _, value = struct.unpack_from('<IBBHQ', data, offset + index * 16)
        if string >= strsize:
            raise ValueError('Invalid symbol string')
        name = data[stroff + string:data.index(b'\0', stroff + string, stroff + strsize)].decode()
        if name not in names:
            continue
        if kind & 0xe0 or kind & 0x0e != 0x0e or not section or name in found:
            raise ValueError('Member pointer must be one section definition')
        address, size, file_offset = sections[section - 1]
        at = value - address
        if at < 0 or at + 16 > size:
            raise ValueError('Truncated member pointer')
        found[name] = list(struct.unpack_from('<QQ', data, file_offset + at))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=pathlib.Path)
    parser.add_argument('sdk', type=pathlib.Path)
    parser.add_argument('overlay', type=pathlib.Path, help='existing generated native output directory')
    parser.add_argument('--zig', type=pathlib.Path)
    parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/native-startup-prototype')
    args = parser.parse_args()
    upstream, sdk, overlay, out = (p.resolve() for p in (args.upstream, args.sdk, args.overlay, args.out))
    spec = importlib.util.spec_from_file_location('native_audit', ROOT / 'tools/build_native_sequoia.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    manifest = json.loads((HERE / 'darwin24_4.json').read_text())
    contract = json.loads((HERE / 'startup-contract.json').read_text())
    if manifest['kernel_sha256'] != contract['kernel_sha256']:
        raise ValueError('Mixed target profiles')
    audit.pinned_checkout(upstream, manifest['upstream_commit'])
    audit.pinned_checkout(sdk, manifest['sdk_commit'])
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'startup-audit.json'
    report_path.unlink(missing_ok=True)
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else ['xcrun', 'clang++']
    flags = ['-target', 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0',
             '-mkernel', '-DKERNEL', '-DKERNEL_EXTENSION', '-D__PRIVATE_SPI__', '-DIO80211FAMILY_V2',
             '-D__IO80211_TARGET=140400', '-DR16_NATIVE_ABI_AUDIT_ONLY=1', '-fno-exceptions',
             '-fno-rtti', '-fno-sanitize=all', '-std=gnu++14', '-Wno-deprecated-register',
             '-Wno-inconsistent-missing-override', '-include', str(upstream / 'itlwm/PrivateSPI.pch'),
             '-I' + str(HERE),
             '-I' + str(overlay / 'include'), '-I' + str(upstream / 'include'),
             '-I' + str(upstream / 'itl80211'), '-I' + str(upstream / 'itl80211/openbsd'),
             '-I' + str(sdk / 'Headers')]
    report = dict(kernel_sha256=contract['kernel_sha256'], scope=contract['scope'], results=[])
    for level in ('O0', 'O2'):
        obj = out / ('startup-' + level + '.o')
        obj.unlink(missing_ok=True)
        with (out / ('diagnostics-' + level + '.txt')).open('w') as diagnostics:
            subprocess.run([*compiler, *flags, '-' + level, '-c', str(HERE / 'startup_prototype.cpp'),
                            '-o', str(obj)], stdout=diagnostics, stderr=diagnostics, check=True)
        imported = audit.object_undefined(obj)
        expected = set(contract['undefined_symbols'])
        if imported != expected:
            raise ValueError(dict(level=level, missing=sorted(expected-imported), unexpected=sorted(imported-expected)))
        actual = constants(obj, contract['member_pointer_constants'])
        expected_constants = {name:[entry['vptr_byte_offset']+1, 0]
                              for name,entry in contract['member_pointer_constants'].items()}
        if actual != expected_constants:
            raise ValueError(dict(level=level, actual=actual, expected=expected_constants))
        report['results'].append(dict(optimization=level, object_sha256=sha(obj), imports=sorted(imported),
                                      member_pointer_constants=actual))
    # A compiling declaration drift must fail the emitted-slot check. Keep all
    # mutated material in build output; no shared source/overlay is changed.
    mutated = out / 'wrong-pipe-slot.cpp'
    source_text = (HERE / 'startup_prototype.cpp').read_text()
    needle = 'virtual bool startPipe();'
    if source_text.count(needle) != 1:
        raise ValueError('Negative slot fixture target changed')
    mutated.write_text(source_text.replace(needle, 'virtual void incorrectExtraSlot();\n    '+needle))
    wrong_object = out / 'wrong-pipe-slot.o'
    wrong_object.unlink(missing_ok=True)
    with (out / 'diagnostics-negative-slot.txt').open('w') as diagnostics:
        subprocess.run([*compiler,*flags,'-O2','-c',str(mutated),'-o',str(wrong_object)],
                       stdout=diagnostics,stderr=diagnostics,check=True)
    wrong_constants = constants(wrong_object, contract['member_pointer_constants'])
    if wrong_constants == expected_constants or wrong_constants.get('_r16_startup_pipe_slot') != [2161,0]:
        raise ValueError('Negative slot drift was not detected as expected')
    report['negative_slot_drift_rejected'] = True
    inputs = [HERE / 'startup_prototype.cpp', HERE / 'support_options.hpp', HERE / 'startup-contract.json',
              pathlib.Path(__file__), HERE / 'darwin24_4.json', ROOT / 'tools/build_native_sequoia.py']
    report['input_sha256'] = {str(p.relative_to(ROOT)):sha(p) for p in inputs}
    report['overlay_sha256'] = {str(p.relative_to(overlay)):sha(p) for p in sorted((overlay/'include').rglob('*.h'))}
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('Startup prototype: O0/O2 imports and five member slots match. No linking or execution.')


if __name__ == '__main__':
    main()
