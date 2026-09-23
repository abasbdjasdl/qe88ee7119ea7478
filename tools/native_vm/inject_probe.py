#!/usr/bin/env python3
"""Inject the VM-only probe into a new copy of the disposable OpenCore image."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--bundle',type=Path,required=True)
p.add_argument('--python-packages',type=Path,required=True)
p.add_argument('--name',default='OpenCore-probe-init')
p.add_argument('--stage',type=int,choices=[1,2,3,4,5],default=1)
a=p.parse_args();root=a.root.resolve();bundle=a.bundle.resolve()
if '/' in a.name or '\\' in a.name or not a.name.startswith('OpenCore-probe-'):
    raise ValueError('Choose a local VM image name')
if bundle.name!='R16NativeVMProbe.kext':raise ValueError('Only the isolated probe is accepted')
info=plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
personalities=info['IOKitPersonalities']
if info['CFBundleIdentifier']!='local.r16.nativevm' or len(personalities)!=1:
    raise ValueError('Unexpected probe identity')
entry=next(iter(personalities.values()))
if entry['IOClass']!='R16VMHarness' or entry['IOProviderClass']!='IOResources':
    raise ValueError('Probe must not match hardware')
target=root/(a.name+'.raw');qcow=root/(a.name+'.qcow2')
if target.exists() or qcow.exists():raise FileExistsError('Preserve earlier test images')
source=root/'OpenCore-lifecycle.raw'
with source.open('rb') as f:
    f.seek(512);header=f.read(512)
    if header[:8]!=b'EFI PART':raise ValueError('Missing GPT header')
    table_lba=struct.unpack_from('<Q',header,72)[0]
    f.seek(table_lba*512);first=f.read(128)
    start,end=struct.unpack_from('<QQ',first,32)
    if not 0<start<end or (end+1)*512>source.stat().st_size:raise ValueError('Invalid GPT extent')
sys.path.insert(0,str(a.python_packages.resolve()))
from pyfatfs.PyFatFS import PyFatFS
shutil.copyfile(source,target)
hashes={}
with PyFatFS(str(target),offset=start*512) as fs:
    path='EFI/OC/config.plist';config=plistlib.loads(fs.readbytes(path))
    guid='7C436110-AB2A-4BBB-A880-FE41995C9F82'
    if 'r16vmtest=1' not in config['NVRAM']['Add'][guid]['boot-args'].split():
        raise ValueError('VM lifecycle gate is not configured')
    config['NVRAM']['Add'][guid]['boot-args']=' '.join(
        'r16vmtest='+str(a.stage) if token=='r16vmtest=1' else token
        for token in config['NVRAM']['Add'][guid]['boot-args'].split())
    if any(k['BundlePath']==bundle.name for k in config['Kernel']['Add']):
        raise ValueError('Probe already configured')
    for local in sorted(bundle.rglob('*')):
        if local.is_symlink():raise ValueError('No bundle symlinks permitted')
        if not local.is_file():continue
        rel=local.relative_to(bundle).as_posix();destination='EFI/OC/Kexts/'+bundle.name+'/'+rel
        fs.makedirs(destination.rsplit('/',1)[0],recreate=True)
        data=local.read_bytes();fs.writebytes(destination,data)
        hashes[rel]=hashlib.sha256(data).hexdigest()
    config['Kernel']['Add'].append(dict(Arch='x86_64',BundlePath=bundle.name,
        Comment='VM-only lifecycle experiment; never deploy to physical EFI',Enabled=True,
        ExecutablePath='Contents/MacOS/R16NativeVMProbe',PlistPath='Contents/Info.plist',
        MinKernel='24.4.0',MaxKernel='24.4.0'))
    # PyFatFS 1.1.0 can retain a freed first-cluster pointer when truncating an
    # existing file. Replace the directory entry in this disposable copy.
    fs.remove(path)
    fs.writebytes(path,plistlib.dumps(config,sort_keys=False))
with PyFatFS(str(target),offset=start*512,read_only=True) as fs:
    if plistlib.loads(fs.readbytes('EFI/OC/config.plist'))!=config:raise ValueError('Config readback differs')
    for rel,expected in hashes.items():
        if hashlib.sha256(fs.readbytes('EFI/OC/Kexts/'+bundle.name+'/'+rel)).hexdigest()!=expected:
            raise ValueError('Bundle readback differs')
subprocess.run(['C:/Program Files/qemu/qemu-img.exe','convert','-f','raw','-O','qcow2',str(target),str(qcow)],check=True)
(root/(a.name+'.json')).write_text(json.dumps(dict(partition_offset=start*512,bundle=hashes,stage=a.stage,
    qcow_sha256=hashlib.sha256(qcow.read_bytes()).hexdigest(),scope='VM-only; physical EFI untouched'),indent=2)+'\n')
print(qcow)
