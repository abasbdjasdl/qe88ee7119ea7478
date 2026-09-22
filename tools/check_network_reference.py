#!/usr/bin/env python3
"""Check manually adapted PCI tables against the pinned Realtek source."""
import hashlib,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]).resolve()
commit='d1fced1b8a741dc9f92b47c69489c24385945f6e'
assert subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()==commit
names=('pci.h','pci.c','rtw8852be.c','rtw8852b.c','reg.h','fw.h','fw.c','core.c','txrx.h')
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
bits={m[1]:1<<int(m[2]) for m in re.finditer(r'^#define\s+(B_AX_\w+)\s+BIT\((\d+)\)',texts['pci.h'],re.M)}
runtime=(root/'src/network/PciRuntime.hpp').read_text()
for array,symbols in {
    'irqMasks':['R_AX_HIMR0','R_AX_PCIE_HIMR00','R_AX_PCIE_HIMR10'],
    'irqStatus':['R_AX_HISR0','R_AX_PCIE_HISR00','R_AX_PCIE_HISR10']
}.items():
    values=[int(v,0) for v in re.search(array+r'\[3\]=\{([^}]+)\}',runtime)[1].split(',')]
    assert values==[registers[s] for s in symbols]
body=texts['pci.c'].split('void rtw89_pci_config_intr_mask(',1)[1].split('EXPORT_SYMBOL',1)[0]
groups=[['B_AX_HALT_C2H_INT_EN'],re.findall(r'B_AX_\w+',body.split('rtwpci->intrs[0] = B_AX_TXDMA_STUCK_INT_EN',1)[1].split(';',1)[0])+
        ['B_AX_TXDMA_STUCK_INT_EN','B_AX_HS0ISR_IND_INT_EN'],['B_AX_HC10ISR_IND_INT_EN']]
expected_irq=[]
for group in groups:
    value=0
    for symbol in group:value|=bits[symbol]
    expected_irq.append(value)
assert [int(v,0) for v in re.search(r'irqEnabled\[3\]=\{([^}]+)\}',runtime)[1].split(',')]==expected_irq
assert re.search(r'\.fill_txaddr_info\s*=\s*rtw89_pci_fill_txaddr_info\s*,',texts['rtw8852be.c'])
assert re.search(r'\.check_rx_tag\s*=\s*false\s*,',texts['rtw8852be.c'])
assert re.search(r'\.rx_ring_eq_is_full\s*=\s*false\s*,',texts['rtw8852be.c'])
assert re.search(r'#define\s+RTW89_PCI_MULTITAG\s+8\b',texts['pci.h'])
assert re.search(r'#define\s+H2C_ROLE_MAINTAIN_LEN\s+4\b',texts['fw.c'])
assert re.search(r'struct rtw89_h2c_join\s*\{\s*__le32 w0;\s*\}',texts['fw.h'])
mailbox=(root/'src/network/FirmwareMailbox.hpp').read_text()
mac_regs={m[1]:int(m[2],16) for m in re.finditer(r'^#define\s+(R_AX_\w+)\s+(0x[0-9A-Fa-f]+)\b',texts['reg.h'],re.M)}
for array,prefix in [('h2cData','R_AX_H2CREG_DATA'),('c2hData','R_AX_C2HREG_DATA')]:
    values=[int(v,0) for v in re.search(array+r'\[4\]=\{([^}]+)\}',mailbox)[1].split(',')]
    assert values==[mac_regs[prefix+str(i)] for i in range(4)]
for name,symbol,offset in [('h2cControl','R_AX_H2CREG_CTRL',0),('c2hControl','R_AX_C2HREG_CTRL',0),
                         ('hostCounters','R_AX_UDM1',1),('firmwareControl','R_AX_WCPU_FW_CTRL',0),('schedulerTx','R_AX_CTN_TXEN',0)]:
    assert int(re.search(r'\b'+name+r'=(0x[0-9a-f]+)',mailbox)[1],16)==mac_regs[symbol]+offset
def enum_value(enum,symbol):
    body=re.search(r'enum '+enum+r' \{([^}]+)\}',texts['fw.h'])[1]
    value=-1
    for item in body.split(','):
        item=item.strip()
        if not item:continue
        parts=item.split('=');value=int(parts[1].strip(),0) if len(parts)==2 else value+1
        if parts[0].strip()==symbol:return value
    raise AssertionError(symbol)
assert int(re.search(r'schedulerCommand=(\d+)',mailbox)[1])==enum_value('rtw89_mac_h2c_type','RTW89_FWCMD_H2CREG_FUNC_SCH_TX_EN')
assert int(re.search(r'schedulerReply=(\d+)',mailbox)[1])==enum_value('rtw89_mac_c2h_type','RTW89_FWCMD_C2HREG_FUNC_TX_PAUSE_RPT')
for symbol,hi,lo in [('RTW89_H2CREG_HDR_FUNC_MASK',6,0),('RTW89_H2CREG_HDR_LEN_MASK',11,8),
                     ('RTW89_C2HREG_HDR_FUNC_MASK',6,0),('RTW89_C2HREG_HDR_LEN_MASK',11,8),
                     ('RTW89_H2CREG_SCH_TX_EN_W0_EN',31,16),('RTW89_H2CREG_SCH_TX_EN_W1_MASK',15,0)]:
    assert re.search(r'#define\s+'+symbol+r'\s+GENMASK\('+str(hi)+r',\s*'+str(lo)+r'\)',texts['fw.h'])
assert re.search(r'#define\s+RTW89_C2H_TIMEOUT\s+1000000\b',texts['fw.h'])
for symbol,hi,lo in [('B_AX_UDM1_HALMAC_H2C_DEQ_CNT_MASK',11,8),('B_AX_UDM1_HALMAC_C2H_ENQ_CNT_MASK',15,12)]:
    assert re.search(r'#define\s+'+symbol+r'\s+GENMASK\('+str(hi)+r',\s*'+str(lo)+r'\)',texts['reg.h'])
    assert symbol+' >> 8' in texts['rtw8852b.c']
report={'repository':'https://github.com/lwfinger/rtw89','commit':commit,'pci_table_verified':True,
        'firmware_register_mailbox_verified':True,
        'ring_registers_and_allocations':expected,'source_sha256':{n:hashlib.sha256(texts[n].encode()).hexdigest() for n in names},
        'license':'BSD-3-Clause option','hardware_tested':False}
dest=root/'build/network-stack/wire-reference.json';dest.parent.mkdir(parents=True,exist_ok=True)
dest.write_text(json.dumps(report,indent=2)+'\n')
print('PASS: all 9 PCI register/BDRAM rows, 8852BE address format, RX mode, DMA retention, AX commands and register mailbox match pinned rtw89')
