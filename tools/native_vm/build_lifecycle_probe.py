#!/usr/bin/env python3
"""Build a VM-only native controller allocation/init/free experiment.

No PCI personality, station, fake scan results, hardware or startup transaction.
This tests the exact class layout and init/free before the more complex start.
The generated controller reuses the audited callback bodies verbatim.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Audited controller source changed: ' + old)
    return text.replace(old, new)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=Path)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--zig', type=Path, help='Compile-only on Windows')
    args = parser.parse_args()
    upstream, sdk, out = [x.resolve() for x in (args.upstream, args.sdk, args.out)]
    out.mkdir(parents=True, exist_ok=True)
    (out / 'probe-build.json').unlink(missing_ok=True)
    spec = importlib.util.spec_from_file_location('native_audit', ROOT / 'tools/build_native_sequoia.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    manifest = json.loads((ROOT / 'tools/native_abi/darwin24_4.json').read_text())
    audit.pinned_checkout(upstream, manifest['upstream_commit'])
    audit.pinned_checkout(sdk, manifest['sdk_commit'])
    audit.generate_overlay(manifest, upstream, sdk, out)
    original = (ROOT / 'tools/native_abi/native_controller_prototype.cpp').read_text()
    source = original.replace('R16NativeControllerAudit', 'R16VMController')
    source = replace_once(source, '    R16VMController() = delete;\n    ~R16VMController() override;', '')
    source = replace_once(source, 'class R16VMController final : public IO80211Controller {',
                          'class R16VMController final : public IO80211Controller {\n'
                          '    OSDeclareDefaultStructors(R16VMController);')
    source = replace_once(source, 'R16VMController::~R16VMController() {}',
                          'OSDefineMetaClassAndStructors(R16VMController, IO80211Controller);')
    # The borrowed support objects are deliberately absent in an init/free test.
    # No start call can legally be made on this generated object.
    source += '''
extern "C" IO80211WorkQueue *r16_startup_work_queue(const StartupLedger *) { return nullptr; }
extern "C" CCLogStream *r16_startup_logger(const StartupLedger *) { return nullptr; }
extern "C" IO80211FaultReporter *r16_startup_fault_wrapper(const StartupLedger *) { return nullptr; }
'''
    source = '// GENERATED VM-ONLY EXPERIMENT. Not a deployable network driver.\n' + source
    source += (HERE / 'lifecycle_probe.cpp.inc').read_text()
    generated = out / 'VMController.cpp'
    generated.write_text(source)
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else ['xcrun', 'clang++']
    flags = ['-target', 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0',
             '-mkernel', '-DKERNEL', '-DKERNEL_EXTENSION', '-D__PRIVATE_SPI__',
             '-DIO80211FAMILY_V2', '-D__IO80211_TARGET=140400',
             '-DR16_NATIVE_ABI_AUDIT_ONLY=1', '-DR16_VM_LIFECYCLE_EXPERIMENT=1',
             '-fno-exceptions', '-fno-rtti', '-fno-sanitize=all', '-fno-stack-protector',
             '-mno-red-zone', '-std=gnu++14', '-O0', '-Wno-deprecated-register',
             '-Wno-inconsistent-missing-override',
             '-include', str(upstream / 'itlwm/PrivateSPI.pch'),
             '-I' + str(out / 'include'), '-I' + str(upstream / 'include'),
             '-I' + str(upstream / 'itl80211'), '-I' + str(upstream / 'itl80211/openbsd'),
             '-I' + str(sdk / 'Headers'), '-I' + str(ROOT / 'tools/native_abi')]
    objects = []
    for path in (generated, HERE / 'dependency_identity.cpp'):
        obj = out / (path.stem + '.o')
        subprocess.run(compiler + flags + ['-c', str(path), '-o', str(obj)], check=True)
        objects.append(obj)
    identity_imports=set(audit.object_undefined(objects[1]))
    allowed={'_version_major','_version_minor','_version_revision','_kmod_info','_sysctlbyname','_IOLog'}
    if not allowed<=identity_imports or identity_imports-allowed-{'_memset','_bzero','_memcpy'}:
        raise RuntimeError(('Unexpected dependency identity imports',sorted(identity_imports)))
    # Full emitted controller table, allowing only the metaclass override added
    # by OSDeclareDefaultStructors. Check against the audited baseline table.
    tables = audit.object_vtables(objects[0])
    actual = tables['__ZTV15R16VMController']
    expected = manifest['classes']['IO80211Controller']['table']
    contract = json.loads((ROOT / 'tools/native_abi/native-controller-contract.json').read_text())
    owned = set(contract['own_nonpure_slots']) | {2, 3}
    mismatches = []
    for slot, (got, want) in enumerate(zip(actual, expected)):
        if slot in owned or want == '___cxa_pure_virtual' or 'getMetaClass' in (want or ''):
            if not got or 'R16VMController' not in got:
                mismatches.append([slot, got, want])
            compare = manifest['classes']['IO80211Controller'].get('pure_implementations', {}).get(str(slot), want)
            if slot != 0 and compare and audit.method_identity(got) != audit.method_identity(compare):
                mismatches.append([slot, 'override signature', got, compare])
        elif got != want:
            mismatches.append([slot, got, want])
    if len(actual) != len(expected) or mismatches:
        raise RuntimeError(('VM controller table differs', len(actual), mismatches))
    report = dict(scope='VM-only allocation/init/free experiment; no start, interface or radio',
                  source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                  identity_source_sha256=hashlib.sha256((HERE/'dependency_identity.cpp').read_bytes()).hexdigest(),
                  controller_raw_slots=len(actual), compiled=True, linked=False,
                  identity_imports=sorted(identity_imports),
                  loaded=False, native_wifi_verified=False)
    if not args.zig:
        if sys.platform != 'darwin':
            raise RuntimeError('Link step requires macOS toolchain')
        module = out / 'VMModule.o'
        subprocess.run(['xcrun', 'clang', '-target', 'x86_64-apple-macos15.0', '-mkernel',
                        '-DKERNEL', '-DKERNEL_EXTENSION', '-fno-stack-protector', '-mno-red-zone',
                        '-I' + str(sdk / 'Headers'), '-c', str(HERE / 'VMModule.c'), '-o', str(module)], check=True)
        bundle = out / 'R16NativeVMProbe.kext'
        binary = bundle / 'Contents/MacOS/R16NativeVMProbe'
        binary.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['xcrun', 'ld', '-arch', 'x86_64', '-kext', '-undefined', 'dynamic_lookup',
                        '-o', str(binary), *map(str, objects), str(module),
                        str(sdk / 'Library/x86_64/libkmod.a')], check=True)
        header=struct.unpack_from('<4I',binary.read_bytes())
        if header[0]!=0xfeedfacf or header[1]!=0x1000007 or header[3]!=11:
            raise RuntimeError('Linker did not produce an x86_64 MH_KEXT_BUNDLE')
        libraries = {'com.apple.kpi.bsd': '8.0.0', 'com.apple.kpi.iokit': '8.0.0',
                     'com.apple.kpi.libkern': '8.0.0', 'com.apple.kpi.mach': '8.0.0',
                     'com.apple.kpi.unsupported': '8.0.0',
                     'com.apple.iokit.IONetworkingFamily': '3.0.0',
                     'com.apple.iokit.IO80211Family': '1200.12.2',
                     'com.apple.iokit.IOSkywalkFamily': '1.0.0',
                     'com.apple.driver.corecapture': '1.0.4'}
        info = dict(CFBundleIdentifier='local.r16.nativevm', CFBundleExecutable=binary.name,
                    CFBundleName='R16 VM-only lifecycle probe', CFBundlePackageType='KEXT',
                    CFBundleVersion='0.0.1', CFBundleShortVersionString='0.0.1',
                    OSBundleRequired='Root', OSBundleLibraries=libraries,
                    IOKitPersonalities={'VM-only harness': dict(CFBundleIdentifier='local.r16.nativevm',
                       IOClass='R16VMHarness', IOProviderClass='IOResources', IOResourceMatch='IOKit')})
        (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
        report.update(linked=True, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    (out / 'probe-build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
