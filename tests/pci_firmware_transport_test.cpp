// SPDX-License-Identifier: BSD-3-Clause
#include "../src/PciFirmwareTransport.hpp"
#include <assert.h>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>
#include <stdio.h>
namespace t=rtl8852be::transport;
struct Device {
    std::map<uint32_t,uint32_t> regs{{0x1000,0xc15000},{0x1010,0xfff00},{0x1e0,0x23}};
    uint16_t cmd=2;uint64_t time=0;unsigned writes=0,fail=0,barriers=0,doorbells=0,releases=0;
    bool safe=true,stuck=false,resetStuck=false,noHeader=false,noFirmware=false,busyAfter=false,denyOff=false,partialEnable=false;
    uint16_t command(){return cmd;}bool interruptsSafe(){return safe;}
    uint64_t nowUs(){return time;}void pauseUs(unsigned n){if(!stuck)time+=n;}
    uint32_t read32(uint32_t a){if(a==0x101c&&busyAfter&&doorbells)return 0x40000;return regs[a];}
    uint16_t read16(uint32_t a){return static_cast<uint16_t>(regs[a]);}
    void uploadBarrier(){++barriers;}
    bool uploadBusMaster(bool on){if(on){cmd=0x406;return !partialEnable;}if(denyOff)return false;cmd=2;return true;}
    bool uploadWrite32(uint32_t a,uint32_t v){
        assert(t::uploadAddress32(a));++writes;if(writes==fail)return false;
        if(cmd&4)assert(a==0x1000||a==0x1010||a==0x8380||a==0x160||a==0x164);
        regs[a]=v;
        if(a==0x1000&&(v&8)&&!resetStuck)regs[a]&=~8u;
        if(a==0x1014)regs[0x1080]=0;
        return true;
    }
    bool uploadWrite16(uint32_t a,uint16_t v){
        assert(t::uploadAddress16(a));++writes;if(writes==fail)return false;
        if(a==0x1080){
            assert((cmd&6)==6&&(regs[0x1000]&0x2800)==0x800&&(regs[0x8380]&3)==1);
            assert((regs[0x1010]&0x1f0f00)==0xb0f00&&v>=1&&v<=164);
            assert(regs[0x1160]==0x10000000&&regs[0x1164]==0&&regs[0x1038]==256);
            assert(!regs[0x1a0]&&!regs[0x10b0]&&!regs[0x13b0]);
            ++doorbells;regs[a]=uint32_t(v)|(uint32_t(v)<<16);
            if(v==1&&!noHeader)regs[0x1e0]=0x27;
            if(v==164&&!noFirmware)regs[0x1e0]=0xe0;
        }else regs[a]=v;
        return true;
    }
};
int main(int argc,char **argv){
    assert(argc==2);std::ifstream in(argv[1],std::ios::binary);
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),{});
    t::Packets packets;assert(packets.initialize(blob.data(),blob.size(),1)==t::PacketStatus::ok);
    auto run=[&](Device &d){auto release=[](void *p){auto &dev=*static_cast<Device *>(p);assert(!(dev.cmd&4));++dev.releases;return true;};t::PciDownload<Device> backend(d,{0x10000000,4096,4096,1},164,true,release,&d);auto r=t::transfer(backend,packets);return std::make_pair(r,backend.result);};
    Device d;auto r=run(d);
    assert(r.first.status==t::TransferStatus::complete&&r.first.buffersReleased&&r.first.submitted==164);
    assert(r.second.idle&&r.second.busMasterOff&&r.second.restored&&r.second.published==164);
    assert(d.cmd==2&&d.barriers==164&&!d.regs[0x1160]&&!d.regs[0x1038]&&d.releases==1);
    const auto writes=d.writes;
    for(unsigned i=1;i<=writes;++i){d=Device{};d.fail=i;r=run(d);assert(r.first.status!=t::TransferStatus::complete);assert(!(d.cmd&4));}
    d=Device{};d.safe=false;r=run(d);assert(r.first.status==t::TransferStatus::startFailed&&!d.doorbells&&!d.writes);
    d=Device{};d.regs[0x1160]=4096;r=run(d);assert(r.first.status==t::TransferStatus::startFailed&&!d.doorbells);
    d=Device{};d.regs[0x1080]=1;r=run(d);assert(r.first.status==t::TransferStatus::startFailed&&!d.doorbells);
    d=Device{};d.partialEnable=true;r=run(d);assert(r.first.status==t::TransferStatus::startFailed&&d.cmd==2&&!d.doorbells);
    d=Device{};d.noHeader=true;r=run(d);assert(r.first.status==t::TransferStatus::timeout&&d.doorbells==1&&r.first.buffersReleased);
    d=Device{};d.noFirmware=true;r=run(d);assert(r.first.status==t::TransferStatus::timeout&&d.doorbells==164&&r.first.buffersReleased);
    d=Device{};d.resetStuck=true;d.stuck=true;r=run(d);assert(r.first.status==t::TransferStatus::startFailed&&!d.doorbells&&r.second.polls<250);
    d=Device{};d.busyAfter=true;r=run(d);assert(r.first.status==t::TransferStatus::quiesceFailed&&r.first.retainBuffers&&!r.first.buffersReleased&&d.cmd==2&&!d.releases);
    d=Device{};d.denyOff=true;r=run(d);assert(r.first.status==t::TransferStatus::quiesceFailed&&r.first.retainBuffers&&!r.first.buffersReleased&&!d.releases);
    assert(!t::uploadAddress32(0xc000)&&!t::uploadAddress32(0x30)&&!t::uploadAddress16(0x1082));
    printf("PASS: full 164-packet CH12 upload model, %u write failures, MSI/dirty-ring guards, partial start, timeouts, retained DMA ownership\n",writes);
}
