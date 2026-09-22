#!/usr/bin/env python3
"""Check the actual ioreg/plutil path on macOS, without saving raw registry data."""
import plistlib, subprocess, sys
from pathlib import Path
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
for name in ('collect_network_state.sh', 'run-network-test.sh'):
    script=Path(__file__).with_name(name).read_text()
    start=script.index('bounded() ('); end=script.index('\n)', start)+2
    wrapper=script[start:end]
    command='/usr/bin/plutil -extract 0.IOObjectClass raw -o - -'
    query='\nbounded 3 /usr/sbin/ioreg -r -c IOPlatformExpertDevice -a -d 2 | bounded 3 '+command+'\n'
    for shell in ('/bin/sh', '/bin/bash'):
        result=subprocess.run([shell, '-c', wrapper+query],capture_output=True,text=True,timeout=10)
        assert result.returncode==0 and result.stdout.strip()==expected, (name,shell,result.returncode,result.stderr)
        # Regression control: original asynchronous invocation must lose input.
        broken=wrapper.replace('exec 9<&0\n    "$@" <&9 9<&- &', '"$@" &')
        assert broken!=wrapper
        result=subprocess.run([shell, '-c', broken+query],capture_output=True,text=True,timeout=10)
        assert result.returncode!=0, 'Regression control unexpectedly passed'
    print('PASS: exact '+name+' timeout wrapper preserves native plist pipeline; old wrapper fails.')
print('PASS: actual macOS ioreg XML and plutil scalar extraction; missing key reports error; no registry data persisted.')

worker=Path(__file__).with_name('run-network-test.sh').read_text()
value=worker[worker.index('value() {'):worker.index('\nresolve()')].replace('R16RTL8852BE','IOPlatformExpertDevice')
start=worker.index('bounded() (');end=worker.index('\n)',start)+2
for shell in ('/bin/sh','/bin/bash'):
    for query,ok in [('0.IOObjectClass',True),('1.IOObjectClass',False)]:
        command=worker[start:end]+'\n'+value+'\nexec 4>/dev/null\nvalue '+query
        result=subprocess.run([shell,'-c',command],capture_output=True,text=True,timeout=10)
        assert (result.returncode==0)==ok
        assert result.stdout.strip()==(expected if ok else ''), result.stdout
print('PASS: worker value suppresses failed plutil stdout; absent second controller stays empty.')

# Class filtering without -l omits properties on nonmatching children.
import re
full=subprocess.check_output(['/usr/sbin/ioreg','-r','-c','IOPlatformExpertDevice','-a','-l','-d','2'],timeout=10)
full_tree=plistlib.loads(full)
candidates=[]
for index,child in enumerate(full_tree[0].get('IORegistryEntryChildren',[])):
    for key,value in child.items():
        if not key.startswith('IORegistry') and re.fullmatch(r'[A-Za-z0-9_ -]+',key) and isinstance(value,(str,int)):
            if key not in tree[0]['IORegistryEntryChildren'][index]:candidates.append((index,key))
assert candidates, 'Expected a child property included only with -l'
index,key=candidates[0]
query='0.IORegistryEntryChildren.'+str(index)+'.'+key
result=subprocess.run(['/usr/bin/plutil','-extract',query,'raw','-o','-','-'],input=full,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
assert result.returncode==0
for name in ('run-network-test.sh','collect_network_state.sh'):
    assert '-c R16RTL8852BE -a -l -d 2' in Path(__file__).with_name(name).read_text()
print('PASS: actual class-filtered ioreg child property requires -l; collector and worker include it.')
