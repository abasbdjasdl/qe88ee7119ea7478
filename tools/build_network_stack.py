#!/usr/bin/env python3
"""Build the pinned protocol stack and integrated experimental network kext."""
import hashlib,json,os,pathlib,plistlib,struct,subprocess,sys
from network_source_overrides import without_tkip_key_logging,with_session_link_callback
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
sdk=pathlib.Path(os.environ['MAC_KERNEL_SDK']).resolve()
commit='53c51c2cdd6e4b69beb91f310d74c53422b0f8bd'
assert sys.platform=='darwin'
assert subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()==commit
assert not subprocess.check_output(['git','-C',str(source),'diff','--','itl80211','include','itlwm/PrivateSPI.pch'],text=True)
dest=root/'build/network-stack';dest.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'tools/embed_firmware.py')],check=True)
flags=['-target','x86_64-apple-macos11.0','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-DIEEE80211_STA_ONLY','-D__PRIVATE_SPI__','-fno-stack-protector','-mno-red-zone','-fno-exceptions','-fno-rtti','-std=gnu++14','-Wno-deprecated-register','-Wno-unknown-warning-option','-include',str(source/'itlwm/PrivateSPI.pch')]
for include in ('itl80211/openbsd','itl80211','itl80211/linux','include'):
    flags+=['-I',str(source/include)]
