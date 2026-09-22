#!/usr/bin/env python3
"""Import only numeric fields used by native 8852B PHY initialization."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*args):return subprocess.check_output(['git','-C',str(source),*args])
assert git('rev-parse','HEAD').decode().strip()==commit
files=['reg.h','phy.h','mac.h','core.h','rtw8852b.c','phy.c','core.c','chan.c']
assert not git('diff','HEAD','--',*files)
texts={f:git('show','HEAD:'+f).decode() for f in files}
macros={}
for f in ['reg.h','phy.h','mac.h']:
    for m in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',texts[f].replace('\\\n',' '),re.M):
        macros[m[1]]=re.sub(r'/\*.*?\*/','',m[2]).strip()
used=set(re.findall(r'\b(?:R_|B_|RR_)[A-Z0-9_]+\b',(root/'src/network/MacPhyInitialization.cpp').read_text()))
ordered=[];seen=set()
def take(name):
    if name in seen:return
    assert name in macros,name
    seen.add(name)
    for token in re.findall(r'\b\w+\b',macros[name]):
        if token in macros:take(token)
    ordered.append(name)
for name in sorted(used):take(name)
text='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation (BSD option)\n// Generated from pinned rtw89 '+commit+'; do not edit.\n#pragma once\n#include <stdint.h>\nnamespace rtl8852be { namespace network { namespace phyinit_constants {\n'
text+='constexpr uint32_t bit(unsigned n){return uint32_t(1)<<n;}\nconstexpr uint32_t mask(unsigned h,unsigned l){return (0xffffffffu>>(31-h))&(0xffffffffu<<l);}\n'
for name in ordered:
    value=macros[name].replace('BIT(', 'bit(').replace('GENMASK(', 'mask(')
    text+=f'constexpr uint32_t {name} = {value};\n'
text+='} } }\n'
dest=root/'src/network/Rtw8852bPhyInitConstants.hpp';dest.write_bytes(text.encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option',
        'source_sha256':{f:hashlib.sha256(s.encode()).hexdigest() for f,s in texts.items()},
        'constants':{n:macros[n] for n in ordered},'generated_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
        'scope':'numeric masks/addresses; native implementation retains only actual 8852B/AX initial hardware branches',
        'hardware_tested':False}
(root/'src/network/phy-init-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported',len(ordered),'PHY initialization numeric fields')
