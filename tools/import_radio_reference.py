#!/usr/bin/env python3
"""Import RTL8852B register tables and packed eFuse layout without Linux code."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*args):
    return subprocess.check_output(['git','-c','safe.directory='+source.as_posix(),'-C',str(source),*args])
assert git('rev-parse','HEAD').decode().strip()==commit
names=('rtw8852b_table.c','rtw8852b.h','rtw8852b.c','phy.c','phy.h','efuse.c','core.h','reg.h')
assert not git('diff','HEAD','--',*names)
texts={n:git('show','HEAD:'+n).decode() for n in names}
def block(text,start):
    p=text.index(start);a=text.index('{',p);end=a+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[p:text.index(';',end)+1]
dest=root/'src/network'
notice='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation\n// Generated from pinned rtw89 by tools/import_radio_reference.py.\n'
tables=[];manifest={}
for name,suffix,kind,path in [('bb','bb_regs','baseband',0),('radioA','radioa_regs','radio',0),('radioB','radiob_regs','radio',1),('nctl','nctl_regs','baseband',0),('gain','bb_reg_gain','gain',0)]:
    original=block(texts['rtw8852b_table.c'],'static const struct rtw89_reg2_def rtw89_8852b_phy_'+suffix+'[]')
    rows=re.findall(r'\{\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\s*\}',original)
    assert rows and original.count('{')==len(rows)+1
    body=original[original.index('{'):]
    tables.append('static const RadioRegister '+name+'Registers[] = '+body+'\nconst RadioTable '+name+'Table{'+name+'Registers,sizeof('+name+'Registers)/sizeof(RadioRegister),RadioTableKind::'+kind+','+str(path)+'};\n')
    manifest[name]={'rows':len(rows),'original_block_sha256':hashlib.sha256(original.encode()).hexdigest()}
table_cpp=notice+'#include "RadioTables.hpp"\nnamespace rtl8852be { namespace network {\n'+'\n'.join(tables)+'} }\n'
(dest/'Rtw8852bRadioTables.cpp').write_bytes(table_cpp.encode())
structs=[block(texts['rtw8852b.h'],'struct '+n+' {') for n in ('rtw8852b_u_efuse','rtw8852b_e_efuse','rtw8852b_tssi_offset','rtw8852b_efuse')]
layout=notice+'#pragma once\n#include <stdint.h>\nnamespace rtl8852be { namespace reference {\nusing u8=uint8_t;\nconstexpr unsigned ETH_ALEN=6,TSSI_CCK_CH_GROUP_NUM=6,TSSI_MCS_2G_CH_GROUP_NUM=5,TSSI_MCS_5G_CH_GROUP_NUM=14;\n'
layout+='\n'.join(s.replace('__packed','__attribute__((packed))') for s in structs)+'\n} }\n'
(dest/'Rtw8852bEfuseLayout.hpp').write_bytes(layout.encode())
# Independent test oracle: preserve upstream selection/execution function bodies.
# Only the test harness supplies their small Linux type/macro environment.
def function(text,start):
    p=text.index(start);a=text.index('{',p);end=a+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[p:end]
oracle=notice+'\n'.join(function(texts['phy.c'],name) for name in (
    'static int rtw89_phy_sel_headline(', 'static void rtw89_phy_init_reg('))+'\n'
oracle+=block(texts['phy.c'],'union rtw89_phy_bb_gain_arg {').replace('__packed','__attribute__((packed))')+'\n'
oracle+=block(texts['phy.c'],'enum rtw89_phy_bb_rxsc_start_idx {')+'\n'
oracle+='\n'.join(function(texts['phy.c'],name) for name in (
    'static void\nrtw89_phy_cfg_bb_gain_error(', 'static void\nrtw89_phy_cfg_bb_rpl_ofst(',
    'static void\nrtw89_phy_cfg_bb_gain_bypass(', 'static void\nrtw89_phy_cfg_bb_gain_op1db(',
    'static void rtw89_phy_config_bb_gain_ax('))+'\n'
(root/'tests/network_radio_reference.inc').write_bytes(oracle.encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option',
        'tables':manifest,'sources':{n:hashlib.sha256(texts[n].encode()).hexdigest() for n in names},
        'generated':{n:hashlib.sha256((dest/n).read_bytes()).hexdigest() for n in ('Rtw8852bRadioTables.cpp','Rtw8852bEfuseLayout.hpp')},
        'test_oracle_sha256':hashlib.sha256(oracle.encode()).hexdigest(),'hardware_tested':False}
(dest/'radio-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported original RTL8852B tables:',{n:v['rows'] for n,v in manifest.items()})