flags+=['-I',str(sdk/'Headers'),'-I',str(root/'build/generated')]
files=sorted(p for p in (source/'itl80211').rglob('*') if p.suffix in ('.c','.cpp') and p.name!='CTimeout.cpp')
objects=[];manifest=[]
for i,p in enumerate(files):
    output=dest/f'{i:03d}-{p.stem}.o'
    print('compile',p.relative_to(source),flush=True)
    log=dest/(output.stem+'.diagnostics')
    compiled_source=p
    override_description=None
    if p.name=='ieee80211_crypto_tkip.c':
        compiled_source=dest/'ieee80211_crypto_tkip-no-key-log.c'
        compiled_source.write_text(without_tkip_key_logging(p.read_text()))
        override_description='remove two raw TKIP key log calls'
    elif p.relative_to(source).as_posix()=='itl80211/openbsd/net80211/ieee80211_proto.c':
        compiled_source=dest/'ieee80211_proto-session-link.c'
        compiled_source.write_text(with_session_link_callback(p.read_text()))
        override_description='route two link notifications through session owner'
    result=subprocess.run(['xcrun','clang++',*flags,'-x','c++','-c',str(compiled_source),'-o',str(output)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    log.write_text(result.stdout)
    if result.returncode:
        print(result.stdout[-12000:]);result.check_returncode()
    objects.append(output)
    manifest.append({'path':p.relative_to(source).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),
                     'compiled_source_sha256':hashlib.sha256(compiled_source.read_bytes()).hexdigest(),
                     'override':override_description})
for bridge in sorted((root/'src/network').glob('*.cpp')):
    bridge_obj=dest/(bridge.stem+'.o')
    subprocess.run(['xcrun','clang++',*flags,'-c',str(bridge),'-o',str(bridge_obj)],check=True)
    objects.append(bridge_obj)
    manifest.append({'path':bridge.relative_to(root).as_posix(),'sha256':hashlib.sha256(bridge.read_bytes()).hexdigest()})
subprocess.run(['xcrun','ld','-r','-arch','x86_64','-o',str(dest/'NetworkStack-reloc.o'),*map(str,objects)],check=True)
undefined=subprocess.check_output(['xcrun','nm','-uj',str(dest/'NetworkStack-reloc.o')],text=True).splitlines()
unresolved_protocol=[s for s in undefined if any(x in s for x in ('ieee80211_','HMAC_','pbkdf2_','rijndael','SHA1','SHA256','CTimeout','_fCommandGate','_fWorkloop'))]
assert not unresolved_protocol, 'Protocol dependency still unresolved: '+repr(unresolved_protocol)
unresolved_local=[s for s in undefined if any(x in s for x in ('rtl8852be','R16Pci','R16Network','R16RTL8852BE','r16_net80211_link_status','__ZSt','__ZNSt'))]
assert not unresolved_local, 'Native driver dependency still unresolved: '+repr(unresolved_local)
archive=dest/'libR16Net80211.a'
subprocess.run(['xcrun','libtool','-static','-o',str(archive),*map(str,objects)],check=True)
subprocess.run(['xcrun','nm','-u',str(archive)],stdout=(dest/'undefined-symbols.txt').open('w'),check=True)
# License and exact imported source remain alongside the binary artifact.
import shutil
shutil.copyfile(source/'LICENSE',dest/'ITLWM-LICENSE')
module=dest/'NetworkModule.o'
subprocess.run(['xcrun','clang','-target','x86_64-apple-macos11.0','-mkernel','-DKERNEL','-DKERNEL_EXTENSION',
                '-fno-stack-protector','-mno-red-zone','-I',str(sdk/'Headers'),'-c',str(root/'src/NetworkModule.c'),'-o',str(module)],check=True)
bundle=dest/'RTL8852BENetwork.kext';macos=bundle/'Contents/MacOS';macos.mkdir(parents=True,exist_ok=True)
binary=macos/'RTL8852BENetwork'
subprocess.run(['xcrun','ld','-arch','x86_64','-kext','-undefined','dynamic_lookup','-o',str(binary),
                str(dest/'NetworkStack-reloc.o'),str(module),str(sdk/'Library/x86_64/libkmod.a')],check=True)
info=plistlib.loads((root/'Info-Network.plist').read_bytes())
firmware=(root/'firmware/rtw8852b_fw-1.bin').read_bytes()
info['FirmwareSHA256']=hashlib.sha256(firmware).hexdigest()
info['FirmwareBytes']=len(firmware)
info['FirmwareLicense']=(root/'firmware/LICENCE.rtlwifi_firmware.txt').read_text()
info['ExperimentalProfile']='2.4GHz channels 1-11, 20MHz, legacy rates, software WPA2-CCMP; no concurrent Bluetooth'
info['HardwareTested']=False
(bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
licenses=bundle/'Contents/Resources';licenses.mkdir(exist_ok=True)
shutil.copyfile(source/'LICENSE',licenses/'COPYING.itlwm')
shutil.copyfile(root/'firmware/LICENCE.rtlwifi_firmware.txt',licenses/'LICENCE.rtlwifi_firmware.txt')
subprocess.run(['plutil','-lint',str(bundle/'Contents/Info.plist')],check=True)
data=binary.read_bytes();magic,cpu,subtype,kind=struct.unpack_from('<4I',data)
assert magic==0xfeedfacf and cpu==0x01000007 and kind==0xb,'Expected x86_64 MH_KEXT_BUNDLE'
assert data.count(firmware)==1,'Firmware must be embedded unchanged exactly once'
defined=subprocess.check_output(['xcrun','nm','-jU',str(binary)],text=True).splitlines()
assert '_kmod_info' in defined and '__start' in defined and '__stop' in defined
assert any('R16RTL8852BE' in s for s in defined),'Concrete personality not linked'
subprocess.run(['xcrun','nm','-uj',str(binary)],stdout=(dest/'kext-undefined-symbols.txt').open('w'),check=True)
shutil.make_archive(str(dest/'RTL8852BENetwork-unsigned'), 'gztar',root_dir=dest,base_dir=bundle.name)
report={'repository':'https://github.com/OpenIntelWireless/itlwm','commit':commit,'sources':manifest,
        'upstream_overrides':{'itl80211/openbsd/net80211/CTimeout.cpp':'src/network/Net80211Timers.cpp',
                              'itl80211/openbsd/net80211/ieee80211_crypto_tkip.c':'tools/network_source_overrides.py: remove raw key logging only',
                              'itl80211/openbsd/net80211/ieee80211_proto.c':'tools/network_source_overrides.py: route two link notifications to session owner'},
        'local_headers':{p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'src/network').glob('*.hpp'))},
        'local_includes':{p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'src/network').glob('*.inc'))},
        'firmware_sha256':info['FirmwareSHA256'],'non_network_headers':{p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'src').glob('*.hpp'))},
        'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'compiled':True,'relocatable_link_passed':True,'unresolved_protocol_symbols':unresolved_protocol,'unresolved_local_symbols':unresolved_local,'external_kernel_and_host_symbols':undefined,
        'linked_into_driver':True,'kext_binary_sha256':hashlib.sha256(data).hexdigest(),'hardware_tested':False,'load_verified':False,'wifi_operational':False}
(dest/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')
print('Linked experimental network kext:',binary,'; loading and real radio operation remain unverified.')
