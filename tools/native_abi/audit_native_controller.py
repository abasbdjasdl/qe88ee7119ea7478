#!/usr/bin/env python3
"""Compile-only exact-KC native controller vtable and import audit.

The output is an object, never a kext. No methods are called or instantiated.
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
SOURCE = HERE / 'native_controller_prototype.cpp'
CONTRACT = HERE / 'native-controller-contract.json'
TARGET = 'IO80211Controller'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_table(audit, target, actual, class_name, own_nonpure):
    expected = target['table']
    errors = []
    if len(actual) != len(expected):
        errors.append(['length', len(actual), len(expected)])
    for slot, (compiled, original) in enumerate(zip(actual, expected)):
        owned = slot in (2, 3) or slot in own_nonpure or original == '___cxa_pure_virtual'
        if original == '___cxa_pure_virtual':
            original = target['pure_implementations'][str(slot)]
        if owned:
            if not compiled or not re.match(r'__ZNK?' + str(len(class_name)) + class_name, compiled):
                errors.append([slot, 'wrong override owner', compiled])
            if audit.method_identity(compiled) != audit.method_identity(original):
                errors.append([slot, compiled, original])
        elif compiled != original:
            errors.append([slot, compiled, original])
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=pathlib.Path)
    parser.add_argument('sdk', type=pathlib.Path)
    parser.add_argument('overlay', type=pathlib.Path)
    parser.add_argument('--zig', type=pathlib.Path)
    parser.add_argument('--kc', type=pathlib.Path,
                        help='Optional read-only check of the two new imports in the exact Boot KC')
    parser.add_argument('--components', type=pathlib.Path,
                        help='Component map for --kc; defaults beside the KC')
    parser.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/native-controller-prototype')
    args = parser.parse_args()
    upstream, sdk, overlay, out = (p.resolve() for p in
                                   (args.upstream, args.sdk, args.overlay, args.out))
    spec = importlib.util.spec_from_file_location('native_audit', ROOT / 'tools/build_native_sequoia.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    manifest = json.loads((HERE / 'darwin24_4.json').read_text())
    contract = json.loads(CONTRACT.read_text())
    if manifest['kernel_sha256'] != contract['kernel_sha256']:
        raise ValueError('Mixed kernel profiles')
    if args.components and not args.kc:
        raise ValueError('--components requires --kc')
    audit.pinned_checkout(upstream, manifest['upstream_commit'])
    audit.pinned_checkout(sdk, manifest['sdk_commit'])
    target = manifest['classes'][TARGET]
    class_name = contract['class']
    own_nonpure = set(contract['own_nonpure_slots'])
    if len(target['table']) != contract['raw_table_entries'] or (sum(
            s == '___cxa_pure_virtual' for s in target['table']) != contract['pure_override_count']):
        raise ValueError('Target class layout changed')
    if any(target['table'][slot] == '___cxa_pure_virtual' for slot in own_nonpure):
        raise ValueError('A declared nonpure slot became pure')
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'controller-audit.json'
    report_path.unlink(missing_ok=True)
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else ['xcrun', 'clang++']
    flags = ['-target', 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0',
             '-mkernel', '-DKERNEL', '-DKERNEL_EXTENSION', '-D__PRIVATE_SPI__',
             '-DIO80211FAMILY_V2', '-D__IO80211_TARGET=140400',
             '-DR16_NATIVE_ABI_AUDIT_ONLY=1', '-fno-exceptions', '-fno-rtti',
             '-fno-sanitize=all', '-fno-stack-protector', '-mno-red-zone',
             '-std=gnu++14', '-Wno-deprecated-register',
             '-Wno-inconsistent-missing-override',
             '-include', str(upstream / 'itlwm/PrivateSPI.pch'),
             '-I' + str(overlay / 'include'), '-I' + str(upstream / 'include'),
             '-I' + str(upstream / 'itl80211'),
             '-I' + str(upstream / 'itl80211/openbsd'), '-I' + str(sdk / 'Headers')]
    inherited = {symbol for slot, symbol in enumerate(target['table'])
                 if symbol and slot not in (2, 3) and slot not in own_nonpure
                 and symbol != '___cxa_pure_virtual'}
    expected_imports = inherited | set(contract['startup_getters']) | set(contract['extra_kc_imports'])
    report = dict(kernel_sha256=contract['kernel_sha256'], scope=contract['scope'], results=[])
    if args.kc:
        kc = args.kc.resolve()
        if sha(kc) != contract['kernel_sha256']:
            raise ValueError('Boot KC differs from the pinned profile')
        export_spec = importlib.util.spec_from_file_location(
            'native_exports', HERE / 'audit_native_export_dependencies.py')
        exports = importlib.util.module_from_spec(export_spec)
        export_spec.loader.exec_module(exports)
        _, symbols = exports.kc_nlists(
            kc, args.components.resolve() if args.components else kc.parent / 'components.json')
        for symbol, pinned in contract['extra_kc_imports'].items():
            candidates = [item for item in symbols.get(symbol, ()) if item['defined_external']]
            if len(candidates) != 1 or any(
                    candidates[0][field] != pinned[field] for field in ('component', 'address')):
                raise ValueError(dict(symbol=symbol, candidates=candidates, pinned=pinned))
        report['new_kc_imports_checked'] = True
    for level in ('O0', 'O2'):
        obj = out / ('native-controller-' + level + '.o')
        obj.unlink(missing_ok=True)
        with (out / ('diagnostics-' + level + '.txt')).open('w') as diagnostics:
            subprocess.run([*compiler, *flags, '-' + level, '-c', str(SOURCE), '-o', str(obj)],
                           stdout=diagnostics, stderr=diagnostics, check=True)
        table = audit.object_vtables(obj).get('__ZTV' + str(len(class_name)) + class_name, [])
        errors = audit_table(audit, target, table, class_name, own_nonpure)
        if errors:
            raise ValueError(dict(level=level, vtable_errors=errors[:20]))
        imports = audit.object_undefined(obj)
        if imports != expected_imports:
            raise ValueError(dict(level=level, missing=sorted(expected_imports - imports),
                                  unexpected=sorted(imports - expected_imports)))
        report['results'].append(dict(optimization=level, object_sha256=sha(obj),
                                     raw_slots=len(table), inherited_imports=len(inherited),
                                     imports=sorted(imports), vtable=table))
    source = SOURCE.read_text()
    needle = '    ~R16NativeControllerAudit() override;'
    if source.count(needle) != 1:
        raise ValueError('Negative fixture anchor changed')
    mutation = out / 'native-controller-extra-slot.cpp'
    mutation.write_text(source.replace(needle,
        '    virtual IOReturn unintendedSlot() { return kIOReturnUnsupported; }\n' + needle, 1))
    bad_obj = out / 'native-controller-extra-slot.o'
    bad_obj.unlink(missing_ok=True)
    with (out / 'diagnostics-extra-slot.txt').open('w') as diagnostics:
        subprocess.run([*compiler, *flags, '-O0', '-c', str(mutation), '-o', str(bad_obj)],
                       stdout=diagnostics, stderr=diagnostics, check=True)
    bad_table = audit.object_vtables(bad_obj).get('__ZTV' + str(len(class_name)) + class_name, [])
    if not audit_table(audit, target, bad_table, class_name, own_nonpure):
        raise ValueError('The extra virtual slot mutation escaped the audit')
    report['negative_extra_virtual_rejected'] = True
    getter_needle = 'return r16_startup_logger(ledger_);'
    if source.count(getter_needle) != 1:
        raise ValueError('Negative getter fixture anchor changed')
    missing_getter = out / 'native-controller-missing-logger.cpp'
    missing_getter.write_text(source.replace(getter_needle, 'return nullptr;', 1))
    missing_obj = out / 'native-controller-missing-logger.o'
    missing_obj.unlink(missing_ok=True)
    with (out / 'diagnostics-missing-logger.txt').open('w') as diagnostics:
        subprocess.run([*compiler, *flags, '-O0', '-c', str(missing_getter), '-o', str(missing_obj)],
                       stdout=diagnostics, stderr=diagnostics, check=True)
    missing_imports = audit.object_undefined(missing_obj)
    if missing_imports == expected_imports or '_r16_startup_logger' in missing_imports:
        raise ValueError('Missing logger getter import escaped the audit')
    report['negative_missing_logger_import_rejected'] = True
    inputs = [SOURCE, HERE / 'startup_prototype.cpp', CONTRACT,
              HERE / 'darwin24_4.json', pathlib.Path(__file__),
              ROOT / 'tools/build_native_sequoia.py']
    report['input_sha256'] = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    report['overlay_sha256'] = {str(p.relative_to(overlay)): sha(p)
                                for p in sorted((overlay / 'include').rglob('*.h'))}
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print('Exact-KC offline controller audit passed:',
          dict(slots=len(target['table']), inherited_imports=len(inherited),
               own_pure=contract['pure_override_count'], own_nonpure=sorted(own_nonpure)))


if __name__ == '__main__':
    main()
