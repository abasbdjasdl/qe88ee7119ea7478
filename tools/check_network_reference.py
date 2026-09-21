#!/usr/bin/env python3
"""Check manually adapted PCI tables against the pinned Realtek source."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
assert subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()==commit
names=('pci.h','pci.c','rtw8852be.c','fw.h','fw.c','core.c','txrx.h')
assert not subprocess.check_output(['git','-C',str(source),'diff','HEAD','--',*names],text=True)
texts={n:subprocess.check_output(['git','-C',str(source),'show','HEAD:'+n]).decode() for n in names}
registers={m[1]:int(m[2],16) for m in re.finditer(r'^#define\s+(R_AX_\w+)\s+(0x[0-9A-Fa-f]+)\b',texts['pci.h'],re.M)}
table=texts['pci.c'].split('rtw89_bd_ram_table_single[RTW89_TXCH_NUM] = {',1)[1].split('};',1)[0]
allocations={m[1]:int(m[2])|(int(m[3])<<8)|(int(m[4])<<16) for m in re.finditer(r'\[RTW89_TXCH_(\w+)\]\s*=\s*\{.start_idx\s*=\s*(\d+),\s*.max_num\s*=\s*(\d+),\s*.min_num\s*=\s*(\d+)',table)}
expected=[]
for channel in ('ACH0','ACH1','ACH2','ACH3','CH8','CH9','CH12','RXQ','RPQ'):
    direction='RX' if channel in ('RXQ','RPQ') else 'TX'
    prefix='R_AX_'+channel+'_'+direction+'BD_'
    row=[registers[prefix+k] for k in ('NUM','IDX','DESA_L','DESA_H')]
    row += [registers['R_AX_'+channel+'_BDRAM_CTRL'],allocations[channel]] if direction=='TX' else [0,0]
    expected.append(row)
adapted=(root/'src/network/PciRingSetup.hpp').read_text().split('ringRegisters[9]={',1)[1].split('};',1)[0]
actual=[[int(x.strip(),0) for x in row.split(',')] for row in re.findall(r'\{([^{}]+)\}',adapted)]
assert actual==expected,(actual,expected)
assert re.search(r'\.fill_txaddr_info\s*=\s*rtw89_pci_fill_txaddr_info\s*,',texts['rtw8852be.c'])
assert re.search(r'\.check_rx_tag\s*=\s*false\s*,',texts['rtw8852be.c'])
assert re.search(r'\.rx_ring_eq_is_full\s*=\s*false\s*,',texts['rtw8852be.c'])
assert re.search(r'#define\s+RTW89_PCI_MULTITAG\s+8\b',texts['pci.h'])
assert re.search(r'#define\s+H2C_ROLE_MAINTAIN_LEN\s+4\b',texts['fw.c'])
assert re.search(r'struct rtw89_h2c_join\s*\{\s*__le32 w0;\s*\}',texts['fw.h'])
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'pci_table_verified':True,
        'ring_registers_and_allocations':expected,'source_sha256':{n:hashlib.sha256(texts[n].encode()).hexdigest() for n in names},
        'license':'BSD-3-Clause option','hardware_tested':False}
dest=root/'build/network-stack/wire-reference.json';dest.parent.mkdir(parents=True,exist_ok=True)
dest.write_text(json.dumps(report,indent=2)+'\n')
print('PASS: all 9 PCI register/BDRAM rows, 8852BE address format, RX mode, DMA retention and AX command sizes match pinned rtw89')
