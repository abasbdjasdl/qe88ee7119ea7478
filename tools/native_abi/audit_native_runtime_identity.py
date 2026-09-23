#!/usr/bin/env python3
"""Compile the runtime build-ID reader and audit its exact imports/slots."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from audit_native_packet_port import inspect_kernel,object_imports
from audit_startup_prototype import constants
from audit_native_export_dependencies import kc_nlists

HERE=Path(__file__).resolve().parent
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('sdk',type=Path)
    p.add_argument('--zig',type=Path);p.add_argument('--kc',type=Path)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True);report_path=a.out/'identity-audit.json'
    report_path.unlink(missing_ok=True)
    contract=json.loads((HERE/'native-runtime-identity-contract.json').read_text())
    if contract['kernel_sha256']!=json.loads((HERE/'darwin24_4.json').read_text())['kernel_sha256']:
        raise ValueError('Mixed target profiles')
    header=(HERE/'native_runtime_identity.hpp').read_text()
    declared={name:bytes(int(v,16) for v in re.findall(r'0x[0-9a-f]+',body)).hex()
              for name,body in re.findall(r'\{"([^"]+)",\{([^}]+)\}\}',header)}
    if declared!=contract['components']:raise ValueError('Header UUIDs differ from audited profile')
    report={'scope':contract['scope'],'results':[],'runtime_executed':False}
    if a.kc:
        components=a.kc.parent/'components.json'
        report['kernel']=inspect_kernel(a.kc,components,contract)
        entries,_=kc_nlists(a.kc,components);data=a.kc.read_bytes();found={}
        for entry in entries:
            identifier=entry['identifier']
            if identifier not in contract['components']:continue
            cursor=entry['offset']+32
            for _ in range(struct.unpack_from('<I',data,entry['offset']+16)[0]):
                cmd,size=struct.unpack_from('<II',data,cursor)
                if cmd==27:
                    if identifier in found or size!=24:raise ValueError('Invalid/duplicate UUID')
                    found[identifier]=data[cursor+8:cursor+24].hex()
                cursor+=size
        if found!=contract['components']:raise ValueError('KC component UUIDs differ')
        report['component_uuids']=found
    compiler=[str(a.zig.resolve()),'c++'] if a.zig else ['xcrun','clang++']
    flags=['-target','x86_64-macos' if a.zig else 'x86_64-apple-macos15.0',
           '-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-D__PRIVATE_SPI__',
           '-DR16_NATIVE_ABI_AUDIT_ONLY=1','-fno-exceptions','-fno-rtti','-fno-sanitize=all',
           '-fno-stack-protector','-mno-red-zone','-std=gnu++14','-I',str(a.sdk.resolve()/'Headers')]
    source=HERE/'native_runtime_identity.cpp'
    for opt in ('O0','O2'):
        obj=a.out/('runtime-identity-'+opt+'.o')
        subprocess.run([*compiler,*flags,'-'+opt,'-c',str(source),'-o',str(obj)],check=True)
        imports=object_imports(obj)
        required=set(contract['direct_imports'])
        if not required<=set(imports) or set(imports)-required-set(contract['optional_compiler_helpers']):
            raise ValueError('Unexpected runtime imports: '+repr(sorted(imports)))
        actual=constants(obj,contract['member_pointer_constants'])
        if actual!=contract['member_pointer_constants']:raise ValueError('Runtime virtual slot mismatch')
        report['results'].append({'optimization':opt,'imports':sorted(imports),
            'member_pointer_constants':actual,'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest()})
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('Runtime identity reader: O0/O2 exported KPI/dependency imports passed; see separate VM execution evidence')
if __name__=='__main__':main()
