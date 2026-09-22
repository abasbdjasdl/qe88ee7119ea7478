#!/usr/bin/env python3
"""Import RTL8852B initial, IQ and TSSI calibration dependency closures."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*a):return subprocess.check_output(['git','-C',str(source),*a])
assert git('rev-parse','HEAD').decode().strip()==commit
files=['rtw8852b_rfk.c','rtw8852b_rfk_table.c','rtw8852b.c','rtw8852b_table.c','rtw8852b.h','core.h','phy.h','reg.h','coex.h']
assert not git('diff','HEAD','--',*files)
texts={f:git('show','HEAD:'+f).decode() for f in files}
def block(text,pattern,semicolon=False):
    m=re.search(pattern,text,re.M);assert m,pattern
    start=m.start();p=text.index('{',m.end()-1);end=p+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    if semicolon:end=text.index(';',end)+1
    return text[start:end]
src=texts['rtw8852b_rfk.c']+'\n'+texts['rtw8852b.c']
signature=r'^(?:static )?(?:void|bool|u8|u16|u32|s8|s16|s32|int) '
all_names=re.findall(signature+r'(\w+)\(',src,re.M)
selected=set()
def visit(n):
    if n in selected:return
    selected.add(n)
    body=block(src,signature+n+r'\([^;]+?\n\{')
    # Poll helpers pass a function identifier as an argument, without '('.
    for name in re.findall(r'\b\w+\b',body):
        if name in all_names and name!=n:visit(name)
roots=['_set_dpd_backoff','_rck','_dac_cal','_wait_rx_mode','_rx_dck','_iqk_init','_iqk']
roots+=['_tssi_disable','_tssi_rf_setting','_tssi_set_sys','_tssi_ini_txpwr_ctrl_bb',
        '_tssi_ini_txpwr_ctrl_bb_he_tb','_tssi_set_dck','_tssi_set_tmeter_tbl',
        '_tssi_set_dac_gain_tbl','_tssi_slope_cal_org','_tssi_alignment_default',
        '_tssi_set_tssi_slope','_tssi_alimentk','_tssi_enable','_tssi_set_efuse_to_de']
for n in roots:visit(n)
original={n:block(src,signature+n+r'\([^;]+?\n\{') for n in all_names if n in selected}
original['rtw89_subband_to_gain_offset_band_of_ofdm']=block(texts['phy.h'],r'^enum rtw89_gain_offset rtw89_subband_to_gain_offset_band_of_ofdm\([^;]+?\n\{')
table_names=sorted(set(re.findall(r'&(rtw8852b_\w+)_tbl','\n'.join(original.values()))))
tables={n:block(texts['rtw8852b_rfk_table.c'],r'^static const struct rtw89_reg5_def '+n+r'\[\] = \{',True) for n in table_names}
enums='\n'.join(block(texts['core.h'],r'^enum '+n+r' \{',True) for n in ['rtw89_rf_path','rtw89_rf_path_bit','rtw89_phy_idx','rtw89_sub_entity_idx','rtw89_band','rtw89_bandwidth','rtw89_subband','rtw89_gain_offset','rtw89_tssi_alimk_band'])
enums+='\n'+block(src,r'^enum rtw8852b_iqk_type \{',True)
enums+='\n'+block(texts['rtw8852b.h'],r'^enum rtw8852b_pmac_mode \{',True)
enums+='\n'+'\n'.join(block(texts['coex.h'],r'^enum '+n+r' \{',True) for n in ['btc_wl_rfk_type','btc_wl_rfk_state'])
structs='\n'.join(block(texts['core.h'],r'^struct '+n+r' \{',True) for n in ['rtw89_dack_info','rtw89_iqk_info','rtw89_tssi_info','rtw89_phy_efuse_gain'])
structs+='\n'+block(texts['phy.h'],r'^struct rtw89_txpwr_track_cfg \{',True)
structs+='\n'+'\n'.join(block(texts['rtw8852b.h'],r'^struct '+n+r' \{',True) for n in ['rtw8852b_bb_pmac_info','rtw8852b_bb_tssi_bak'])
array_names=re.findall(r'^static const (?:u32|struct rtw89_reg3_def) (\w+)\[',src,re.M)
tokens=set(re.findall(r'\b\w+\b','\n'.join(original.values())))
arrays={n:block(src,r'^static const (?:u32|struct rtw89_reg3_def) '+n+r'(?:\[[^\]]*\])+ = \{',True) for n in array_names if n in tokens}
track_src=texts['rtw8852b_table.c']
track_names=re.findall(r'^static const s8 (_txpwr_track_delta_swingidx_\w+)\[',track_src,re.M)
for n in track_names:arrays[n]=block(track_src,r'^static const s8 '+n+r'(?:\[[^\]]*\])+ = \{',True)
track_config=block(track_src,r'^const struct rtw89_txpwr_track_cfg rtw89_8852b_trk_cfg = \{',True)
macros={}
for t in texts.values():
    for m in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',t.replace('\\\n',' '),re.M):macros[m[1]]=m[2].strip()
ordered=[];seen=set()
def take(n):
    if n in seen or n not in macros:return
    seen.add(n)
    for d in re.findall(r'\b[A-Za-z_]\w*\b',macros[n]):take(d)
    ordered.append(n)
for token in re.findall(r'\b[A-Za-z_]\w*\b','\n'.join(original.values())+'\n'.join(tables.values())+'\n'.join(arrays.values())+enums+structs):take(token)
for token in ['R_AX_WCPU_FW_CTRL','B_AX_WCPU_FWDL_STS_MASK','R_AX_CMAC_FUNC_EN','B_AX_CMAC_EN',
              'R_AX_SYS_FUNC_EN','B_AX_FEN_BBRSTB','B_AX_FEN_BB_GLB_RSTN','R_AX_CTN_TXEN','B_AX_CTN_TXEN_ALL_MASK',
              'BTC_RFK_PATH_MAP','BTC_RFK_PHY_MAP','BTC_RFK_BAND_MAP']:
    take(token)
def adapt(t):
    t=t.replace('struct rtw89_dev','Context').replace('enum rtw89_rf_path path','u8 path')
    t=re.sub(r'\bBIT\(','bit(',t);t=re.sub(r'\bGENMASK\(','mask(',t)
    t=re.sub(r'\bFIELD_GET\(','fieldGet(',t)
    t=re.sub(r'\bFIELD_PREP\(','fieldPrep(',t)
    t=t.replace('clamp_t(s32,','clampS32(').replace('S8_MIN','(-128)').replace('S8_MAX','127')
    t=t.replace('ktime_get_ns()', '(rtwdev->io.nowUs()*1000)')
    for old,new in [('TSSI_EXTRA_GROUP','tssiExtraGroup'),('IS_TSSI_EXTRA_GROUP','isTssiExtraGroup'),
                    ('TSSI_EXTRA_GET_GROUP_IDX1','tssiExtraIndex1'),('TSSI_EXTRA_GET_GROUP_IDX2','tssiExtraIndex2')]:
        t=re.sub(r'\b'+old+r'\(',new+'(',t)
    t=re.sub(r'\bARRAY_SIZE\(','arraySize(',t)
    t=re.sub(r'\btry\b','attemptLimit',t)
    t=re.sub(r'\budelay\(([^;]+)\)',r'delay(rtwdev,\1)',t)
    t=re.sub(r'\bmdelay\(([^;]+)\)',r'delay(rtwdev,1000*(\1))',t)
    t=re.sub(r'\bread_poll_timeout(?:_atomic)?\(','R16_RFK_POLL(',t)
    return t
def adapt_function(name,text):
    t=adapt(text)
    if not t.startswith('static '):t='static '+t
    if name=='rtw8852b_start_pmac_tx':t=t.replace('= tx_info->mode;', '= static_cast<rtw8852b_pmac_mode>(tx_info->mode);')
    if name=='rtw8852b_bb_set_pmac_pkt_tx':t=t.replace('tx_info = {0};','tx_info = {};')
    if name=='rtw8852b_set_gain_offset':
        # Linux relies on signed shifts; multiplication is defined for these
        # bounded signed efuse values in C++ and gives the same register bits.
        t=re.sub(r'\b(tmp|offset_a|offset_b|offset_ofdm|offset_cck) << (\d+)',r'\1 * (1 << \2)',t)
    if name=='_tssi_set_tmeter_tbl':
        t=re.sub(r'#define RTW8852B_TSSI_GET_VAL[\s\S]+?\n\}\)', '', t)
        t=t.replace('RTW8852B_TSSI_GET_VAL(', 'thermalWord(').replace('#undef RTW8852B_TSSI_GET_VAL','')
    if name=='_tssi_hw_tx':
        # Native backend records possible TX before the first programming write.
        t=t.replace('\tif (enable) {', '\tif (enable) {\n\t\tif (!check(rtwdev) || !rtwdev->io.armCalibrationTx()) { fail(rtwdev,Error::io); return; }')
        t=t.replace('\trtw8852b_bb_set_pmac_pkt_tx(rtwdev, enable, cnt, period, 20, phy);',
                    '\tif (enable) rtw8852b_bb_set_pmac_pkt_tx(rtwdev, enable, cnt, period, 20, phy);\n'
                    '\telse {\n\t\tif (!rtwdev->io.stopCalibrationTx()) fail(rtwdev,Error::io);\n'
                    '\t\tif (check(rtwdev)) rtw8852b_bb_set_pmac_pkt_tx(rtwdev, false, cnt, period, 20, phy);\n\t}')
    if name=='_tssi_get_cw_report':
        t=t.replace('int j, k;', 'unsigned j; int k;')
        t=t.replace('\t\tif (k >= retry) {','\t\tif (k >= retry) {\n\t\t\tfail(rtwdev,Error::timeout);')
    if name=='_iqk_by_path':
        t=t.replace('\tif (lok_is_fail)\n','\trtwdev->iqk.lok_fail[path] = lok_is_fail;\n\tif (lok_is_fail)\n')
    if name=='_iqk_restore':
        t=t.replace('fail = _iqk_check_cal(rtwdev, path);','fail = _iqk_check_cal(rtwdev, path);\n\trtwdev->restoreFailed[path] = fail;')
    if name=='_iqk_lok':
        # Neither VBUFFER result is retained upstream. Both are prerequisites
        # for valid LOK; include them in the existing three-attempt retry.
        t=t.replace('\tbool tmp;', '\tbool tmp, vbuffer_fail;')
        t=t.replace('tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);',
                    'vbuffer_fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);')
        t=t.replace('\n\t_iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);',
                    '\n\tvbuffer_fail |= _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);')
        t=t.replace('return _lok_finetune_check(rtwdev, path);',
                    'return _lok_finetune_check(rtwdev, path) | vbuffer_fail |\n'
                    '\t       iqk_info->lok_cor_fail[0][path] | iqk_info->lok_fin_fail[0][path];')
    return t
notice='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation\n// Generated from pinned rtw89 by tools/import_rfk_reference.py.\n'
header=notice+'#pragma once\n#include <stdint.h>\n#include <stddef.h>\nnamespace rtl8852be { namespace rfk {\n'
header+='using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using s8=int8_t;using s16=int16_t;using s32=int32_t;\n'
header+='constexpr u32 bit(unsigned n){return u32(1)<<n;}\nconstexpr u32 mask(unsigned hi,unsigned lo){return (u32(0xffffffff)>>(31-hi))&(u32(0xffffffff)<<lo);}\n'
header+='constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}\nconstexpr u32 fieldGet(u32 m,u32 v){return (v&m)>>shift(m);}\n'
header+='constexpr u32 fieldPrep(u32 m,u32 v){return (v<<shift(m))&m;}\nconstexpr s32 clampS32(s32 v,s32 low,s32 high){return v<low?low:v>high?high:v;}\n'
header+='constexpr u32 tssiExtraGroup(u32 n){return bit(31)|n;}\nconstexpr bool isTssiExtraGroup(u32 n){return n&bit(31);}\nconstexpr u32 tssiExtraIndex1(u32 n){return n&~bit(31);}\nconstexpr u32 tssiExtraIndex2(u32 n){return tssiExtraIndex1(n)+1;}\n'
header+='inline u32 thermalWord(const s8 *p,unsigned i){u32 v=0;for(unsigned j=0;j<4;++j)v|=u32(u8(p[i+j]))<<(j*8);return v;}\n'
header+='constexpr s32 sign_extend32(u32 v,unsigned sign){return (v&(u32(1)<<sign))?s32(v&((u32(1)<<sign)-1))-s32(u32(1)<<sign):s32(v);}\n'
header+='template<class T,size_t N> constexpr size_t arraySize(const T (&)[N]){return N;}\n'
header+=adapt(enums)+'\n'
late=[n for n in ordered if 'ARRAY_SIZE' in macros[n]]
header+='\n'.join('constexpr u32 '+n+' = '+adapt(macros[n])+';' for n in ordered if n not in late)+'\n'
header+=structs+'\nstruct rtw89_reg3_def {u32 addr,mask,data;};\nstruct rtw89_reg5_def {u8 flag,path;u32 addr,mask,data;};\nstruct rtw89_rfk_tbl {const rtw89_reg5_def *defs;u32 size;};\n'
header+='\n'.join(adapt(t) for t in arrays.values())+'\n'
track_fields=re.findall(r'\(\*(\w+)\)|\*(\w+);',block(texts['phy.h'],r'^struct rtw89_txpwr_track_cfg \{',True))
track_values=dict(re.findall(r'\.(\w+)\s*=\s*(\w+)',track_config))
header+='static const rtw89_txpwr_track_cfg rtw89_8852b_trk_cfg={'+','.join(track_values.get(a or b,'nullptr') for a,b in track_fields)+'};\n'
header+='\n'.join('constexpr u32 '+n+' = '+adapt(macros[n])+';' for n in late)+'\n'
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
(dest/'Rtw8852bRfkFunctions.inc').write_bytes((notice+'\n\n'.join(adapt_function(n,f) for n,f in original.items())+'\n').encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option',
    'roots':roots,
    'original_functions':{n:hashlib.sha256(b.encode()).hexdigest() for n,b in original.items()},
    'original_tables':{n:hashlib.sha256(b.encode()).hexdigest() for n,b in tables.items()},
    'original_arrays':{n:hashlib.sha256(b.encode()).hexdigest() for n,b in arrays.items()},
    'original_track_config_sha256':hashlib.sha256(track_config.encode()).hexdigest(),
    'sources':{n:hashlib.sha256(t.encode()).hexdigest() for n,t in texts.items()},
    'adaptations':['typed Context','RF path arguments accept bounded u8','bounded failure-latching polling and delays',
        'Linux bit/array/signed helpers replaced with local helpers','RFK table macros expanded in original order',
        'C try variable renamed for C++','LOK terminal failure and restore failure retained for public readiness checks',
        'LOK retries retain coarse/fine and both VBUFFER command failures',
        'TSSI PMAC start records TX ownership; mandatory backend stop bypasses normal I/O fault latch',
        'TSSI report timeout latches failure; signed gain shifts use bounded multiplication',
        'Thermal byte packing uses unsigned shifts; track configuration uses positional C++ initializer'],
    'generated':{n:hashlib.sha256((dest/n).read_bytes()).hexdigest() for n in ['Rtw8852bRfkConstants.hpp','Rtw8852bRfkFunctions.inc']},
    'hardware_tested':False,'full_rfk_implemented':False}
(dest/'rfk-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported',len(original),'RFK functions,',len(tables),'tables,',len(arrays),'arrays,',len(ordered),'constants')
