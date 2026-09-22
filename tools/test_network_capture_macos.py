#!/usr/bin/env python3
"""Check the actual ioreg/plutil path on macOS, without saving raw registry data."""
import plistlib, subprocess, sys
assert sys.platform == 'darwin'
raw = subprocess.check_output(['/usr/sbin/ioreg','-r','-c','IOPlatformExpertDevice','-a','-d','2'], timeout=10)
tree = plistlib.loads(raw)
assert isinstance(tree,list) and len(tree)==1
expected=tree[0]['IOObjectClass']
result=subprocess.run(['/usr/bin/plutil','-extract','0.IOObjectClass','raw','-o','-','-'],input=raw,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
assert result.returncode==0, 'Native plutil extraction failed: '+result.stderr.decode(errors='replace')
assert result.stdout.decode().strip()==expected
# Missing properties must remain distinguishable from a successful scalar query.
missing=subprocess.run(['/usr/bin/plutil','-extract','0.R16AbsentProperty','raw','-o','-','-'],input=raw,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
assert missing.returncode!=0
print('PASS: actual macOS ioreg XML and plutil scalar extraction; missing key reports error; no registry data persisted.')
