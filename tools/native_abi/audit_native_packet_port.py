#!/usr/bin/env python3
"""Audit offline Skywalk packet-Port callsites; never link or run a kext."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = HERE / 'native_packet_port_probe.cpp'
CONTRACT = HERE / 'native-packet-port-contract.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def object_imports(path):
    spec = importlib.util.spec_from_file_location('native_build_audit', ROOT/'tools/build_native_sequoia.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.object_undefined(path)


def inspect_kernel(kc, components, contract):
    data = kc.read_bytes()
    if hashlib.sha256(data).hexdigest() != contract['kernel_sha256']:
        raise ValueError('Target KC hash differs; packet ABI is not approved')
    entries = json.loads(components.read_text())[1]['entries']
    symbols, segments = {}, []
    for component in entries:
        offset = component['offset']
        cursor = offset + 32
        for _ in range(struct.unpack_from('<I', data, offset + 16)[0]):
            command, size = struct.unpack_from('<II', data, cursor)
            if size < 8 or cursor + size > len(data):
                raise ValueError('Invalid KC load command')
            if command == 0x19:
                low, _, file_offset, file_size = struct.unpack_from('<QQQQ', data, cursor + 24)
                if file_size:
                    segments.append((low, low + file_size, file_offset))
            if command == 2:
                symoff, count, stroff, strsize = struct.unpack_from('<IIII', data, cursor + 8)
                for index in range(count):
                    nameindex, kind, section, desc, value = struct.unpack_from(
                        '<IBBHQ', data, symoff + index * 16)
                    if not value or nameindex >= strsize:
                        continue
                    start = stroff + nameindex
                    end = data.find(b'\0', start, stroff + strsize)
                    if end < start:
                        raise ValueError('Invalid KC string table')
                    name = data[start:end].decode(errors='replace')
                    symbols.setdefault(name, []).append((value, kind, section, desc))
            cursor += size

    def location(address):
        matches = {file_offset + address - low for low, high, file_offset in segments
                   if low <= address < high}
        if len(matches) != 1:
            raise ValueError(f'KC address has no unique file mapping: {address:x}')
        return matches.pop()

    def pointer(address):
        raw = struct.unpack_from('<Q', data, location(address))[0]
        if raw >> 48 == 0xffff:
            return raw
        if (raw >> 30) & 3:
            raise ValueError(f'Unsupported KC fixup at {address:x}')
        return 0xffffff8000100000 + (raw & 0x3fffffff)

    for name, address_text in contract['symbols'].items():
        address = int(address_text, 16)
        records = symbols.get(name, [])
        if len(records) != 1 or records[0][0] != address:
            raise ValueError(f'Missing or moved packet API: {name}')
        _, kind, section, _ = records[0]
        if kind & 1 == 0 or kind & 0x10 or kind & 0x0e != 0x0e or not section:
            raise ValueError(f'Packet API is not a defined public Mach-O symbol: {name}')
    seen = set()
    for slot in contract['vtable_slots']:
        name, table_address, offset = slot['vtable'], int(slot['address'], 16), slot['offset']
        key = (name, offset)
        if key in seen or offset < 0 or offset % 8:
            raise ValueError('Duplicate or unaligned packet slot')
        seen.add(key)
        records = symbols.get(name, [])
        if len(records) != 1 or records[0][0] != table_address:
            raise ValueError(f'Missing or moved packet vtable: {name}')
        target = int(contract['symbols'][slot['symbol']], 16)
        if pointer(table_address + 16 + offset) != target:
            raise ValueError(f'Packet virtual slot drift: {name}+{offset:#x}')
    return dict(kernel_sha256=contract['kernel_sha256'], public_symbols=len(contract['symbols']),
                checked_slots=len(seen), source='supplied exact KC and component map')


def compile_probe(compiler, flags, source, output, log):
    with log.open('w') as diagnostics:
        process = subprocess.run([*compiler, *flags, '-c', str(source), '-o', str(output)],
                                 stdout=diagnostics, stderr=diagnostics)
    if process.returncode:
        raise RuntimeError(f'Mach-O compile failed; see {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path, help='Zig compiler when not on macOS')
    parser.add_argument('--kc', type=Path, help='optional exact BootKernelExtensions.kc')
    parser.add_argument('--components', type=Path, help='components.json beside --kc by default')
    parser.add_argument('--out', type=Path, default=ROOT/'build/native-packet-port')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report_path = out/'packet-port-audit.json'
    report_path.unlink(missing_ok=True)
    contract = json.loads(CONTRACT.read_text())
    manifest = json.loads((HERE/'darwin24_4.json').read_text())
    if contract['kernel_sha256'] != manifest['kernel_sha256']:
        raise ValueError('Mixed native target profiles')
    expected = set(contract['symbols'])
    if len(expected) != 15 or len(contract['vtable_slots']) != 10:
        raise ValueError('Packet callsite/slot inventory changed without review')
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else ['xcrun', 'clang++']
    target = 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0'
    common = ['-target', target, '-std=c++17', '-Wall', '-Wextra', '-Werror',
              '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector',
              '-fno-sanitize=all', '-nostdinc++', '-DR16_NATIVE_ABI_AUDIT_ONLY=1']
    report = dict(scope=contract['scope'], kernel_sha256=contract['kernel_sha256'],
                  results=[], excluded=['loaded class hierarchy', 'Skywalk workloop ownership',
                                        'pool/queue startup and teardown', 'native menu or Internet'])
    for level in ('O0', 'O2'):
        obj = out/f'packet-port-{level}.o'
        obj.unlink(missing_ok=True)
        compile_probe(compiler, [*common, '-'+level], SOURCE, obj,
                      out/f'diagnostics-{level}.txt')
        imported = object_imports(obj)
        missing, unexpected = expected-imported, imported-expected-{'_memcpy', '_memset'}
        if missing or unexpected:
            raise ValueError(dict(level=level, missing=sorted(missing), unexpected=sorted(unexpected)))
        report['results'].append(dict(optimization=level, object_sha256=digest(obj),
                                      api_symbols=sorted(imported & expected),
                                      compiler_helpers=sorted(imported-expected)))
    # A wrong full-width return declaration must fail before symbol inspection.
    source_text = SOURCE.read_text()
    needle = 'r16_native_audit::Status allocatePacket(unsigned,IOSkywalkPacket **,unsigned);'
    if source_text.count(needle) != 1:
        raise ValueError('Negative return fixture target changed')
    bad_source = out/'wrong-allocate-return.cpp'
    bad_source.write_text(source_text.replace(needle,
        'bool allocatePacket(unsigned,IOSkywalkPacket **,unsigned);'))
    with (out/'diagnostics-negative-return.txt').open('w') as diagnostics:
        negative = subprocess.run([*compiler, *common, '-O2', '-I'+str(HERE), '-c',
                                   str(bad_source), '-o', str(out/'wrong-return.o')],
                                  stdout=diagnostics, stderr=diagnostics)
    if negative.returncode == 0 or 'pool allocation must use full IOReturn' not in (
            out/'diagnostics-negative-return.txt').read_text():
        raise ValueError('Wrong allocation return declaration was not rejected')
    report['negative_bool_allocation_rejected'] = True
    with (out/'diagnostics-negative-guard.txt').open('w') as diagnostics:
        guard = subprocess.run([*compiler, *[x for x in common if not x.startswith('-DR16_')],
                                '-O2', '-c', str(SOURCE), '-o', str(out/'wrong-guard.o')],
                               stdout=diagnostics, stderr=diagnostics)
    if guard.returncode == 0 or 'offline ABI audit only' not in (
            out/'diagnostics-negative-guard.txt').read_text():
        raise ValueError('Offline-only compile guard failed')
    report['negative_runtime_guard_rejected'] = True
    if args.kc:
        kc = args.kc.resolve()
        components = args.components.resolve() if args.components else kc.parent/'components.json'
        report['exact_kc'] = inspect_kernel(kc, components, contract)
    report['input_sha256'] = {p.name:digest(p) for p in
        (SOURCE, CONTRACT, HERE/'native_cpu_packet_bridge.hpp',
         HERE/'darwin24_4.json', Path(__file__))}
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    print('Packet Port: O0/O2 imports, return/guard negatives and pinned manifest passed.'
          + (' Exact KC symbols/slots passed.' if args.kc else ' KC bytes not supplied.'))
    print('Compile-only; no kext linked or native menu claimed.')


if __name__ == '__main__':
    main()
