#!/usr/bin/env python3
"""Host model tests and a freestanding x86_64 Mach-O compile, never a kext."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--out', type=Path, default=REPO/'build/native-cpu-packet-bridge')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = [str(args.zig.resolve()), 'c++'] if args.zig else [shutil.which('clang++') or 'clang++']
    exe = out/('native_cpu_packet_bridge_test.exe' if platform.system() == 'Windows' else 'native_cpu_packet_bridge_test')
    obj = out/'native_cpu_packet_bridge_probe.o'
    result = out/'result.json'
    for path in (exe,obj,result,out/'negative.o'):
        path.unlink(missing_ok=True)
    flags = ['-std=c++17','-O2','-Wall','-Wextra','-Werror']
    define = ['-DR16_NATIVE_ABI_AUDIT_ONLY=1']
    source = HERE/'native_cpu_packet_bridge_test.cpp'
    subprocess.run([*compiler,*flags,*define,'-fsanitize=undefined,bounds',
                    '-fno-sanitize-recover=all',str(source),'-o',str(exe)],check=True)
    host = subprocess.run([str(exe)],text=True,capture_output=True,check=True)
    print(host.stdout.strip())
    target = 'x86_64-macos' if args.zig else 'x86_64-apple-macos15.0'
    cross = [*compiler,*flags,'-target',target,'-ffreestanding','-fno-exceptions',
             '-fno-rtti','-fno-stack-protector','-nostdinc++','-c']
    subprocess.run([*cross,*define,str(HERE/'native_cpu_packet_bridge_probe.cpp'),
                    '-o',str(obj)],check=True)
    negative = subprocess.run([*compiler,*flags,*define,'-DR16_TEST_BAD_ALLOCATE_RETURN=1',
                               '-c',str(source),'-o',str(out/'negative.o')],
                              text=True,capture_output=True)
    if negative.returncode == 0 or 'allocateRx returns IOReturn, never bool' not in negative.stderr:
        raise RuntimeError('bool allocation ABI negative check did not reject the declaration')
    guard = subprocess.run([*cross,str(HERE/'native_cpu_packet_bridge_probe.cpp'),
                            '-o',str(out/'negative.o')],text=True,capture_output=True)
    if guard.returncode == 0 or 'not approved for a loaded driver' not in guard.stderr:
        raise RuntimeError('audit-only compile guard negative check failed')
    sources = [HERE/name for name in ('native_cpu_packet_bridge.hpp',
               'native_cpu_packet_bridge_probe.cpp','native_cpu_packet_bridge_test.cpp',
               'native_cpu_packet_bridge_test.py')]
    report = dict(scope='Offline opaque-port ownership model only; no runtime/API adapter validation',
                  kernel_sha256='d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1',
                  host=host.stdout.strip(),sanitizers=['undefined','bounds'],
                  negative_checks=['bool-allocation-ABI','audit-only-guard'],
                  target=target,object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),
                  source_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    result.write_text(json.dumps(report,indent=2)+'\n')
    print('Mach-O compile and both negative checks passed; no kext generated.')


if __name__ == '__main__':
    main()
