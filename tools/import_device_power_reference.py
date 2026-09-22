#!/usr/bin/env python3
"""Import complete RTL8852B chip power functions from the pinned BSD source."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*args):return subprocess.check_output(['git','-C',str(source),*args])
assert git('rev-parse','HEAD').decode().strip()==commit
files=['rtw8852b.c','mac.c','mac.h','core.h','reg.h']
assert not git('diff','HEAD','--',*files)
texts={f:git('show','HEAD:'+f).decode() for f in files}
def block(text,pattern,semicolon=False):
    m=re.search(pattern,text,re.M);assert m,pattern
    p=text.index('{',m.end()-1);end=p+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    if semicolon:end=text.index(';',end)+1
    return text[m.start():end]
names=['rtw8852b_pwr_sps_ana','rtw8852b_pwr_on_func','rtw8852b_pwr_off_func']
original={n:block(texts['rtw8852b.c'],r'^static (?:int|void) '+n+r'\([^;]+?\n\{') for n in names}
enums=block(texts['core.h'],r'^enum rtw89_cv \{',True)+'\n'+block(texts['mac.h'],r'^enum rtw89_mac_xtal_si_offset \{',True)
macros={}
for text in texts.values():
    for m in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',text.replace('\\\n',' '),re.M):macros[m[1]]=m[2].strip()
needed=set();ordered=[]
def take(n):
    if n in needed or n not in macros:return
    needed.add(n)
    for t in re.findall(r'\b\w+\b',macros[n]):take(t)
    ordered.append(n)
extras='R_AX_IC_PWR_STATE B_AX_WLMAC_PWR_STE_MASK R_AX_WLAN_XTAL_SI_CTRL B_AX_WL_XTAL_SI_CMD_POLL R_AX_SCOREBOARD MAC_AX_NOTIFY_TP_MAJOR MAC_AX_NOTIFY_PWR_MAJOR XTAL_SI_NORMAL_READ XTAL_SI_NORMAL_WRITE'
for t in re.findall(r'\b\w+\b','\n'.join(original.values())+enums+extras):take(t)
def adapt(t):
    t=t.replace('struct rtw89_dev','Context').replace('struct rtw89_efuse','Efuse')
    t=t.replace('test_bit(RTW89_FLAG_PROBE_DONE, rtwdev->flags)','rtwdev->probeDone')
    t=re.sub(r'\bBIT\(','bit(',t);t=re.sub(r'\bGENMASK\(','mask(',t)
    t=re.sub(r'\bfsleep\(([^;]+)\)',r'delay(rtwdev,\1)',t)
    return t.replace('read_poll_timeout(', 'R16_POWER_POLL(')
notice='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation\n// Generated from pinned rtw89 '+commit+'; BSD option.\n'
header=notice+'#pragma once\n#include <stdint.h>\nnamespace rtl8852be { namespace powerseq {\n'
header+='using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;\n'
header+='constexpr u32 bit(unsigned n){return u32(1)<<n;}\nconstexpr u32 mask(unsigned hi,unsigned lo){return (0xffffffffu>>(31-hi))&(0xffffffffu<<lo);}\n'
header+='constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}\n'
# Definitions embedded between enum entries become the same namespaced constants
# as definitions outside the enum. Leaving #define here leaks into other modules
# and rewrites the constexpr declarations below.
header+=adapt(re.sub(r'^#define[^\n]*\n','',enums,flags=re.M))+'\n'+'\n'.join('constexpr u32 '+n+' = '+adapt(macros[n])+';' for n in ordered)+'\n} }\n'
dest=root/'src/network'
(dest/'Rtw8852bDevicePowerConstants.hpp').write_bytes(header.encode())
(dest/'Rtw8852bDevicePowerFunctions.inc').write_bytes((notice+'\n\n'.join(adapt(original[n]) for n in names)+'\n').encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option',
        'sources':{n:hashlib.sha256(t.encode()).hexdigest() for n,t in texts.items()},
        'original_functions':{n:hashlib.sha256(t.encode()).hexdigest() for n,t in original.items()},
        'adaptations':['typed Context and Efuse','bounded failure-latching poll/delay helpers','explicit probeDone lifecycle input','namespaced constexpr register definitions'],
        'generated':{n:hashlib.sha256((dest/n).read_bytes()).hexdigest() for n in ['Rtw8852bDevicePowerConstants.hpp','Rtw8852bDevicePowerFunctions.inc']},'hardware_tested':False}
(dest/'device-power-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported',len(original),'complete chip power functions and',len(ordered),'constants')
