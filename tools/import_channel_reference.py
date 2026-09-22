#!/usr/bin/env python3
"""Import the pinned 8852B MAC/BB/RF channel-programming closure."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*a):return subprocess.check_output(['git','-C',str(source),*a])
assert git('rev-parse','HEAD').decode().strip()==commit
files=['rtw8852b.c','rtw8852b_rfk.c','phy.c','phy.h','mac.c','mac.h','core.h','reg.h']
assert not git('diff','HEAD','--',*files)
texts={f:git('show','HEAD:'+f).decode() for f in files}
def block(text,pattern,semicolon=False):
    m=re.search(pattern,text,re.M);assert m,pattern
    p=text.index('{',m.end()-1);end=p+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    if semicolon:end=text.index(';',end)+1
    return text[m.start():end]
signature=r'^(?:static\s+)?(?:void|bool|u8|u16|u32|s8|s16|s32|int|enum \w+) '
src=texts['rtw8852b.c']+'\n'+texts['rtw8852b_rfk.c']
pool={n:block(src,signature+n+r'\([^;]+?\n\{') for n in re.findall(signature+r'(\w+)\(',src,re.M)}
for f,n in [('phy.c','rtw89_phy_get_txsc'),('phy.c','rtw89_encode_chan_idx'),
            ('phy.h','rtw89_subband_to_bb_gain_band'),('phy.h','rtw89_subband_to_gain_offset_band_of_ofdm'),
            ('mac.c','rtw89_mac_cfg_ppdu_status_ax')]:
    pool[n]=block(texts[f],signature+n+r'\([^;]+?\n\{')
selected=set()
def visit(n):
    if n in selected:return
    selected.add(n)
    for t in re.findall(r'\b\w+\b',pool[n]):
        if t in pool and t!=n:visit(t)
roots=['rtw8852b_set_channel','rtw8852b_set_channel_help','rtw89_mac_cfg_ppdu_status_ax']
for n in roots:visit(n)
original={n:v for n,v in pool.items() if n in selected}
enums='\n'.join(block(texts['core.h'],r'^enum '+n+r' \{',True)
                for n in ['rtw89_mac_idx','rtw89_sc_offset','rtw89_phy_bb_gain_band'])
enums+='\n'+block(texts['mac.h'],r'^enum rtw89_mac_hwmod_sel \{',True)
# PPDU destination is imported from the reg.h macro along with its field mask.
structs=block(src,r'^struct rtw8852b_bb_gain \{',True)
structs+='\n'+block(texts['core.h'],r'^struct rtw89_channel_help_params \{',True)
tokens=set(re.findall(r'\b\w+\b','\n'.join(original.values())))
arrays={}
array_src=src+'\n'+texts['phy.c']
for n in re.findall(r'^(?:static\s+)?const (?:u8|u32|struct rtw8852b_bb_gain) (\w+)\[',array_src,re.M):
    if n in tokens:arrays[n]=block(array_src,r'^(?:static\s+)?const (?:u8|u32|struct rtw8852b_bb_gain) '+n+r'(?:\[[^\]]*\])+ = \{',True)
macros={}
for t in texts.values():
    for m in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',t.replace('\\\n',' '),re.M):macros[m[1]]=m[2].strip()
shared=(root/'src/network/Rtw8852bRfkConstants.hpp').read_text()
shared_names=set(re.findall(r'constexpr u32 (\w+) =',shared))
ordered=[];seen=set()
def take(n):
    if n in seen or n in shared_names or n not in macros:return
    seen.add(n)
    for t in re.findall(r'\b\w+\b',macros[n]):take(t)
    ordered.append(n)
for t in re.findall(r'\b\w+\b','\n'.join(original.values())+'\n'.join(arrays.values())+enums+structs):take(t)
def adapt(t):
    t=t.replace('struct rtw89_dev','Context').replace('enum rtw89_rf_path path','u8 path')
    t=t.replace('struct rtw89_phy_bb_gain_info','network::BasebandGain').replace('rtwdev->bb_gain.ax','rtwdev->gain')
    t=t.replace('enum rtw89_subband subband','u8 subband')
    t=t.replace('rtwdev->hw->conf.flags & IEEE80211_CONF_MONITOR','rtwdev->monitor')
    for a,b in [('BIT','bit'),('GENMASK','mask'),('FIELD_GET','fieldGet'),('FIELD_PREP','fieldPrep'),('ARRAY_SIZE','arraySize')]:
        t=re.sub(r'\b'+a+r'\(',b+'(',t)
    t=t.replace('clamp_t(s32,','clampS32(').replace('S8_MIN','(-128)').replace('S8_MAX','127')
    t=re.sub(r'\b(tmp|offset_a|offset_b|offset_ofdm|offset_cck) << (\d+)',r'\1 * (1 << \2)',t)
    t=re.sub(r'\b(?:udelay|fsleep)\(([^;]+)\)',r'delay(rtwdev,\1)',t)
    t=re.sub(r'\bread_poll_timeout(?:_atomic)?\(','R16_CHANNEL_POLL(',t)
    t=t.replace('fallthrough;', '/* fall through */')
    return t
def function(n,s):
    t=adapt(s)
    if not t.startswith('static'):t='static '+t
    if n=='rtw8852b_set_gain_error':t=t.replace('\tint i;', '\tunsigned i;')
    if n=='rtw8852b_set_channel_help':
        # The adapter acquired an acknowledged scheduler/BT lease before this.
        # Neither phase may resume TX; power and RFK are controller prerequisites.
        for line in ['\t\trtw89_chip_stop_sch_tx(rtwdev, RTW89_MAC_0, &p->tx_en, RTW89_SCH_TX_SEL_ALL);\n',
                     '\t\trtw89_chip_resume_sch_tx(rtwdev, RTW89_MAC_0, p->tx_en);\n']:
            assert t.count(line)==1;t=t.replace(line,'')
    if n=='_lck_check':
        t=t[:-1]+'\tif (check(rtwdev) && rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) != 1)\n\t\tfail(rtwdev,Error::pllUnlocked);\n}'
    return t
notice='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation\n// Generated from pinned rtw89 '+commit+'; BSD option. Do not edit.\n'
header=notice+'#pragma once\n#include "Rtw8852bRfkConstants.hpp"\nnamespace rtl8852be { namespace channel {\nusing namespace rfk;\n'
for n in ordered:header+='constexpr u32 '+n+' = '+adapt(macros[n])+';\n'
header+=enums+'\n'+structs+'\n'
for n,a in arrays.items():
    a=re.sub(r'\.(?:gain_g|gain_a|gain_mask)\s*=\s*','',adapt(a))
    if not a.startswith('static'):a='static '+a
    header+=a+'\n'
header+='inline bool macByteAddress(u32 a){return a==R_AX_WMAC_RFMOD||a==R_AX_TXRATE_CHK;}\n'
header+='inline bool macWordAddress(u32 a,bool write){return a==R_AX_TX_SUB_CARRIER_VALUE||a==R_AX_PPDU_STAT||a==R_AX_HW_RPT_FWD||(!write&&a==R_AX_CMAC_FUNC_EN);}\n'
header+='} }\n'
dest=root/'src/network'
(dest/'Rtw8852bChannelConstants.hpp').write_bytes(header.encode())
(dest/'Rtw8852bChannelFunctions.inc').write_bytes((notice+'\n\n'.join(function(n,s) for n,s in original.items())+'\n').encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option','roots':roots,
        'original_functions':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in original.items()},
        'original_arrays':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in arrays.items()},
        'sources':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in texts.items()},
        'adaptations':['typed bounded Context','scheduler pause belongs to mandatory parent lease; never resume TX here',
                       'bounded failure-latching I/O/polls','verify terminal synthesizer lock','signed gain shifts use multiplication',
                       'monitor flag passed as boolean; only MAC0/PHY0 supported','shared pinned RFK numeric constants'],
        'generated':{n:hashlib.sha256((dest/n).read_bytes()).hexdigest() for n in ['Rtw8852bChannelConstants.hpp','Rtw8852bChannelFunctions.inc']},
        'hardware_tested':False,'power_limits_implemented':False}
(dest/'channel-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported',len(original),'channel functions,',len(arrays),'arrays,',len(ordered),'additional constants')
