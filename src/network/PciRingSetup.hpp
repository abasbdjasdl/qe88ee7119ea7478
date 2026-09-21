// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2020-2022 Realtek Corporation (upstream BSD option)
// Adapted from rtw89_pci_reset_trx_rings / rtw89_bd_ram_table_single and
// rtw8852b_pci_info at d1fced1b8a741dc9f92b47c69489c24385945f6e.
#pragma once
#include "PciDataPath.hpp"
namespace rtl8852be { namespace network {
struct RingRegisters {uint32_t count,index,low,high,bdram,allocation;};
// ACH0..3, management, high-priority, firmware, RXQ, RPQ. 8852BE uses the
// original AX map; the _V1 register addresses belong to other chips.
constexpr RingRegisters ringRegisters[9]={
    {0x1024,0x1058,0x1110,0x1114,0x1200,0x020500},
    {0x1026,0x105c,0x1118,0x111c,0x1204,0x020505},
    {0x1028,0x1060,0x1120,0x1124,0x1208,0x02050a},
    {0x102a,0x1064,0x1128,0x112c,0x120c,0x02050f},
    {0x1034,0x1078,0x1150,0x1154,0x1220,0x010414},
    {0x1036,0x107c,0x1158,0x115c,0x1224,0x010418},
    {0x1038,0x1080,0x1160,0x1164,0x1228,0x01041c},
    {0x1020,0x1050,0x1100,0x1104,0,0},
    {0x1022,0x1054,0x1108,0x110c,0,0}
};
struct RingMemory {uint64_t address{};uint16_t count{};};
struct RingSetupResult {bool attempted{},programmed{},restored{};uint32_t failureAddress{},expected{},actual{};unsigned writes{},polls{};};
// D provides command(), read16/32, write16/32 (bool), nowUs(), pauseUs().
// This configures stopped rings only. It never enables bus mastering, HCI,
// interrupts or a radio. Full chip init and prepared RX buffers are prerequisites
// for the separate future start path. On any ambiguous failure keep mappings.
template<class D> class PciRingSetup {
    D &device_;
    struct Snapshot {uint32_t low{},high{},bdram{};uint16_t count{};} saved_[9]{};
    uint32_t init_{};bool snapshot_{};
    bool mismatch(uint32_t address,uint32_t expected,uint32_t actual){
        if(!result.failureAddress){result.failureAddress=address;result.expected=expected;result.actual=actual;}return false;
    }
    bool safe(){
        if((device_.command()&6)!=2)return mismatch(4,2,device_.command()&6);
        const auto init=device_.read32(0x1000),stop=device_.read32(0x1010),busy=device_.read32(0x101c);
        if(init==0xffffffff||(init&0x2800))return mismatch(0x1000,0,init);
        if(stop==0xffffffff||(stop&0x1f0f00)!=0x1f0f00)return mismatch(0x1010,0x1f0f00,stop);
        if(busy==0xffffffff||(busy&0x7f0f03))return mismatch(0x101c,0,busy);
        const uint32_t masks[]={0x1a0,0x10b0,0x13b0};
        for(auto address:masks)if(device_.read32(address))return mismatch(address,0,device_.read32(address));
        return true;
    }
    bool write32(uint32_t address,uint32_t value){
        if(!safe())return false;++result.writes;
        if(!device_.write32(address,value))return mismatch(address,value,device_.read32(address));return true;
    }
    bool write16(uint32_t address,uint16_t value){
        if(!safe())return false;++result.writes;
        if(!device_.write16(address,value))return mismatch(address,value,device_.read16(address));return true;
    }
    bool equal32(uint32_t address,uint32_t expected){
        const auto value=device_.read32(address);return value==expected||mismatch(address,expected,value);
    }
    bool equal16(uint32_t address,uint16_t expected){
        const auto value=device_.read16(address);return value==expected||mismatch(address,expected,value);
    }
public:
    RingSetupResult result{};
    explicit PciRingSetup(D &device):device_(device){}
    PciRingSetup(const PciRingSetup &)=delete;
    PciRingSetup &operator=(const PciRingSetup &)=delete;
    bool configure(const RingMemory (&rings)[9]){
        if(result.attempted)return false;result.attempted=true;
        for(size_t i=0;i<9;++i){
            const auto &m=rings[i];
            if(m.count<2||m.count>4095||(m.address&7)||!dma32Range(m.address,size_t(m.count)*8))return false;
            for(size_t j=0;j<i;++j)if(m.address<rings[j].address+size_t(rings[j].count)*8&&
                rings[j].address<m.address+size_t(m.count)*8)return false;
        }
        if(!safe())return false;
        // Both internal HCI clocks are required for BDRAM reset, with host DMA
        // independently disabled by the preconditions above.
        const auto hci=device_.read32(0x8380);
        if(hci==0xffffffff||(hci&3)!=3)return mismatch(0x8380,3,hci);
        init_=device_.read32(0x1000);
        if(init_&8)return mismatch(0x1000,0,init_);
        for(size_t i=0;i<9;++i){const auto &r=ringRegisters[i];auto &s=saved_[i];
            s.low=device_.read32(r.low);s.high=device_.read32(r.high);s.count=device_.read16(r.count);
            s.bdram=r.bdram?device_.read32(r.bdram):0;
            if(s.low)return mismatch(r.low,0,s.low);
            if(s.high)return mismatch(r.high,0,s.high);
            if(s.count&0xfff)return mismatch(r.count,0,s.count);
            if(!equal32(r.index,0))return false;
            if(s.bdram==0xffffffff||s.bdram==0xdeadbeef)return mismatch(r.bdram,0,s.bdram);
        }
        snapshot_=true;
        for(size_t i=0;i<9;++i){const auto &r=ringRegisters[i];const auto &m=rings[i];
            const auto count=uint16_t((saved_[i].count&0xf000)|m.count);
            if(!write32(r.high,0)||!write32(r.low,uint32_t(m.address))||!write16(r.count,count)||
               (r.bdram&&!write32(r.bdram,(saved_[i].bdram&0xff000000)|r.allocation)))return false;
            if(!equal32(r.high,0)||!equal32(r.low,uint32_t(m.address))||!equal16(r.count,count)||
               (r.bdram&&!equal32(r.bdram,(saved_[i].bdram&0xff000000)|r.allocation)))return false;
        }
        // Only the seven implemented TX channels and the two RX channels.
        if(!write32(0x1014,0x70f)||!write32(0x1018,3)||!write32(0x1000,init_|8))return false;
        const uint64_t begin=device_.nowUs();bool reset=false;
        for(unsigned i=0;i<201;++i){
            const auto now=device_.nowUs();if(now<begin||now-begin>=10000)break;
            const auto value=device_.read32(0x1000);++result.polls;
            if(value==0xffffffff||value==0xdeadbeef)return mismatch(0x1000,init_,value);
            if(!(value&8)){reset=true;break;}device_.pauseUs(50);
        }
        if(!reset)return mismatch(0x1000,init_,device_.read32(0x1000));
        for(const auto &r:ringRegisters)if(!equal32(r.index,0))return false;
        if(!safe())return false;result.programmed=true;return true;
    }
    bool restore(){
        if(!snapshot_)return false;if(!safe())return false;
        // No descriptor was ever submitted by this class. A future running
        // path must prove idle and reset software/hardware epochs separately.
        bool ok=true;
        for(size_t i=0;i<9;++i){const auto &r=ringRegisters[i];const auto &s=saved_[i];
            const bool high=write32(r.high,s.high),low=write32(r.low,s.low),count=write16(r.count,s.count);
            const bool bdram=!r.bdram||write32(r.bdram,s.bdram);ok=high&&low&&count&&bdram&&ok;
        }
        const bool init=write32(0x1000,init_);ok=init&&ok;
        for(size_t i=0;i<9;++i){const auto &r=ringRegisters[i];const auto &s=saved_[i];
            ok=equal32(r.high,s.high)&&equal32(r.low,s.low)&&equal16(r.count,s.count)&&ok;
            if(r.bdram)ok=equal32(r.bdram,s.bdram)&&ok;
        }
        result.restored=ok&&safe();return result.restored;
    }
};
} }
