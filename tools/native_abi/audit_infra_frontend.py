#!/usr/bin/env python3
"""Compile-only exact-KC InfraProtocol frontend: full vtable and import audit.

Consumes the generated overlay without changing it. No linking, object creation,
IOKit method invocation, native pointer reads, radio or credentials are involved.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CLASS = 'R16InfraFrontend'
TARGET = 'IO80211InfraProtocol'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def vtable_errors(audit, specification, table):
    expected = specification['table']
    errors = []
    if len(table) != len(expected):
        errors.append(['length', len(table), len(expected)])
    for slot, (actual, target) in enumerate(zip(table, expected)):
        own = target == '___cxa_pure_virtual' or slot in (2, 3)
        if target == '___cxa_pure_virtual':
            target = specification['pure_implementations'][str(slot)]
        if own:
            if not actual or not re.match(r'__ZNK?' + str(len(CLASS)) + CLASS, actual):
                errors.append([slot, 'wrong override owner', actual])
            if audit.method_identity(actual) != audit.method_identity(target):
                errors.append([slot, actual, target])
        elif actual != target:
            errors.append([slot, actual, target])
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=pathlib.Path)
    parser.add_argument('sdk', type=pathlib.Path)
    parser.add_argument('overlay', type=pathlib.Path)
    parser.add_argument('--zig', type=pathlib.Path)
    parser.add_argument('--out', type=pathlib.Path, default=ROOT/'build/native-infra-prototype')
    args = parser.parse_args()
    upstream, sdk, overlay, out = (p.resolve() for p in
                                   (args.upstream, args.sdk, args.overlay, args.out))
    spec = importlib.util.spec_from_file_location('native_audit', ROOT/'tools/build_native_sequoia.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    manifest = json.loads((HERE/'darwin24_4.json').read_text())
    contract = json.loads((HERE/'infra-frontend-contract.json').read_text())
    registration = json.loads((HERE/'registration-symbols.json').read_text())
    if not manifest['kernel_sha256'] == contract['kernel_sha256'] == registration['kernel_sha256']:
        raise ValueError('Mixed kernel profiles')
    audit.pinned_checkout(upstream, manifest['upstream_commit'])
    audit.pinned_checkout(sdk, manifest['sdk_commit'])
    out.mkdir(parents=True, exist_ok=True)
    report_path = out/'infra-audit.json'
    report_path.unlink(missing_ok=True)
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else ['xcrun', 'clang++']
    flags = ['-target', 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0',
             '-mkernel', '-DKERNEL', '-DKERNEL_EXTENSION', '-D__PRIVATE_SPI__',
             '-DIO80211FAMILY_V2', '-D__IO80211_TARGET=140400', '-DR16_NATIVE_ABI_AUDIT_ONLY=1',
             '-fno-exceptions', '-fno-rtti', '-fno-sanitize=all',
             '-fno-stack-protector', '-mno-red-zone', '-std=gnu++14',
             '-Wno-deprecated-register', '-Wno-inconsistent-missing-override',
             '-include', str(upstream/'itlwm/PrivateSPI.pch'), '-I'+str(HERE),
             '-I'+str(overlay/'include'), '-I'+str(upstream/'include'),
             '-I'+str(upstream/'itl80211'), '-I'+str(upstream/'itl80211/openbsd'),
             '-I'+str(sdk/'Headers')]
    target = manifest['classes'][TARGET]
    inherited = {s for i, s in enumerate(target['table'])
                 if s and i not in (2, 3) and s != '___cxa_pure_virtual'}
    helper_names = set(contract['registration_helpers'])
    helper_symbols = {h['symbol'] for h in registration['helpers'] if h['name'] in helper_names}
    if len(helper_symbols) != len(helper_names):
        raise ValueError('Registration evidence missing helper')
    report = dict(kernel_sha256=contract['kernel_sha256'], scope=contract['scope'], results=[])
    for level in ('O0', 'O2'):
        obj = out/('infra-'+level+'.o')
        obj.unlink(missing_ok=True)
        with (out/('diagnostics-'+level+'.txt')).open('w') as log:
            subprocess.run([*compiler, *flags, '-'+level, '-c', str(HERE/'infra_frontend_prototype.cpp'),
                            '-o', str(obj)], stdout=log, stderr=log, check=True)
        table = audit.object_vtables(obj).get('__ZTV'+str(len(CLASS))+CLASS, [])
        errors = vtable_errors(audit, target, table)
        if errors:
            raise ValueError(dict(level=level, vtable_errors=errors))
        imported = audit.object_undefined(obj)
        required = inherited | helper_symbols | set(contract['extra_imports'][level])
        # Apple Clang may lower the 5456-byte zero-initialized owned scratch
        # member to libkern memset. Zig's Clang currently expands that init.
        # This is the only permitted compiler-generated import; preserve it
        # explicitly in the report rather than weakening the target ABI set.
        generated = imported-required
        allowed_generated = {'_memset'} if not args.zig else set()
        if required-imported or generated-allowed_generated:
            raise ValueError(dict(level=level, missing=sorted(required-imported),
                                  unexpected=sorted(generated-allowed_generated)))
        report['results'].append(dict(optimization=level, slots=len(table),
            overrides=target['table'].count('___cxa_pure_virtual'), object_sha256=sha(obj),
            imports=sorted(imported), compiler_generated_imports=sorted(generated), vtable=table))
    # A declaration drift that still compiles must be rejected by the full
    # table check. Mutated code stays in build output, never in shared sources.
    source = (HERE/'infra_frontend_prototype.cpp').read_text()
    mutation = '    virtual IOReturn unintendedSlot() { return kIOReturnUnsupported; }\n'
    negative = out/'infra-extra-slot.cpp'
    negative.write_text(source.replace('    ~R16InfraFrontend() override;',
                                      mutation+'    ~R16InfraFrontend() override;', 1))
    bad_obj = out/'infra-extra-slot.o'
    bad_obj.unlink(missing_ok=True)
    with (out/'diagnostics-extra-slot.txt').open('w') as log:
        subprocess.run([*compiler, *flags, '-O0', '-c', str(negative), '-o', str(bad_obj)],
                       stdout=log, stderr=log, check=True)
    bad_table = audit.object_vtables(bad_obj).get('__ZTV'+str(len(CLASS))+CLASS, [])
    rejected = vtable_errors(audit, target, bad_table)
    if not rejected:
        raise ValueError('Extra virtual slot was not detected')
    report['negative_extra_slot'] = rejected
    sources = [HERE/'infra_frontend_prototype.cpp', HERE/'native_infra_scan_bridge.hpp',
               HERE/'native_infra_scan_bridge_test.cpp', HERE/'infra-frontend-contract.json',
               HERE/'infra-frontend-evidence.md',
               HERE/'darwin24_4.json', HERE/'registration-symbols.json', pathlib.Path(__file__),
               ROOT/'tools/build_native_sequoia.py', ROOT/'src/network/NativeWclScan.hpp',
               ROOT/'src/network/NativeWclScanPlan.hpp', ROOT/'src/network/NativeForegroundScan.hpp']
    report['source_hashes'] = {str(p.relative_to(ROOT)):sha(p) for p in sources}
    report['overlay_hashes'] = {str(p.relative_to(overlay)):sha(p)
                                for p in sorted((overlay/'include').rglob('*.h'))}
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    print('InfraProtocol prototype: O0/O2 full 658-entry table + exact imports + extra-slot rejection passed')
    print('Compile-only. No trusted live dispatch adapter, native interface registration or scan events executed.')


if __name__ == '__main__':
    main()
