#!/usr/bin/env python3
"""Create a LOCAL credential-configured copy. Never uploads or installs it."""
import argparse
import hashlib
import pathlib
import plistlib
import shutil

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=pathlib.Path)
p.add_argument('destination',type=pathlib.Path)
p.add_argument('--ssid-file',type=pathlib.Path,required=True,help='Raw SSID bytes, no trailing newline')
group=p.add_mutually_exclusive_group(required=True)
group.add_argument('--passphrase-file',type=pathlib.Path,help='WPA2 passphrase bytes, no trailing newline')
group.add_argument('--open-network',action='store_true')
args=p.parse_args()
ssid=args.ssid_file.read_bytes()
if not 1<=len(ssid)<=32:p.error('SSID must contain 1..32 bytes')
psk=None
if args.passphrase_file:
    password=args.passphrase_file.read_bytes()
    if not 8<=len(password)<=63:p.error('WPA2 passphrase must contain 8..63 bytes')
    psk=hashlib.pbkdf2_hmac('sha1',password,ssid,4096,32)
source=args.source.resolve();destination=args.destination.resolve()
if source==destination or destination.exists():p.error('Destination must be a new local directory')
info=plistlib.loads((source/'Contents/Info.plist').read_bytes())
if info.get('CFBundleIdentifier')!='local.rtl8852be.network':p.error('Source must be RTL8852BENetwork.kext')
personality=info['IOKitPersonalities']['RTL8852BE-R16-Network']
personality['R16SSID']=ssid
if psk is not None:personality['R16PSK']=psk
else:personality.pop('R16PSK',None)
shutil.copytree(source,destination)
(destination/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
print('Created local credential-configured bundle; credentials are omitted from output. Not installed.')
