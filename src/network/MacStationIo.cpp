// SPDX-License-Identifier: BSD-3-Clause
#include "MacStationIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
#include <sys/kpi_mbuf.h>
namespace rtl8852be { namespace network {
MacStationIo::MacStationIo(IOPCIDevice &device,IOMemoryMap &map,IOWorkLoop &loop,MacStationAuthority authority):
    device_(&device),map_(&map),loop_(&loop),authority_(authority){}
bool MacStationIo::valid()const{
    if(!device_||!map_||!loop_||!map_->getVirtualAddress()||map_->getLength()<0x40020||
       device_->configRead16(kIOPCIConfigVendorID)!=0x10ec||device_->configRead16(kIOPCIConfigDeviceID)!=0xb852)return false;
    auto *bar=device_->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    return bar&&bar->getPhysicalAddress()==map_->getPhysicalAddress()&&bar->getLength()==map_->getLength()&&
        (device_->configRead16(kIOPCIConfigCommand)&2);
}
bool MacStationIo::inGate()const{return loop_&&loop_->inGate();}
uint64_t MacStationIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacStationIo::read(uint32_t address,uint32_t &value){
    if(!inGate()||!valid()||(address&3)||address>map_->getLength()-4)return false;
    value=OSReadLittleInt32(reinterpret_cast<const volatile void*>(map_->getVirtualAddress()),address);OSSynchronizeIO();
    return value!=0xffffffff&&value!=0xdeadbeef;
}
bool MacStationIo::safe(){uint32_t value=0;return !faulted_&&read(0xc000,value)&&(value&0x40000000);}
bool MacStationIo::schedulerPaused(){
    if(!safe()||!authority_.owner||!authority_.schedulerPaused||!authority_.schedulerPaused(authority_.owner))return false;
    uint32_t value=0;return read(0xc348,value)&&(value&0xffff)==0;
}
bool MacStationIo::stationTableWindowOwned(){
    return seedActive_&&inGate()&&authority_.owner&&authority_.stationWindowOwned&&authority_.stationWindowOwned(authority_.owner);
}
bool MacStationIo::write(uint32_t address,uint32_t value){
    if(!safe()||(address&3)||address>map_->getLength()-4)return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void*>(map_->getVirtualAddress()),address,value);OSSynchronizeIO();return true;
}
bool MacStationIo::masked(uint32_t address,uint32_t mask,uint32_t value){
    uint32_t old=0,actual=0;
    return !(value&~mask)&&read(address,old)&&write(address,(old&~mask)|value)&&read(address,actual)&&(actual&mask)==value;
}
bool MacStationIo::byteMasked(uint32_t address,uint8_t mask,uint8_t value){
    if(!safe()||address>=map_->getLength()||(value&~mask))return false;
    auto *bytes=reinterpret_cast<volatile uint8_t*>(map_->getVirtualAddress());
    const auto old=bytes[address];bytes[address]=uint8_t((old&~mask)|value);OSSynchronizeIO();return (bytes[address]&mask)==value;
}
bool MacStationIo::halfMasked(uint32_t address,uint16_t mask,uint16_t value){
    if(!safe()||(address&1)||address>map_->getLength()-2||(value&~mask))return false;
    auto *bytes=reinterpret_cast<volatile void*>(map_->getVirtualAddress());
    const auto old=OSReadLittleInt16(bytes,address);OSWriteLittleInt16(bytes,address,uint16_t((old&~mask)|value));OSSynchronizeIO();
    return (OSReadLittleInt16(bytes,address)&mask)==value;
}
bool MacStationIo::write32(uint32_t address,uint32_t value){
    if(!stationTableWindowOwned()||!schedulerPaused())return false;
    if(address==0xc04){
        const bool dmac=value>=0x18800000&&value<0x18800800&&!(value&3);
        const bool cmac=value>=0x18840000&&value<0x18841000&&!(value&31);
        if(!dmac&&!cmac)return false;
        if(!write(address,value))return false;uint32_t actual=0;
        if(!read(address,actual)||actual!=value)return false;selectedWindow_=value;return true;
    }
    if(address<0x40000||address>0x4001c||(address&3)||!selectedWindow_)return false;
    if(selectedWindow_<0x18840000&&(address!=0x40000||value))return false;
    return write(address,value);
}
bool MacStationIo::drainWrites(){
    uint32_t actual=0;OSSynchronizeIO();
    return stationTableWindowOwned()&&schedulerPaused()&&read(0xc04,actual)&&actual==selectedWindow_;
}
station::tables::SeedResult MacStationIo::seed(uint8_t macid){
    if(seedActive_||portState_==MacPortState::waiting||faulted_)return {station::tables::SeedError::precondition};
    seedActive_=true;selectedWindow_=0;const auto result=station::tables::seedMacTables(*this,macid);seedActive_=false;
    if(result.requiresReset)fault();return result;
}
bool MacStationIo::beginPort(MacPortConfig config){
    if(portState_==MacPortState::waiting||seedActive_||config.port>4||
       (config.connected&&(!config.beaconInterval||config.beaconInterval>1000||!config.dtimPeriod))||!schedulerPaused())return false;
    uint32_t control=0,interval=0;
    if(!read(0xc400+config.port*0x40,control)||!read(0xc414+config.port*0x40,interval))return fault();
    if(((control>>10)&3)==3)return false; // AP beacon draining is a separate unsupported lifecycle.
    previouslyEnabled_=(control&4)!=0;port_=config;lastNow_=nowUs();
    const uint64_t delay=previouslyEnabled_?uint64_t((interval&0xffff)+1)*1000:0;
    if(delay>=2000000||lastNow_>UINT64_MAX-2000000)return false;
    waitUntil_=lastNow_+delay;portDeadline_=lastNow_+2000000;portState_=MacPortState::waiting;return true;
}
bool MacStationIo::portProgram(){
    const uint32_t offset=port_.port*0x40,config=0xc400+offset;
    if(previouslyEnabled_){
        if(!masked(config,0x10004,0))return false;
        uint32_t old=0;if(!read(config,old)||!write(config,old|0x20)||!write(0xc434+offset,0))return false;
        // TSF reset is a strobe and can self-clear; do not assert its readback.
    }
    const uint32_t flags=port_.connected?0x12818:0;
    if(!masked(config,0x13c1b,flags)||
       !masked(0xc414+offset,0xffff,port_.connected?port_.beaconInterval:100)||
       !byteMasked(port_.port?0xc59f+port_.port:0xc590,0xff,0)||
       !byteMasked(0xca08,3,3)||!halfMasked(0xc426+offset,0xff00,uint16_t(port_.dtimPeriod)<<8))return false;
    const uint32_t drop=(1u<<(16+port_.port))|(port_.port==0?1u:0u);
    if(!masked(0xc63c,drop,0)||!masked(0xc404+offset,0xff,2)||
       !masked(0xc404+offset,0x0fff0000,200u<<16)||!masked(0xc408+offset,0x0fff0000,0)||
       !halfMasked(0xc40e + offset,0x0fff,5)||
       !masked(port_.port==4?0xc6a4:0xc6a0,0x3fu<<((port_.port%4)*8),0))return false;
    if(port_.port==0&&!masked(0xc568,0x00fffffe,0))return false;
    if(!masked(config,4,4))return false;
    // TSF resync across APs is a no-op for this sole infrastructure station.
    IODelay(20);return masked(0xc40c+offset,0xfff,160);
}
MacPortState MacStationIo::servicePort(){
    if(portState_!=MacPortState::waiting)return portState_;
    const auto now=nowUs();
    if(now<lastNow_||now>=portDeadline_||!schedulerPaused()){fault();return portState_;}
    lastNow_=now;if(now<waitUntil_)return portState_;
    if(!portProgram())fault();else portState_=MacPortState::complete;return portState_;
}
bool MacStationIo::scanFilter(bool enable){
    if(!schedulerPaused()||seedActive_||portState_==MacPortState::waiting||enable==scanSaved_)return false;
    constexpr uint32_t mask=~0x003f0000u;uint32_t value=0;
    if(!read(0xce20,value))return fault();
    if(enable){scanFilter_=value&mask;
        // rtw89_ops_configure_filter scanning policy; do not enable CRC/error delivery.
        if(!masked(0xce20,mask,scanFilter_&~0x86u))return fault();scanSaved_=true;
    }else{if(!masked(0xce20,mask,scanFilter_))return fault();scanSaved_=false;}
    return true;
}
bool MacStationIo::txInfo(const TxLease &lease,uint8_t macid,channel::Channel channel,uint16_t basicRates,TxInfo &out,unsigned &ring){
    if(!inGate()||faulted_||!lease.frame||!lease.node||lease.bytes!=mbuf_pkthdr_len(lease.frame))return false;
    uint8_t header[24];if(mbuf_copydata(lease.frame,0,sizeof(header),header))return false;
    return stationio::legacyTx(header,sizeof(header),lease.bytes,macid,channel,basicRates,out,ring);
}
} }

