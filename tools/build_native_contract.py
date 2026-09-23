#!/usr/bin/env python3
"""Compile private interface layout evidence; does not build or load a driver."""
import pathlib,re,subprocess,sys,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
upstream=pathlib.Path(sys.argv[1]).resolve();sdk=pathlib.Path(sys.argv[2]).resolve()
out=root/'build/native-contract';out.mkdir(parents=True,exist_ok=True)
header=upstream/'include/Airport/IO80211InfraProtocol.h'
methods=re.findall(r'virtual\s+IOReturn\s+(\w+)\s*\(([^\n;]*?)\)\s*=\s*0\s*;',header.read_text())
assert len(methods)>150
source=['#include <Airport/Apple80211.h>','#include "NativeWirelessData.hpp"','#include "NativeWirelessRequests.hpp"','extern "C" {',
        'unsigned long long R16NativeSizes[]={sizeof(IO80211Controller),sizeof(IO80211InfraProtocol),sizeof(IO80211InfraInterface),sizeof(IO80211SkywalkInterface),sizeof(apple80211_assoc_data),sizeof(apple80211_scan_result)};']
for method,_ in methods:source.append(f'auto R16NativeSlot_{method} = &IO80211InfraProtocol::{method};')
source.append('}')
source.append('IOReturn R16NativeDataCompile(const rtl8852be::network::wireless::Snapshot &s, apple80211_ssid_data *a, apple80211_bssid_data *b, apple80211_channel_data *c, apple80211_rssi_data *d) { using namespace rtl8852be::network::nativewifi; return ssid(s,a)|bssid(s,b)|channel(s,c)|rssi(s,d); }')
source.append('struct R16CompileBackend { IOReturn selectWirelessNetwork(const rtl8852be::network::selection::Join&); IOReturn disconnectWirelessNetwork(); };')
source.append('IOReturn R16NativeRequestCompile(R16CompileBackend &b, const apple80211_assoc_data *a) { return rtl8852be::network::nativewifi::associate(b,a); }')
(out/'layout.cpp').write_text('\n'.join(source)+'\n')
flags=['-target','x86_64-apple-macos15.0','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-D__PRIVATE_SPI__','-DIO80211FAMILY_V2','-D__IO80211_TARGET=140400','-fno-exceptions','-fno-rtti','-std=gnu++14','-Wno-deprecated-register','-include',str(upstream/'itlwm/PrivateSPI.pch'),'-I'+str(upstream/'include'),'-I'+str(upstream/'itl80211'),'-I'+str(upstream/'itl80211/openbsd'),'-I'+str(sdk/'Headers')]
subprocess.run(['xcrun','clang++',*flags,'-I'+str(root/'src/network'),'-c',str(out/'layout.cpp'),'-o',str(out/'layout.o')],check=True)
subprocess.run(['xcrun','clang++',*flags,'-I'+str(root/'src/network'),'-c',str(root/'tests/network_native_adapter_test.cpp'),'-o',str(out/'adapter-test.o')],check=True)
(out/'adapter-main.cpp').write_text('#include <stdio.h>\nextern "C" int R16NativeAdapterTest();\nint main(){int r=R16NativeAdapterTest();printf("Native adapter test failure line: %d (0 means pass)\\n",r);return r?1:0;}\n')
subprocess.run(['xcrun','clang++',str(out/'adapter-main.cpp'),str(out/'adapter-test.o'),'-o',str(out/'adapter-test')],check=True)
subprocess.run([str(out/'adapter-test')],check=True)
local_inputs=sorted((root/'src/network').glob('*.hpp'))+sorted((root/'tests').glob('network_native_*'))
(out/'metadata.json').write_text(json.dumps({'scope':'Compiled declarations and offline byte-view tests only; ABI compatibility and native UI functionality NOT established','upstream':subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip(),'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'methods':[m for m,_ in methods],
    'port_revision':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
    'port_inputs_sha256':{p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in local_inputs}},indent=2))
print('Private header layout contract compiled; runtime comparison required.')
