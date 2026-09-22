#!/usr/bin/env python3
"""Import the dependency closure of RTL8852B initial calibration routines."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*a):return subprocess.check_output(['git','-C',str(source),*a])
assert git('rev-parse','HEAD').decode().strip()==commit
files=['rtw8852b_rfk.c','rtw8852b_rfk_table.c','rtw8852b.h','core.h','phy.h','reg.h']
assert not git('diff','HEAD','--',*files)
texts={f:git('show','HEAD:'+f).decode() for f in files}
def block(text,pattern,semicolon=False):
    m=re.search(pattern,text,re.M);assert m,pattern
    start=m.start();p=text.index('{',m.end()-1);end=p+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    if semicolon:end=text.index(';',end)+1
    return text[start:end]
src=texts['rtw8852b_rfk.c']
all_names=re.findall(r'^static (?:void|bool|u8|u32|int) (\w+)\(',src,re.M)
selected=set()
def visit(n):
    if n in selected:return
    selected.add(n)
    body=block(src,r'^static (?:void|bool|u8|u32|int) '+n+r'\([^;]+?\n\{')
    # Poll helpers pass a function identifier as an argument, without '('.
    for name in re.findall(r'\b\w+\b',body):
        if name in all_names and name!=n:visit(name)
for n in ['_set_dpd_backoff','_rck','_dac_cal','_wait_rx_mode','_rx_dck']:visit(n)
original={n:block(src,r'^static (?:void|bool|u8|u32|int) '+n+r'\([^;]+?\n\{') for n in all_names if n in selected}
table_names=sorted(set(re.findall(r'&(rtw8852b_\w+)_tbl','\n'.join(original.values()))))
tables={n:block(texts['rtw8852b_rfk_table.c'],r'^static const struct rtw89_reg5_def '+n+r'\[\] = \{',True) for n in table_names}
enums='\n'.join(block(texts['core.h'],r'^enum '+n+r' \{',True) for n in ['rtw89_rf_path','rtw89_rf_path_bit','rtw89_phy_idx'])
structs=block(texts['core.h'],r'^struct rtw89_dack_info \{',True)
macros={}
for t in texts.values():
    for m in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',t.replace('\\\n',' '),re.M):macros[m[1]]=m[2].strip()
ordered=[];seen=set()
def take(n):
    if n in seen or n not in macros:return
    seen.add(n)
    for d in re.findall(r'\b[A-Za-z_]\w*\b',macros[n]):take(d)
    ordered.append(n)
for token in re.findall(r'\b[A-Za-z_]\w*\b','\n'.join(original.values())+'\n'.join(tables.values())+enums+structs):take(token)
for token in ['R_AX_WCPU_FW_CTRL','B_AX_WCPU_FWDL_STS_MASK','R_AX_CMAC_FUNC_EN','B_AX_CMAC_EN',
              'R_AX_SYS_FUNC_EN','B_AX_FEN_BBRSTB','B_AX_FEN_BB_GLB_RSTN','R_AX_CTN_TXEN','B_AX_CTN_TXEN_ALL_MASK']:
    take(token)
def adapt(t):
    t=t.replace('struct rtw89_dev','Context').replace('enum rtw89_rf_path path','u8 path')
    t=re.sub(r'\bBIT\(','bit(',t);t=re.sub(r'\bGENMASK\(','mask(',t)
    t=re.sub(r'\bFIELD_GET\(','fieldGet(',t)
    t=re.sub(r'\budelay\(([^;]+)\)',r'delay(rtwdev,\1)',t)
    t=re.sub(r'\bmdelay\(([^;]+)\)',r'delay(rtwdev,1000*(\1))',t)
    t=re.sub(r'\bread_poll_timeout(?:_atomic)?\(','R16_RFK_POLL(',t)
    return t
notice='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation\n// Generated from pinned rtw89 by tools/import_rfk_reference.py.\n'
header=notice+'#pragma once\n#include <stdint.h>\nnamespace rtl8852be { namespace rfk {\n'
header+='using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using s32=int32_t;\n'
header+='constexpr u32 bit(unsigned n){return u32(1)<<n;}\nconstexpr u32 mask(unsigned hi,unsigned lo){return (u32(0xffffffff)>>(31-hi))&(u32(0xffffffff)<<lo);}\n'
header+='constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}\nconstexpr u32 fieldGet(u32 m,u32 v){return (v&m)>>shift(m);}\n'
header+='constexpr s32 sign_extend32(u32 v,unsigned sign){return (v&(u32(1)<<sign))?s32(v&((u32(1)<<sign)-1))-s32(u32(1)<<sign):s32(v);}\n'
header+=adapt(enums)+'\n'
header+='\n'.join('constexpr u32 '+n+' = '+adapt(macros[n])+';' for n in ordered)+'\n'
header+=structs+'\nstruct rtw89_reg5_def {u8 flag,path;u32 addr,mask,data;};\nstruct rtw89_rfk_tbl {const rtw89_reg5_def *defs;u32 size;};\n'
def split_args(text):
    args=[];start=depth=0
    for i,c in enumerate(text):
        depth+=(c=='(')-(c==')')
        if c==',' and depth==0:args.append(text[start:i].strip());start=i+1
    args.append(text[start:].strip());return args
for name,body in tables.items():
    rows=[]
    for line in body.splitlines():
        m=re.search(r'RTW89_DECL_RFK_(\w+)\((.*)\),',line)
        if not m:continue
        kind,args=m[1],split_args(m[2])
        if kind=='WRF':row=['0',*args]
        elif kind=='WM':row=['1','0',*args]
        elif kind in ('WS','WC'):row=['2' if kind=='WS' else '3','0',*args,'0']
        elif kind=='DELAY':row=['4','0','0','0',*args]
        else:raise ValueError(kind)
        assert len(row)==5
        rows.append('{'+','.join(row)+'}')
    assert rows,name
    header+='static const rtw89_reg5_def '+name+'[]={'+adapt(',\n'.join(rows))+'};\n'
    header+='static const rtw89_rfk_tbl '+name+'_tbl={'+name+',sizeof('+name+')/sizeof('+name+'[0])};\n'
header+='} }\n'
dest=root/'src/network'
(dest/'Rtw8852bRfkConstants.hpp').write_bytes(header.encode())
(dest/'Rtw8852bRfkFunctions.inc').write_bytes((notice+'\n\n'.join(adapt(f) for f in original.values())+'\n').encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option',
    'roots':['_set_dpd_backoff','_rck','_dac_cal','_wait_rx_mode','_rx_dck'],
    'original_functions':{n:hashlib.sha256(b.encode()).hexdigest() for n,b in original.items()},
    'original_tables':{n:hashlib.sha256(b.encode()).hexdigest() for n,b in tables.items()},
    'sources':{n:hashlib.sha256(t.encode()).hexdigest() for n,t in texts.items()},
    'adaptations':['typed Context','RF path arguments accept bounded u8','bounded failure-latching polling and delays',
        'Linux bit and signed helpers replaced with local helpers','RFK table macros expanded in original order'],
    'generated':{n:hashlib.sha256((dest/n).read_bytes()).hexdigest() for n in ['Rtw8852bRfkConstants.hpp','Rtw8852bRfkFunctions.inc']},
    'hardware_tested':False,'full_rfk_implemented':False}
(dest/'rfk-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported',len(original),'initial RFK functions,',len(tables),'tables,',len(ordered),'constants')
