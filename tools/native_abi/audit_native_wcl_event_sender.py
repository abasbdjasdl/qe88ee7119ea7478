#!/usr/bin/env python3
"""Audit a compile-only WCL event callsite. Never link or invoke it."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = HERE / 'native_wcl_event_sender_probe.cpp'
CONTRACT = HERE / 'native-wcl-event-sender-contract.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compile_object(compiler, common, level, source, obj, log):
    with log.open('w') as diagnostics:
        status = subprocess.run([*compiler, *common, '-'+level, '-c',
                                 str(source), '-o', str(obj)],
                                stdout=diagnostics, stderr=diagnostics)
    return status.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path, help='Zig compiler on Windows')
    parser.add_argument('--kc', type=Path, help='optional exact BootKernelExtensions.kc')
    parser.add_argument('--components', type=Path, help='component map beside KC by default')
    parser.add_argument('--out', type=Path, default=ROOT/'build/native-wcl-event-sender')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report_path = out/'wcl-event-sender-audit.json'
    report_path.unlink(missing_ok=True)
    contract = json.loads(CONTRACT.read_text())
    manifest = json.loads((HERE/'darwin24_4.json').read_text())
    if contract['kernel_sha256'] != manifest['kernel_sha256']:
        raise ValueError('Mixed target-KC profiles')
    expected = set(contract['symbols'])
    if expected != {'__ZN17IO80211PostOffice8sendMailEP23IO80211SkywalkInterfacejPvmb'}:
        raise ValueError('PostOffice import inventory changed without review')
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else ['xcrun', 'clang++']
    target = 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0'
    common = ['-target', target, '-std=c++17', '-Wall', '-Wextra', '-Werror',
              '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector',
              '-fno-sanitize=all', '-nostdinc++', '-DR16_NATIVE_ABI_AUDIT_ONLY=1']
    object_audit = load_module('native_build_audit', ROOT/'tools/build_native_sequoia.py')
    report = dict(kernel_sha256=contract['kernel_sha256'], scope=contract['scope'],
                  results=[], excluded=['actual PostOffice pointer provenance',
                                        'native interface and Glue construction',
                                        'asynchronous delivery acknowledgement',
                                        'safe queue drain before teardown',
                                        'native Wi-Fi menu'])
    for level in ('O0', 'O2'):
        obj = out/f'wcl-event-sender-{level}.o'
        obj.unlink(missing_ok=True)
        log = out/f'diagnostics-{level}.txt'
        if compile_object(compiler, common, level, SOURCE, obj, log):
            raise RuntimeError(f'Compile failed; see {log}')
        imported = object_audit.object_undefined(obj)
        if imported != expected:
            raise ValueError(dict(level=level, imports=sorted(imported),
                                  missing=sorted(expected-imported),
                                  unexpected=sorted(imported-expected)))
        report['results'].append(dict(optimization=level, object_sha256=sha(obj),
                                      imports=sorted(imported)))
    source = SOURCE.read_text()
    needle = 'int sendMail(IO80211SkywalkInterface *,unsigned,void *,unsigned long,bool);'
    if source.count(needle) != 1:
        raise ValueError('Return-type negative fixture changed')
    wrong_return = out/'wrong-return.cpp'
    wrong_return.write_text(source.replace(needle,
        'bool sendMail(IO80211SkywalkInterface *,unsigned,void *,unsigned long,bool);'))
    wrong_log = out/'diagnostics-wrong-return.txt'
    if compile_object(compiler, [*common, '-I'+str(HERE)], 'O2', wrong_return,
                      out/'wrong-return.o', wrong_log) == 0 or \
            'PostOffice status must use full 32-bit IOReturn' not in wrong_log.read_text():
        raise ValueError('Bool return declaration was not rejected')
    report['negative_bool_return_rejected'] = True
    unguarded = [arg for arg in common if not arg.startswith('-DR16_NATIVE_ABI_AUDIT_ONLY')]
    guard_log = out/'diagnostics-unguarded.txt'
    if compile_object(compiler, unguarded, 'O2', SOURCE,
                      out/'unguarded.o', guard_log) == 0 or \
            'WCL event sender probe is offline ABI audit only' not in guard_log.read_text():
        raise ValueError('Audit-only guard was not enforced')
    report['negative_runtime_guard_rejected'] = True
    if args.kc:
        kc = args.kc.resolve()
        components = args.components.resolve() if args.components else kc.parent/'components.json'
        # Reuse the shared Mach-O nlist verifier. This contract intentionally
        # has no vtable claim: only the direct call's external symbol is checked.
        packet_audit = load_module('native_packet_symbol_audit',
                                   HERE/'audit_native_packet_port.py')
        report['exact_kc'] = packet_audit.inspect_kernel(
            kc, components, {**contract,
                             'symbols': {**contract['symbols'], **contract['path']},
                             'vtable_slots': []})
    report['input_sha256'] = {p.name:sha(p) for p in
        (SOURCE, CONTRACT, HERE/'darwin24_4.json', Path(__file__))}
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    print('WCL event callsite: O0/O2 exact import, full-status and guard negatives passed.'
          + (' Exact KC symbol passed.' if args.kc else ' KC bytes not supplied.'))
    print('Compile-only; no live sender, queue-drain proof or native menu.')


if __name__ == '__main__':
    main()
