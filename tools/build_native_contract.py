#!/usr/bin/env python3
"""Compile private interface layout evidence; does not build or load a driver."""
import pathlib,re,subprocess,sys,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
upstream=pathlib.Path(sys.argv[1]).resolve();sdk=pathlib.Path(sys.argv[2]).resolve()
out=root/'build/native-contract';out.mkdir(parents=True,exist_ok=True)
header=upstream/'include/Airport/IO80211InfraProtocol.h'
methods=re.findall(r'virtual\s+IOReturn\s+(\w+)\s*\(([^\n;]*?)\)\s*=\s*0\s*;',header.read_text())
assert len(methods)>150
source=['#include <Airport/Apple80211.h>','extern "C" {',
        'unsigned long long R16NativeSizes[]={sizeof(IO80211Controller),sizeof(IO80211InfraProtocol),sizeof(IO80211InfraInterface),sizeof(IO80211SkywalkInterface),sizeof(apple80211_assoc_data),sizeof(apple80211_scan_result)};']
for method,_ in methods:source.append(f'auto R16NativeSlot_{method} = &IO80211InfraProtocol::{method};')
source.append('}')
(out/'layout.cpp').write_text('\n'.join(source)+'\n')
flags=['-target','x86_64-apple-macos15.0','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-D__PRIVATE_SPI__','-DIO80211FAMILY_V2','-D__IO80211_TARGET=140400','-fno-exceptions','-fno-rtti','-std=gnu++14','-Wno-deprecated-register','-include',str(upstream/'itlwm/PrivateSPI.pch'),'-I'+str(upstream/'include'),'-I'+str(upstream/'itl80211'),'-I'+str(upstream/'itl80211/openbsd'),'-I'+str(sdk/'Headers')]
subprocess.run(['xcrun','clang++',*flags,'-c',str(out/'layout.cpp'),'-o',str(out/'layout.o')],check=True)
(out/'metadata.json').write_text(json.dumps({'scope':'Compiled declarations only; ABI compatibility and native UI functionality NOT established','upstream':subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip(),'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'methods':[m for m,_ in methods]},indent=2))
print('Private header layout contract compiled; runtime comparison required.')
