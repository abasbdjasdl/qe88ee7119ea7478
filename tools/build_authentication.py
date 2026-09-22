#!/usr/bin/env python3
"""Build/test userspace SAE/OWE components. Does not enable driver modes."""
import hashlib,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
upstream=pathlib.Path(sys.argv[1]).resolve();openssl=pathlib.Path(sys.argv[2]).resolve()
revision='24c033de87759e3f8818507a60d873899658a7cf'
assert subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()==revision
assert not subprocess.check_output(['git','-C',str(upstream),'diff','--','src'],text=True)
out=root/'build/authentication';out.mkdir(parents=True,exist_ok=True)
names=['common/sae.c','common/dragonfly.c','common/wpa_common.c',
       'utils/common.c','utils/wpabuf.c','utils/os_unix.c','utils/wpa_debug.c',
       'crypto/crypto_openssl.c','crypto/dh_groups.c','crypto/sha1-prf.c',
       'crypto/sha256-prf.c','crypto/sha256-kdf.c','crypto/sha384-prf.c',
       'crypto/sha384-kdf.c','crypto/sha512-prf.c','crypto/sha512-kdf.c']
sources=[upstream/'src'/n for n in names]+[root/'src/auth/SaeSession.c',root/'src/auth/OweSession.c']
flags=['-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
       '-DCONFIG_SAE','-DCONFIG_OWE','-DCONFIG_ECC','-DCONFIG_SHA256','-DCONFIG_SHA384','-DCONFIG_SHA512',
       '-DCONFIG_NO_STDOUT_DEBUG','-DCONFIG_NO_RANDOM_POOL',
       '-I'+str(upstream/'src'),'-I'+str(upstream/'src/utils'),'-I'+str(openssl/'include')]
objects=[]
for i,p in enumerate(sources):
    obj=out/f'{i:02}-{p.stem}.o';objects.append(obj)
    subprocess.run(['xcrun','clang',*flags,'-c',str(p),'-o',str(obj)],check=True)
for kind in ['sae','owe']:
    test_obj=out/f'{kind}-test.o';binary=out/f'{kind}-exchange-test'
    subprocess.run(['xcrun','clang',*flags,'-c',str(root/f'tests/auth_{kind}_exchange_test.c'),'-o',str(test_obj)],check=True)
    subprocess.run(['xcrun','clang','-fsanitize=address,undefined','-Wl,-dead_strip',*map(str,objects),str(test_obj),
                    '-L'+str(openssl/'lib'),'-lcrypto','-framework','CoreServices','-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],capture_output=True,text=True)
    (out/f'{kind}-test-result.txt').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True)
    result.check_returncode()
(out/'provenance.json').write_text(json.dumps({
    'hostap_repository':'https://git.w1.fi/hostap.git','hostap_revision':revision,
    'scope':'Offline userspace SAE/OWE group19 only. No driver transport/PMF/association integration or hardware validation.',
    'source_sha256':{n:hashlib.sha256((upstream/'src'/n).read_bytes()).hexdigest() for n in names},
    'key_debug_disabled':True,'sanitizers':['address','undefined']},indent=2))
(out/'HOSTAP-COPYING').write_bytes((upstream/'COPYING').read_bytes())
