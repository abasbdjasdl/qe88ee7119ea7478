#!/usr/bin/env python3
"""Import AX system/packet/CMAC initialization from the pinned BSD reference."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
def git(*args):return subprocess.check_output(['git','-C',str(source),*args])
assert git('rev-parse','HEAD').decode().strip()==commit
names=('mac.c','mac.h','core.h','reg.h','rtw8852b.c')
assert not git('diff','HEAD','--',*names)
texts={n:git('show','HEAD:'+n).decode() for n in names}
def block(text,pattern,semicolon=False):
    m=re.search(pattern,text,re.M);assert m,pattern
    begin=m.start();pos=text.index('{',m.end()-1);end=pos+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    if semicolon:end=text.index(';',end)+1
    return text[begin:end]
functions=['cmac_func_en_ax','dmac_func_en_ax','chip_func_en_ax','sys_init_ax',
    '_patch_ss2f_path','sta_sch_init_ax','mpdu_proc_init_ax','sec_eng_init_ax',
    'addr_cam_init_ax','scheduler_init_ax','rtw89_mac_typ_fltr_opt_ax',
    'rx_fltr_init_ax','_patch_dis_resp_chk','cca_ctrl_init_ax','nav_ctrl_init_ax',
    'spatial_reuse_init_ax','tmac_init_ax','trxptcl_init_ax','rst_bacam',
    'rmac_init_ax','cmac_com_init_ax','ptcl_init_ax','cmac_dma_init_ax','cmac_init_ax',
    'dle_func_en_ax','dle_clk_en_ax','dle_mix_cfg_ax','chk_dle_rdy_ax',
    'hfc_ch_cfg_chk','hfc_pub_info_chk','hfc_pub_cfg_chk','hfc_ch_ctrl','hfc_upd_ch_info',
    'hfc_pub_ctrl','hfc_get_mix_info_ax','hfc_mix_cfg_ax','hfc_func_en_ax',
    'rtw89_wdrls_imr_enable','rtw89_wsec_imr_enable','rtw89_mpdu_trx_imr_enable',
    'rtw89_sta_sch_imr_enable','rtw89_txpktctl_imr_enable','rtw89_wde_imr_enable',
    'rtw89_ple_imr_enable','rtw89_pktin_imr_enable','rtw89_dispatcher_imr_enable',
    'rtw89_cpuio_imr_enable','rtw89_bbrpt_imr_enable','rtw89_scheduler_imr_enable',
    'rtw89_ptcl_imr_enable','rtw89_cdma_imr_enable','rtw89_phy_intf_imr_enable',
    'rtw89_rmac_imr_enable','rtw89_tmac_imr_enable','enable_imr_ax','err_imr_ctrl_ax','set_host_rpr_ax']
original={n:block(texts['mac.c'],r'^static (?:int|void) '+n+r'\([^;]+?\n\{') for n in functions}
for n in ['rtw89_mac_write_xtal_si_ax','rtw89_mac_read_xtal_si_ax']:
    functions.append(n)
    original[n]=block(texts['mac.c'],r'^(?:static\s+)?int '+n+r'\([^;]+?\n\{')
functions.append('rtw8852b_mac_enable_bb_rf')
original[functions[-1]]=block(texts['rtw8852b.c'],r'^static int rtw8852b_mac_enable_bb_rf\([^;]+?\n\{')
enum_sources={'core.h':['rtw89_core_chip_id','rtw89_hci_type','rtw89_mac_idx','rtw89_cv','rtw89_dma_ch','rtw89_hcifc_mode','rtw89_host_rpr_mode'],
              'mac.h':['rtw89_machdr_frame_type','rtw89_mac_fwd_target','rtw89_mac_hwmod_sel','rtw89_mac_xtal_si_offset']}
enums='\n'.join(block(texts[f],r'^enum '+n+r' \{',True) for f,ns in enum_sources.items() for n in ns)
rrsr=block(texts['rtw8852b.c'],r'^static const struct rtw89_rrsr_cfgs rtw8852b_rrsr_cfgs = \{',True)
rrsr=rrsr.replace('.ref_rate = ','').replace('.rsc = ','')
struct_names=['rtw89_hfc_ch_cfg','rtw89_hfc_ch_info','rtw89_hfc_pub_cfg','rtw89_hfc_pub_info','rtw89_hfc_prec_cfg','rtw89_hfc_param','rtw89_page_regs','rtw89_imr_info']
structs='\n'.join(block(texts['core.h'],r'^struct '+n+r' \{',True) for n in struct_names)
table_names=['rtw8852b_hfc_chcfg_pcie','rtw8852b_hfc_pubcfg_pcie','rtw8852b_page_regs','rtw8852b_imr_info']
tables={n:block(texts['rtw8852b.c'],r'^static const struct \w+ '+n+r'(?:\[\])? = \{',True) for n in table_names}
# Convert the page-register designated initializer using STRUCT field order.
page_fields=re.findall(r'u32 (\w+);',block(texts['core.h'],r'^struct rtw89_page_regs \{',True))
page_values=dict(re.findall(r'\.(\w+)\s*=\s*(\w+)\s*,',tables['rtw8852b_page_regs']))
assert set(page_fields)==set(page_values)
tables['rtw8852b_page_regs']='static const rtw89_page_regs rtw8852b_page_regs = {'+','.join(page_values[n] for n in page_fields)+'};'
imr_fields=re.findall(r'u32 (\w+);',block(texts['core.h'],r'^struct rtw89_imr_info \{',True))
imr_values=dict(re.findall(r'\.(\w+)\s*=\s*(\w+)\s*,',tables['rtw8852b_imr_info']))
assert set(imr_fields)==set(imr_values)
tables['rtw8852b_imr_info']='static const rtw89_imr_info rtw8852b_imr_info = {'+','.join(imr_values[n] for n in imr_fields)+'};'
sizes={}
for n in ['wde_size7','ple_size6','wde_qt7','ple_qt18','ple_qt58','hfc_preccfg_pcie']:
    sizes[n]=re.search(r'\.'+n+r'\s*=\s*\{([^}]+)\}',texts['mac.c'])[1].strip()
size_text='\n'.join('constexpr u32 '+n+'[]={'+v+'};' for n,v in sizes.items() if n!='hfc_preccfg_pcie')
size_text+='\nstatic const rtw89_hfc_prec_cfg hfcPreccfg={'+sizes['hfc_preccfg_pcie']+'};'
macros={}
for text in texts.values():
    flat=text.replace('\\\n',' ')
    for match in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',flat,re.M):macros[match[1]]=match[2].strip()
def adapt(text):
    if text.startswith('int rtw89_mac_write_xtal_si_ax('):text='static '+text
    text=re.sub(r'\bstruct rtw89_dev\b','Context',text)
    text=re.sub(r'\bEINVAL\b','macInvalid',text)
    text=re.sub(r'\bEFAULT\b','macFault',text)
    text=re.sub(r'\bBIT\(','bit(',text)
    text=re.sub(r'\bGENMASK\(','mask(',text)
    text=re.sub(r'\bFIELD_PREP\(','fieldPrep(',text)
    text=re.sub(r'\bmin_t\(u32,','minValue<u32>(',text)
    text=re.sub(r'\bread_poll_timeout(?:_atomic)?\(','R16_MAC_POLL(',text)
    text=text.replace('rtw89_mac_write_xtal_si(', 'rtw89_mac_write_xtal_si_ax(')
    text=text.replace('rtwdev, i, RTW89_FWD_TO_HOST','rtwdev, static_cast<rtw89_machdr_frame_type>(i), RTW89_FWD_TO_HOST')
    return re.sub(r'^#define[^\n]+\n','',text,flags=re.M)
functions_text='\n\n'.join(adapt(original[n]) for n in functions)
needed=set();ordered=[]
def take(name):
    if name in needed or name not in macros:return
    needed.add(name)
    for dep in re.findall(r'\b[A-Za-z_]\w*\b',macros[name]):take(dep)
    ordered.append(name)
extras=' DEFAULT_AX_RX_FLTR B_AX_WDE_MIN_SIZE_MASK B_AX_WDE_MAX_SIZE_MASK B_AX_PLE_MIN_SIZE_MASK B_AX_PLE_MAX_SIZE_MASK'
extras+=' '+' '.join('R_AX_WDE_QTA'+str(i)+'_CFG' for i in [0,1,3,4])
extras+=' '+' '.join('R_AX_PLE_QTA'+str(i)+'_CFG' for i in range(11))
for token in re.findall(r'\b[A-Za-z_]\w*\b','\n'.join(original.values())+rrsr+structs+'\n'.join(tables.values())+size_text+extras):take(token)
notice='// SPDX-License-Identifier: BSD-3-Clause\n// Copyright(c) 2019-2022 Realtek Corporation\n// Generated by tools/import_mac_reference.py from pinned rtw89.\n'
constants=notice+'#pragma once\n#include <stdint.h>\nnamespace rtl8852be { namespace macinit {\n'
constants+='using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;\n'
constants+='constexpr u32 bit(unsigned n){return u32(1)<<n;}\nconstexpr u32 mask(unsigned hi,unsigned lo){return (uint32_t(0xffffffff)>>(31-hi))&(uint32_t(0xffffffff)<<lo);}\n'
constants+='constexpr unsigned shift(u32 m){return (m&1)?0:1+shift(m>>1);}\nconstexpr u32 u32_encode_bits(u32 v,u32 m){return (v<<shift(m))&m;}\n'
constants+='constexpr u32 fieldPrep(u32 m,u32 v){return u32_encode_bits(v,m);}\nconstexpr u32 u32_replace_bits(u32 o,u32 v,u32 m){return (o&~m)|u32_encode_bits(v,m);}\nconstexpr u16 u16_replace_bits(u16 o,u16 v,u16 m){return u16(u32_replace_bits(o,v,m));}\n'
constants+='constexpr u32 u32_get_bits(u32 v,u32 m){return (v&m)>>shift(m);}\n'
constants+='template<class T> constexpr T minValue(T a,T b){return a<b?a:b;}\nconstexpr int macInvalid=22,macFault=14;\n'+adapt(enums)+'\n'
constants+='\n'.join('constexpr u32 '+n+' = '+adapt(macros[n])+';' for n in ordered)+'\n'
constants+='struct rtw89_reg3_def {u32 addr,mask,data;};\nstruct rtw89_rrsr_cfgs {rtw89_reg3_def ref_rate,rsc;};\n'+rrsr+'\n'
constants+=adapt(structs)+'\n'+'\n'.join(tables.values())+'\n'+size_text+'\n'
constants+='struct rtw89_dle_size {u32 pge_size,lnk_pge_num,unlnk_pge_num;};\nstruct rtw89_dle_mem {const rtw89_dle_size *wde_size,*ple_size;};\n'
constants+='constexpr u32 macInitRegisters[]={'+','.join(n for n in ordered if n.startswith('R_AX_'))+'};\n} }\n'
dest=root/'src/network'
(dest/'Rtw8852bMacConstants.hpp').write_bytes(constants.encode())
(dest/'Rtw8852bMacFunctions.inc').write_bytes((notice+functions_text+'\n').encode())
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'license':'BSD-3-Clause option',
    'original_functions':{n:hashlib.sha256(v.encode()).hexdigest() for n,v in original.items()},
    'sources':{n:hashlib.sha256(t.encode()).hexdigest() for n,t in texts.items()},
    'adaptations':['rtw89_dev replaced by typed Context','Linux bit/min/poll helpers mapped to bounded local helpers',
        'C loop index cast to enum for C++','object macros changed to namespaced constexpr values',
        'designated RRSR, page-register and IMR initializers converted to structure field order',
        'XTAL write dispatch bound to the AX implementation and imported as a static member'],
    'generated':{n:hashlib.sha256((dest/n).read_bytes()).hexdigest() for n in ('Rtw8852bMacConstants.hpp','Rtw8852bMacFunctions.inc')},
    'hardware_tested':False}
(dest/'mac-reference-provenance.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('Imported',len(functions),'AX MAC functions and',len(ordered),'referenced constants')
