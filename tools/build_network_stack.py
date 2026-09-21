#!/usr/bin/env python3
"""Build the pinned itlwm protocol stack as a reusable archive, not a driver."""
import hashlib,json,os,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
sdk=pathlib.Path(os.environ['MAC_KERNEL_SDK']).resolve()
commit='53c51c2cdd6e4b69beb91f310d74c53422b0f8bd'
assert sys.platform=='darwin'
assert subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()==commit
assert not subprocess.check_output(['git','-C',str(source),'diff','--','itl80211','include','itlwm/PrivateSPI.pch'],text=True)
dest=root/'build/network-stack';dest.mkdir(parents=True,exist_ok=True)
flags=['-target','x86_64-apple-macos11.0','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-DIEEE80211_STA_ONLY','-D__PRIVATE_SPI__','-fno-stack-protector','-mno-red-zone','-fno-exceptions','-fno-rtti','-std=gnu++14','-Wno-deprecated-register','-Wno-unknown-warning-option','-include',str(source/'itlwm/PrivateSPI.pch')]
for include in ('itl80211/openbsd','itl80211','include'):
    flags+=['-I',str(source/include)]
flags+=['-I',str(sdk/'Headers')]
files=sorted(p for p in (source/'itl80211').rglob('*') if p.suffix in ('.c','.cpp'))
objects=[];manifest=[]
for i,p in enumerate(files):
    output=dest/f'{i:03d}-{p.stem}.o'
    print('compile',p.relative_to(source),flush=True)
    subprocess.run(['xcrun','clang++',*flags,'-x','c++','-c',str(p),'-o',str(output)],check=True)
    objects.append(output)
    manifest.append({'path':p.relative_to(source).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
archive=dest/'libR16Net80211.a'
subprocess.run(['xcrun','libtool','-static','-o',str(archive),*map(str,objects)],check=True)
subprocess.run(['xcrun','nm','-u',str(archive)],stdout=(dest/'undefined-symbols.txt').open('w'),check=True)
# License and exact imported source remain alongside the binary artifact.
import shutil
shutil.copyfile(source/'LICENSE',dest/'ITLWM-LICENSE')
report={'repository':'https://github.com/OpenIntelWireless/itlwm','commit':commit,'sources':manifest,'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'compiled':True,'linked_into_driver':False,'hardware_tested':False,'wifi_operational':False}
(dest/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')
print('Compiled protocol archive:',len(files),'sources. Hardware adapter/link validation remains required.')
