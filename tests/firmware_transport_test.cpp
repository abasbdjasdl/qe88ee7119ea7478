// SPDX-License-Identifier: BSD-3-Clause
#include "../src/FirmwareTransfer.hpp"
#include "../src/DmaProbe.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <array>
#include <string>
namespace t=rtl8852be::transport;
namespace f=rtl8852be::firmware;
std::vector<uint8_t> fixture(size_t bytes=2021){
    std::vector<uint8_t> v(32+48+bytes);v[0]=255;v[1]=1;v[16]=1;v[17]=5;
    t::put32(v.data()+20,32);t::put32(v.data()+24,static_cast<uint32_t>(48+bytes));
    t::put32(v.data()+32,0x88520102);v[57]=1;
    t::put32(v.data()+64,0x18970000);t::put32(v.data()+68,0x02000000|static_cast<uint32_t>(bytes));
    for(size_t i=80;i<v.size();++i)v[i]=static_cast<uint8_t>(i*17+29);
    return v;
}
struct Backend {
    uint64_t now=0;bool stuck=false,backwards=false,cancel=false,failPrepare=false,failStart=false;
    bool failClear=false,failQuiesce=false,failRelease=false,neverReady=false,stale=false;
    int failedPublish=-1,forceControl=-1,forceIndex=-1,stallPath=-1,lateError=-1,cancelAt=-1;
    unsigned count=0,published=0,quiesces=0,releases=0,delays=0;
    bool allocated=false,started=false,stopped=false,haltCleared=false;
    std::vector<std::array<uint8_t,4096>> buffers;
    std::array<uint8_t,4096> ring{};
    std::vector<std::string> calls;
    uint64_t nowUs(){if(backwards&&now) return --now;return now;}
    bool cancelled(){return cancel||(cancelAt>=0&&published>=static_cast<unsigned>(cancelAt));}
    bool prepare(const t::Packets &packets){
        calls.push_back("prepare");allocated=true;count=packets.count();buffers.resize(count);
        for(unsigned i=0;i<count;++i){
            t::PacketInfo info;assert(packets.encode(i,buffers[i].data(),4096,info)==t::PacketStatus::ok);
            assert(rtl8852be::dma::encodeBd(ring.data()+i*8,8,0x100000+i*4096,static_cast<uint32_t>(info.bytes)));
        }
        return !failPrepare;
    }
    bool startDownload(){assert(allocated&&!started);calls.push_back("start");started=true;return !failStart;}
    bool publish(unsigned producer){
        assert(started&&allocated&&!stopped&&releases==0&&producer==published+1&&producer<=count);
        assert(producer==1||haltCleared);calls.push_back("publish");++published;
        return static_cast<int>(producer)!=failedPublish;
    }
    uint8_t readControl(){
        assert(started&&allocated);
        if(forceControl>=0)return static_cast<uint8_t>(forceControl);
        if(stale)return 0xe6;
        if(!published)return stallPath==0?0:2;
        if(published==1)return stallPath==1?2:6;
        if(lateError>=0)return static_cast<uint8_t>(lateError);
        return neverReady?0x26:0xe6;
    }
    bool clearHaltControls(){assert(published==1);haltCleared=true;return !failClear;}
    uint32_t readIndex(){return forceIndex>=0?static_cast<uint32_t>(forceIndex):(count<<16)|count;}
    void delayUs(unsigned us){assert(us==50);++delays;if(!stuck)now+=us;}
    bool quiesceAndProveIdle(){calls.push_back("stop");++quiesces;stopped=!failQuiesce;return stopped;}
    bool releaseAll(){
        assert(!started||stopped);calls.push_back("release");++releases;
        if(failRelease)return false;
        allocated=false;buffers.clear();return true;
    }
};
void packetsTest(const std::vector<uint8_t> &v,uint8_t cut){
    auto original=v;
    t::Packets packets;assert(packets.initialize(v.data(),v.size(),cut)==t::PacketStatus::ok);
    const auto &p=packets.plan();
    std::array<uint8_t,4098> out{};t::PacketInfo info;
    for(unsigned n=0;n<packets.count();++n){
        out.fill(0xaa);
        assert(packets.encode(n,out.data()+1,4096,info,0x71)==t::PacketStatus::ok);
        assert(out[0]==0xaa&&out[info.bytes+1]==0xaa);
        const auto *raw=out.data()+1,*body=raw+24;
        assert(f::le32(raw)==(n?0x001c0000:0x000c0000));
        assert(f::le32(raw+8)==info.payloadBytes&&f::le32(raw+4)==0&&f::le32(raw+16)==0&&f::le32(raw+20)==0);
        if(!n){
            assert(info.bytes==24+8+p.baseHeaderBytes&&f::le32(body)==0x7100000d);
            assert(f::le32(body+4)==p.baseHeaderBytes+8);
            assert((f::le32(body+8+28)&0xffff)==2020);
            for(size_t i=0;i<p.baseHeaderBytes;++i)if(i!=28&&i!=29)assert(body[8+i]==v[p.imageOffset+i]);
        }else{
            f::Chunk c;assert(f::chunkAt(p,n-1,c));
            assert(info.payloadBytes==c.bytes&&info.bytes==24+c.bytes&&info.sourceOffset==c.offset);
            for(unsigned i=0;i<c.bytes;++i)assert(body[i]==v[c.offset+i]);
        }
        const size_t length=info.bytes;
        assert(packets.encode(n,out.data(),length-1,info)==t::PacketStatus::smallBuffer&&info.bytes==0);
    }
    assert(v==original);
    assert(packets.encode(packets.count(),out.data(),out.size(),info)==t::PacketStatus::badIndex&&info.bytes==0);
    assert(packets.encode(0,const_cast<uint8_t *>(v.data()),v.size(),info)==t::PacketStatus::overlap&&v==original);
    Backend b;const auto r=t::transfer(b,packets);
    assert(r.status==t::TransferStatus::complete&&r.submitted==packets.count()&&r.buffersReleased&&!r.retainBuffers);
    assert(b.calls[b.calls.size()-2]=="stop"&&b.calls.back()=="release"&&b.quiesces==1&&b.releases==1);
}
int main(int argc,char **argv){
    auto v=fixture();packetsTest(v,1);t::Packets p;assert(p.initialize(v.data(),v.size(),1)==t::PacketStatus::ok);
    std::array<uint8_t,4096> out{};t::PacketInfo info;
    assert(p.encode(2,out.data(),out.size(),info)==t::PacketStatus::ok&&info.payloadBytes==1&&f::le32(out.data()+12)==0);
    for(int fail=1;fail<=3;++fail){Backend b;b.failedPublish=fail;auto r=t::transfer(b,p);
        assert(r.operationStatus==t::TransferStatus::submitFailed&&r.buffersReleased&&r.attempted==static_cast<unsigned>(fail)&&b.quiesces==1);}
    for(int point=0;point<3;++point){Backend b;if(point==0)b.failPrepare=true;if(point==1)b.failStart=true;if(point==2)b.failClear=true;
        auto r=t::transfer(b,p);assert(r.status!=t::TransferStatus::complete&&r.buffersReleased&&b.releases==1&&b.quiesces==(point==0?0u:1u));}
    for(int stage=0;stage<2;++stage){Backend b;b.stallPath=stage;auto r=t::transfer(b,p);
        assert(r.status==t::TransferStatus::timeout&&r.buffersReleased&&r.submitted==static_cast<unsigned>(stage));}
    for(int value:{0xff,0x40,0x60,0x80}){Backend b;b.forceControl=value;auto r=t::transfer(b,p);
        assert(r.status!=t::TransferStatus::complete&&r.status!=t::TransferStatus::timeout&&r.buffersReleased&&!r.submitted);}
    Backend b;b.stale=true;auto r=t::transfer(b,p);assert(r.status==t::TransferStatus::staleReady&&!r.submitted);
    b=Backend{};b.failQuiesce=true;r=t::transfer(b,p);assert(r.status==t::TransferStatus::quiesceFailed&&r.retainBuffers&&!r.buffersReleased&&b.allocated&&b.releases==0);
    b=Backend{};b.failStart=true;b.failQuiesce=true;r=t::transfer(b,p);assert(r.operationStatus==t::TransferStatus::startFailed&&r.retainBuffers&&!b.releases);
    b=Backend{};b.failRelease=true;r=t::transfer(b,p);assert(r.status==t::TransferStatus::releaseFailed&&r.retainBuffers&&!r.buffersReleased);
    b=Backend{};b.neverReady=true;b.stuck=true;r=t::transfer(b,p);assert(r.status==t::TransferStatus::timeout&&r.polls<=8003&&r.buffersReleased);
    b=Backend{};b.cancel=true;r=t::transfer(b,p);assert(r.status==t::TransferStatus::cancelled&&b.calls.empty());
    b=Backend{};b.cancelAt=2;r=t::transfer(b,p);assert(r.status==t::TransferStatus::cancelled&&r.submitted==2&&r.quiesced&&r.buffersReleased);
    b=Backend{};b.lateError=0x40;r=t::transfer(b,p);assert(r.status==t::TransferStatus::checksumFailed&&r.submitted==p.count()&&r.buffersReleased);
    b=Backend{};b.now=100;b.backwards=true;r=t::transfer(b,p);assert(r.status==t::TransferStatus::clockInvalid&&b.calls.empty());
    for(int idx:{4<<16|3,0x1000003,0x5eadbeef,3<<16|2}){b=Backend{};b.forceIndex=idx;r=t::transfer(b,p);assert(r.status==t::TransferStatus::indexInvalid&&r.buffersReleased);}
    b=Backend{};b.forceIndex=3;r=t::transfer(b,p);assert(r.status==t::TransferStatus::timeout&&r.buffersReleased);
    auto large=fixture(2020*255);t::Packets invalid;
    assert(invalid.initialize(large.data(),large.size(),1)==t::PacketStatus::tooManyPackets&&invalid.count()==0);
    b=Backend{};r=t::transfer(b,invalid);assert(r.status==t::TransferStatus::invalidPlan&&b.calls.empty());
    for(size_t n=0;n<80;++n){assert(invalid.initialize(v.data(),n,1)==t::PacketStatus::layoutError&&invalid.count()==0);}
    if(argc>1){FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);const auto length=ftell(file);rewind(file);
        std::vector<uint8_t> real(static_cast<size_t>(length));assert(fread(real.data(),1,real.size(),file)==real.size());fclose(file);
        packetsTest(real,1);packetsTest(real,2);
        assert(p.initialize(real.data(),real.size(),1)==t::PacketStatus::ok&&p.count()==164);
        for(unsigned i=1;i<=p.count();++i){b=Backend{};b.failedPublish=static_cast<int>(i);r=t::transfer(b,p);
            assert(r.status==t::TransferStatus::submitFailed&&r.buffersReleased&&r.attempted==i&&r.submitted==i-1);}
        printf("PASS: both real chip cuts, 164 immutable packets, every publish failure, retained ownership until quiescence\n");
    }
    puts("PASS: 8852B TXWD/H2C/header/payload bytes, dynamic-header exclusion, tails, bounds, firmware errors, timeout/cancel/stale-ready, cleanup");
}
