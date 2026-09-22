#!/usr/bin/env python3
"""Import pinned RTL8852B power data and AX page-fill algorithms, without C designators."""
import ast, hashlib, json, math, pathlib, re, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*args): return subprocess.check_output(['git','-C',str(source),*args])
assert git('rev-parse','HEAD').decode().strip()==commit
files=['rtw8852b_table.c','rtw8852b.c','phy.c','phy.h','core.h','reg.h']
assert not git('diff','HEAD','--',*files)
texts={f:git('show','HEAD:'+f).decode() for f in files}
def block(text, pattern):
    m=re.search(pattern,text,re.M); assert m,pattern
    p=text.index('{',m.start()); end=p+1; depth=1
    while depth: depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[m.start():end]
def uncomment(s): return re.sub(r'/\*.*?\*/','',s,flags=re.S)
macros={m[1]:uncomment(m[2]).strip() for t in texts.values() for m in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',t,re.M)}
values={}
def number(expr):
    def calc(n):
        if isinstance(n,ast.Constant) and type(n.value)==int:return n.value
        if isinstance(n,ast.Name):
            if n.id not in values:values[n.id]=number(macros[n.id])
            return values[n.id]
        if isinstance(n,ast.BinOp):
            a,b=calc(n.left),calc(n.right)
            ops={ast.Add:lambda:a+b,ast.Sub:lambda:a-b,ast.LShift:lambda:a<<b,ast.BitOr:lambda:a|b}
            return ops[type(n.op)]()
        if isinstance(n,ast.UnaryOp) and isinstance(n.op,ast.USub):return -calc(n.operand)
        if isinstance(n,ast.Call) and isinstance(n.func,ast.Name):
            a=[calc(x) for x in n.args]
            if n.func.id=='GENMASK' and len(a)==2:return ((1<<(a[0]+1))-1)^((1<<a[1])-1)
            if n.func.id=='BIT' and len(a)==1:return 1<<a[0]
        raise ValueError(ast.dump(n))
    return calc(ast.parse(expr.strip(),mode='eval').body)
enum_names=['rtw89_band','rtw89_bandwidth','rtw89_rate_section','rtw89_rate_offset_indexes','rtw89_rate_num','rtw89_nss','rtw89_ntx','rtw89_beamforming_type','rtw89_regulation_type','rtw89_ru_bandwidth']
enum_sources=[]
for f,name in [('core.h',n) for n in enum_names]+[('phy.h','rtw89_bandwidth_section_num_ax')]:
    s=block(texts[f],r'^enum '+name+r' \{'); enum_sources.append(s)
    last=-1
    for item in uncomment(s[s.index('{')+1:-1]).split(','):
        item=item.strip()
        if not item:continue
        parts=item.split('=',1);last=number(parts[1]) if len(parts)==2 else last+1;values[parts[0].strip()]=last
notice='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation\n// Generated from pinned rtw89 '+commit+'; BSD option. Do not edit.\n'
header=notice+'#pragma once\n#include <stdint.h>\nnamespace rtl8852be { namespace power {\nusing u8=uint8_t;using s8=int8_t;using u32=uint32_t;\n'
header+='\n'.join(s+';' for s in enum_sources)+'\n'
registers=['R_AX_PWR_BY_RATE','R_AX_PWR_RATE_OFST_CTRL','R_AX_PWR_LMT','R_AX_PWR_RU_LMT','R_AX_PWR_RATE_CTRL','B_AX_PWR_REF','R_TXFIR0','R_DCFO_OPT','B_TXSHAPE_TRIANGULAR_CFG','B_DPD_TSSI_CW','B_DPD_PWR_CW','B_DPD_REF']
for n in registers:header+='constexpr u32 '+n+' = '+hex(number(n))+';\n'
for n in ['RTW89_RU_SEC_NUM_AX','RTW89_TXPWR_LMT_PAGE_SIZE_AX','RTW89_TXPWR_LMT_RU_PAGE_SIZE_AX']:
    header+='constexpr unsigned '+n+' = '+str(number(n))+';\n'
for n in ['rtw89_txpwr_limit_ax','rtw89_txpwr_limit_ru_ax']:header+=block(texts['phy.h'],r'^struct '+n+r' \{')+';\n'
cpp=notice+'#include "Rtw8852bPowerTables.hpp"\nnamespace rtl8852be { namespace power {\n'
original={};records={}
for name in ['tx_shape_lmt','tx_shape_lmt_ru','txpwr_lmt_2g','txpwr_lmt_5g','txpwr_lmt_ru_2g','txpwr_lmt_ru_5g']:
    s=block(texts['rtw8852b_table.c'],r'^const [us]8 rtw89_8852b_'+name+r'\[');original[name]=s
    dimensions=[number(v) for v in re.findall(r'\[([^\]]+)\]',s[:s.index('=')])]
    data=[0]*math.prod(dimensions);seen=set();body=uncomment(s[s.index('{')+1:-1])
    for line in body.splitlines():
        if not line.strip():continue
        m=re.fullmatch(r'\s*((?:\[[^\]]+\])+)[ \t]*=[ \t]*(-?\d+),?\s*',line);assert m,line
        indices=[number(x) for x in re.findall(r'\[([^\]]+)\]',m[1])];assert len(indices)==len(dimensions)
        offset=0
        for i,d in zip(indices,dimensions):assert 0<=i<d;offset=offset*d+i
        assert offset not in seen;seen.add(offset);v=int(m[2]);assert -128<=v<=127;data[offset]=v
    records[name]={'dimensions':dimensions,'explicit_entries':len(seen),'dense_sha256':hashlib.sha256(bytes(v&255 for v in data)).hexdigest()}
    header+='extern const s8 '+name+'['+str(len(data))+'];\n'
    header+='constexpr unsigned '+name+'_dimensions[] = {'+','.join(map(str,dimensions))+'};\n'
    cpp+='const s8 '+name+'['+str(len(data))+'] = {\n'
    cpp+='\n'.join(','.join(map(str,data[i:i+32]))+',' for i in range(0,len(data),32))+'\n};\n'
s=block(texts['rtw8852b_table.c'],r'^static const struct rtw89_txpwr_byrate_cfg rtw89_8852b_txpwr_byrate\[\] = \{');original['byrate']=s
# Normal AX has two RF paths. Missing entries retain upstream's zero initialization.
data=[0]*(2*2*5*12);seen=set()
for row in re.findall(r'\{([^{}]+)\}',s):
    band,nss,rs,shift,length,word=[number(x) for x in row.split(',') if x.strip()]
    assert band<2 and nss<2 and rs<5 and 1<=length<=4 and shift+length<=12
    for i in range(length):
        offset=((band*2+nss)*5+rs)*12+shift+i;assert offset not in seen;seen.add(offset)
        v=(word>>(8*i))&255;data[offset]=v if v<128 else v-256
header+='extern const s8 byrate[240];\nextern const u32 dfir[3][8];\n'
cpp+='const s8 byrate[240] = {\n'+ '\n'.join(','.join(map(str,data[i:i+24]))+',' for i in range(0,len(data),24))+'\n};\n'
dfir=block(texts['rtw8852b.c'],r'^static void rtw8852b_bb_set_tx_shape_dfir\(');original['dfir']=dfir
cpp+='const u32 dfir[3][8] = {\n'
for n in ['flat','sharp','sharp_14']:
    vals=re.search(r'__DECL_DFIR_PARAM\('+n+r',([^;]+)\);',dfir)[1]
    assert len(vals.split(','))==8;cpp+='{'+','.join(hex(number(x)) for x in vals.split(','))+'},\n'
cpp+='};\n} }\n'
functions=[]
for kind in ['limit','limit_ru']:
    for width in [20,40,80]:
        n='rtw89_phy_fill_txpwr_'+kind+'_'+str(width)+'m_ax'
        s=block(texts['phy.c'],r'^static (?:void\s*\n|void )'+n+r'\(');original[n]=s
        s=s.replace('struct rtw89_dev','Context').replace('__fill_txpwr_limit_nonbf_bf(', 'fillPair(rtwdev,').replace('min_t(s8,','minimum(')
        functions.append(s)
header+='inline bool macAddress(u32 a){return a==R_AX_PWR_RATE_CTRL||a==R_AX_PWR_RATE_OFST_CTRL||(!(a&3)&&((a>=R_AX_PWR_BY_RATE&&a<R_AX_PWR_BY_RATE+44)||(a>=R_AX_PWR_LMT&&a<R_AX_PWR_LMT+80)||(a>=R_AX_PWR_RU_LMT&&a<R_AX_PWR_RU_LMT+48)));}\n} }\n'
dest=root/'src/network'
outputs={'Rtw8852bPowerTables.hpp':header,'Rtw8852bPowerTables.cpp':cpp,'Rtw8852bPowerFunctions.inc':notice+'\n\n'.join(functions)+'\n'}
for n,s in outputs.items():(dest/n).write_bytes(s.encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option','sources':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in texts.items()},'original':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in original.items()},'tables':records,'generated':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in outputs.items()},'adaptations':['C designated sparse tables flattened with exact zero initialization','AX byrate expanded to two RF paths, preserving missing zeros','20/40/80 MHz page-fill functions use bounded lookup Context','No 6 GHz or 160 MHz programming on RTL8852B'],'hardware_tested':False}
(dest/'power-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported',sum(r['explicit_entries'] for r in records.values()),'power/shape entries and',len(functions),'page-fill functions')
