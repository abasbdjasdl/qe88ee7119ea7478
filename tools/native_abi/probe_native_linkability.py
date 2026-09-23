#!/usr/bin/env python3
"""Ask macOS which kext libraries satisfy the offline native ABI imports.

This creates a codeless-personality *probe* bundle around already-audited
objects. It is never loaded or installed. Results apply to the CI runner's
kernel, not to the separately captured Darwin 24.4 Recovery collection.
"""
import argparse
import json
import pathlib
import plistlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def command(*argv, timeout=90):
    completed = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=timeout)
    return {'argv': list(argv), 'status': completed.returncode,
            'output': completed.stdout[-24000:]}


def main():
    if sys.platform != 'darwin':
        raise SystemExit('A macOS kernel library resolver is required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk', type=pathlib.Path)
    parser.add_argument('audit', type=pathlib.Path)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    sdk, audit, out = (path.resolve() for path in (args.sdk, args.audit, args.out))
    out.mkdir(parents=True, exist_ok=True)
    bundle = out / 'R16NativeLinkProbe.kext'
    macos = bundle / 'Contents' / 'MacOS'
    macos.mkdir(parents=True, exist_ok=True)
    module = out / 'NetworkModule.o'
    compile_module = command('xcrun', 'clang', '-target', 'x86_64-apple-macos15.0',
                             '-mkernel', '-DKERNEL', '-DKERNEL_EXTENSION',
                             '-fno-stack-protector', '-mno-red-zone',
                             '-I', str(sdk / 'Headers'), '-c',
                             str(ROOT / 'src/NetworkModule.c'), '-o', str(module))
    if compile_module['status']:
        raise RuntimeError(compile_module)
    objects = [audit / 'startup/startup-O0.o',
               audit / 'controller/native-controller-O0.o',
               audit / 'infra/infra-O0.o',
               audit / 'sequoia/registration-probe.o']
    for obj in objects:
        if not obj.is_file():
            raise FileNotFoundError(obj)
    binary = macos / 'R16NativeLinkProbe'
    linked = command('xcrun', 'ld', '-arch', 'x86_64', '-kext',
                     '-undefined', 'dynamic_lookup', '-o', str(binary),
                     *(str(obj) for obj in objects), str(module),
                     str(sdk / 'Library/x86_64/libkmod.a'))
    report = {'scope': 'read-only CI-runner library resolution; not a load test',
              'runner_macos': command('sw_vers', '-productVersion'),
              'runner_build': command('sw_vers', '-buildVersion'),
              'objects': [str(obj.relative_to(audit)) for obj in objects],
              'link': linked, 'bundle_loaded': False}
    if linked['status'] == 0:
        info = plistlib.loads((ROOT / 'Info-Network.plist').read_bytes())
        info['CFBundleExecutable'] = binary.name
        info['CFBundleName'] = 'R16 native library resolution probe'
        info['IOKitPersonalities'] = {}
        (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
        report['undefined_symbols'] = command('xcrun', 'nm', '-uj', str(binary))
        report['kmutil_libraries'] = command('kmutil', 'libraries', '-p', str(bundle),
                                            timeout=120)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value['status'] for key, value in report.items()
                      if isinstance(value, dict) and 'status' in value}, indent=2))
    # A rejected resolution is evidence, not a reason to lose the artifact.
    # The local Mach-O link itself must work for the probe to be meaningful.
    if linked['status']:
        raise SystemExit('Offline native objects did not link into probe bundle')


if __name__ == '__main__':
    main()
