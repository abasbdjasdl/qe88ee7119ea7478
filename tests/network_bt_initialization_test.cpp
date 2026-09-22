// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/BtInitialization.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#ifdef BT_INITIALIZATION_NATIVE_TEST
#include "network_rfk_fakes/Fake.hpp"
inline uint16_t OSReadLittleInt16(const volatile void *base,unsigned a){
    ++fakeRfk::accesses;auto p=static_cast<const volatile uint8_t *>(base)+a;return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline void OSWriteLittleInt16(volatile void *base,unsigned a,uint16_t v){
    ++fakeRfk::accesses;auto p=static_cast<volatile uint8_t *>(base)+a;p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
#include "../src/network/MacRadioIo.cpp"
#include "../src/network/MacBtInitializationIo.cpp"
#endif
namespace b=rtl8852be::bt;
namespace i=b::initialization;
namespace n=rtl8852be::network;
struct Device {
    struct Write {uint32_t a,v;unsigned width;};
    struct Command {n::CommandId id;uint8_t sequence;std::vector<uint8_t> data;};
    std::map<uint32_t,uint8_t> memory;
    std::map<uint32_t,uint32_t> lte;
    std::map<unsigned,uint32_t> rf,lut;
    std::vector<Write> writes;std::vector<Command> commands;
    uint32_t lteSelected{},scoreboard=0x16000003,lastScoreboard{};
    uint32_t ignoreAddress=0xffffffff;unsigned ignoreWidth{};
    unsigned ops{},failAt{},delayCalls{},failDelay{},submitCalls{},failSubmit{},poisons{};
    unsigned busyLte{},lteReads{},jumpAt{};uint64_t time=100,jumpUs{};
    uint8_t nextSequence=30;
    bool gate=true,cancel{},frozen{},backward{},repeatSequence{},afterWriteFailure=true;
    bool resetSelfClears=true,ignoreLte{},ignoreRfWe{},ignoreRfSel{},reentrant{};
    void *callbackOwner{};void (*callback)(void *){};
    Device(){set32(b::firmwareControl,0xe0);set32(b::cmacFunctionRegister,0x40000000);
        set32(0x80,0);set32(b::schedulerRegister,0);set32(b::controlPathRegister,0xa1);
        lte[0x3c]=0xabcd1234;lte[0x38]=0x05001500;
        memory[0x40]=0xc5;memory[0x41]=0x85;memory[0xcc07]=0xfe;
        memory[0xda6c]=0xca;set32(0xc340,0xe207);set32(0xd200,0xa5a53fff);set32(0xd220,0x1234ffff);
        rf[2]=rf[0x10002]=0x33333;
    }
    void set32(uint32_t a,uint32_t v){for(unsigned k=0;k<4;++k)memory[a+k]=uint8_t(v>>(k*8));}
    uint32_t get(uint32_t a,unsigned bytes=4){uint32_t v=0;for(unsigned k=0;k<bytes;++k)v|=uint32_t(memory[a+k])<<(k*8);return v;}
    bool step(){++ops;if(ops==jumpAt)time+=jumpUs;return ops!=failAt;}
    bool inGate(){return gate;}bool cancelled(){return cancel;}
    uint64_t nowUs(){if(backward&&delayCalls)--time;return time;}
    bool delayUs(unsigned us){++delayCalls;if(!frozen)time+=us;return delayCalls!=failDelay;}
    bool read8(uint32_t a,uint8_t &v){if(!step())return false;
        v=a==b::lteControl+3?(++lteReads<=busyLte?0:0x20):uint8_t(get(a,1));return true;}
    bool read16(uint32_t a,uint16_t &v){v=uint16_t(get(a,2));return step();}
    bool read32(uint32_t a,uint32_t &v){if(!step())return false;
        v=a==b::scoreboardRegister?scoreboard:a==b::lteReadData?lte[lteSelected]:get(a);return true;}
    bool write(uint32_t a,uint32_t v,unsigned width){
        const bool ok=step();if(!ok&&!afterWriteFailure)return false;writes.push_back({a,v,width});
        if(a==ignoreAddress&&width==ignoreWidth)return ok;
        if(a==b::scoreboardRegister)lastScoreboard=v;
        else {
            for(unsigned k=0;k<width;++k)memory[a+k]=uint8_t(v>>(k*8));
            if(a==b::lteControl){lteSelected=v&0xffff;
                if((v&0xc0000000)==0xc0000000&&!ignoreLte)lte[lteSelected]=get(b::lteWriteData);}
            if(resetSelfClears&&(a==0xda42||(a==0xda40&&width==4)))memory[0xda42]&=~1;
        }
        return ok;
    }
    bool write8(uint32_t a,uint8_t v){return write(a,v,1);}
    bool write16(uint32_t a,uint16_t v){return write(a,v,2);}
    bool write32(uint32_t a,uint32_t v){return write(a,v,4);}
    bool readRf(uint8_t path,uint32_t a,uint32_t mask,uint32_t &v){assert(mask==0xfffff);v=rf[(unsigned(path)<<16)|a];return step();}
    bool writeRf(uint8_t path,uint32_t a,uint32_t mask,uint32_t v){
        assert(mask==0xfffff);const bool ok=step();if(!ok&&!afterWriteFailure)return false;
        writes.push_back({0x100000+(unsigned(path)<<16)+a,v,20});
        if((ignoreRfWe&&a==0xef)||(ignoreRfSel&&a==2))return ok;
        const unsigned base=unsigned(path)<<16;rf[base|a]=v;
        if(a==0x3f){assert(rf[base|0xef]==0x20000);lut[base|rf[base|0x33]]=v;}
        return ok;
    }
    bool submitH2c(n::CommandId id,bool done,const uint8_t *p,size_t length,uint8_t &sequence){
        assert(done);++submitCalls;sequence=nextSequence;if(!repeatSequence)++nextSequence;
        commands.push_back({id,sequence,{p,p+length}});
        if(reentrant&&callback)callback(callbackOwner);
        return submitCalls!=failSubmit;
    }
    void invalidateFirmwareEpoch(){++poisons;}
};
using Init=i::Initialization<Device>;
struct Fixture {
    Device io;b::Scoreboard shadow{};b::PolicySnapshot policy{};
    Init init{io,shadow,policy};
    bool begin(uint8_t rfe=1){return init.begin({0x001d1d00,rfe,1,true});}
    b::EventResult ack(uint8_t sequence,n::CommandId id,uint8_t result=0,bool done=true){
        uint8_t p[4]{};n::store32(p,uint32_t(id.category)|(uint32_t(id.commandClass)<<2)|
            (uint32_t(id.function)<<8)|(uint32_t(result)<<16)|(uint32_t(sequence)<<24));
        return init.acceptEvent({{1,0,uint8_t(done?1:0)},p,sizeof(p)});
    }
    b::EventResult ackLast(){const auto last=io.commands.back();return ack(last.sequence,last.id);}
    bool run(){if(!begin())return false;
        for(unsigned k=0;k<5;++k)if(ackLast()!=b::EventResult::consumed)return false;
        return init.ready();}
};
static void formats(){
    for(uint8_t rfe:{0,1,2,3,4,5}){
        const i::Board board{0x001d1d00,rfe,2,true};uint8_t p[14]{};
        assert(i::encodeInit(board,p));const bool dedicated=rfe>0&&!(rfe&1);
        const uint8_t expected[]={0,12,uint8_t(dedicated),uint8_t(dedicated?3:2),10,0,rfe,2,uint8_t(dedicated?0:2),0,6,2,0,0};
        for(unsigned j=0;j<14;++j)assert(p[j]==expected[j]);
    }
    uint8_t bad[14]{};assert(!i::encodeInit({0x001d1d00,0xff,1,true},bad));
    assert(!i::encodeInit({0x001d1d00,1,6,true},bad));assert(!i::encodeInit({0x001e0000,1,1,true},bad));
    uint8_t slots[146]{};i::encodeSlots(slots);assert(slots[0]==1&&slots[1]==18);
    const uint16_t durations[]={100,5,70,15,15,250,7,5,50,20,500,0,0,0,0,250,50,50};
    const uint32_t tables[]={0x55555555,0xea5a5a5a,0xea5a5a5a,0xea5a5a5a,0xea5a5a5a,0xe5555555,
        0xea5a5a5a,0xe5555555,0xe5555555,0xea5a5a5a,0x55555555,0xea5a5a5a,0xffffffff,
        0xe5555555,0xaaaaaaaa,0xea5a5a5a,0xffffffff,0xffffdfff};
    const uint8_t types[]={0,1,1,1,1,0,0,0,0,1,0,0,1,0,1,0,1,1};
    for(unsigned j=0;j<18;++j){const auto *p=slots+2+8*j;
        assert((unsigned(p[0])|(unsigned(p[1])<<8))==durations[j]);
        assert(n::little32(p+2)==tables[j]&&p[6]==types[j]&&p[7]==0);}
    uint8_t monitor[130]{};assert(i::encodeMonitor({0x001d1d00,1,1,true},monitor));
    assert(monitor[0]==2&&monitor[1]==16&&monitor[2]==0&&monitor[4]==4);
    assert(n::little32(monitor+6)==0xda24&&monitor[2+12*8]==1&&n::little32(monitor+6+15*8)==0x4694);
    assert(i::encodeMonitor({0x001d0e00,1,1,true},monitor)&&monitor[0]==1);
    uint8_t control[6]{};i::encodeControl(control);assert(control[0]==6&&control[1]==4&&n::little32(control+2)==2);
    uint8_t policy[26]{};assert(b::encodePolicy({3,1},i::initialPolicy(),policy));
    assert(policy[0]==0&&policy[1]==12&&policy[2]==3&&policy[14]==1&&policy[15]==10);
    assert(n::little32(policy+20)==0xe5555555&&policy[18]==100&&policy[24]==0);
    uint8_t wire[160]{};size_t size=0;
    assert(n::encodeH2c(i::slotsCommand,31,false,true,slots,146,wire,sizeof(wire),size));
    assert(size==154&&n::little32(wire)==0x1f000142&&n::little32(wire+4)==0x809a);
}
static unsigned success(){
    Fixture f;assert(f.begin());assert(f.init.result.hardwareProgrammed);
    assert(!f.shadow.valid&&!f.policy.valid&&!f.init.ready());
    for(unsigned k=0;k<5;++k){assert(f.io.commands.size()==k+1);
        assert(!f.shadow.valid&&!f.policy.valid);
        assert(f.ackLast()==b::EventResult::consumed);}
    assert(f.init.ready()&&f.shadow.valid&&f.policy.valid);
    assert(f.shadow.value==0x4003&&f.io.lastScoreboard==0x97004003);
    assert(f.policy.offTable==0xe5555555&&f.policy.offDuration==100&&f.policy.tdma[0]==0);
    assert(f.init.result.acknowledged==5&&f.init.result.policyAcknowledged&&f.init.result.scoreboardWritten);
    assert(f.io.get(0x40,1)==0x25&&f.io.get(0x41,1)==0x83);
    assert(f.io.get(0xcc07,1)==0xfc&&f.io.get(0xc340,2)==0xe027);
    assert(f.io.get(0xda6c,1)==0xc5&&f.io.get(0xda2c)==0xf0ffffff);
    assert(f.io.get(0xd200)==0xa5a53c00&&f.io.get(0xd220)==0x1234f005);
    assert(f.io.lte[0x3c]==0&&(f.io.lte[0x38]&b::grantMask)==0xdd00dd00);
    assert(f.io.get(b::controlPathRegister,1)==0xa5&&f.io.get(b::priorityRegister,2)==0x166);
    assert(f.io.lut[0]==0x5ff&&f.io.lut[0x10000]==0x5ff&&f.io.lut[2]==0x5ff&&f.io.lut[0x10002]==0x55f);
    assert(f.io.rf[0xef]==0&&f.io.rf[0x100ef]==0&&f.io.rf[2]==0&&f.io.rf[0x10002]==0);
    assert(f.io.get(b::schedulerRegister,2)==0&&!f.io.poisons);
    return f.io.ops;
}
static void variants(){
    Fixture wlan;assert(wlan.init.begin({0x001d1d00,1,1,true,true}));
    for(unsigned k=0;k<5;++k)assert(wlan.ackLast()==b::EventResult::consumed);
    assert(wlan.io.commands[2].data[11]==3); // FW WL_ONLY|WL_INITOK, not normal mode
    assert((wlan.io.lte[0x38]&b::grantMask)==b::calibrationGrants);
    assert(wlan.io.get(b::priorityRegister,2)==0x100&&wlan.policy.offTable==0xe5555555);
    assert(wlan.shadow.valid&&wlan.policy.valid&&wlan.init.result.policyAcknowledged);
    Fixture dedicated;assert(dedicated.begin(2));for(unsigned k=0;k<5;++k)assert(dedicated.ackLast()==b::EventResult::consumed);
    assert(dedicated.io.lut[0]==0x5df&&dedicated.io.lut[0x10000]==0x5df&&dedicated.io.lut[0x10002]==0x5ff);
    Fixture btOff;btOff.io.scoreboard=0;assert(btOff.run());assert((btOff.io.lte[0x38]&b::grantMask)==b::calibrationGrants);
    Fixture persistentReset;persistentReset.io.resetSelfClears=false;assert(persistentReset.run());
    Fixture existing;existing.shadow={0x1003,true};existing.policy=i::initialPolicy();
    assert(!existing.begin()&&existing.shadow.valid&&existing.policy.valid&&existing.io.ops==0);
    Fixture same;assert(same.run());const auto writes=same.io.writes.size(),commands=same.io.commands.size();
    const auto operations=same.io.ops;const auto shadow=same.shadow.value,table=same.policy.offTable;
    same.io.time+=2000000;same.io.gate=false;
    assert(!same.begin()&&same.shadow.valid&&same.policy.valid);
    assert(same.shadow.value==shadow&&same.policy.offTable==table&&same.io.ops==operations);
    assert(same.io.writes.size()==writes&&same.io.commands.size()==commands&&!same.io.poisons);
    assert(same.init.result.stage==i::Stage::complete&&same.init.result.error==i::Error::none&&!same.init.result.requiresRecovery);
    Fixture inFlight;assert(inFlight.begin());assert(!inFlight.begin());
    assert(inFlight.init.result.stage==i::Stage::fault&&inFlight.init.result.requiresRecovery&&inFlight.io.poisons);
}
static void faults(unsigned operations){
    for(unsigned point=1;point<=operations;++point){Fixture f;f.io.failAt=point;assert(!f.run());
        assert(f.init.result.error==i::Error::io&&!f.shadow.valid&&!f.policy.valid);
        if(f.init.result.modified)assert(f.init.result.requiresRecovery);
        if(f.io.submitCalls)assert(f.io.poisons);
        const auto writes=f.io.writes.size();assert(!f.init.service()&&!f.init.ready());assert(f.io.writes.size()==writes);}
    for(unsigned command=1;command<=5;++command){Fixture f;f.io.failSubmit=command;assert(!f.run());
        assert(f.init.result.error==i::Error::command&&!f.shadow.valid&&!f.policy.valid&&f.io.poisons);}
    Fixture delay;delay.io.failDelay=1;assert(!delay.run());
    assert(delay.init.result.error==i::Error::io&&delay.io.lastScoreboard&&!delay.shadow.valid&&!delay.policy.valid);
    Fixture lte;lte.io.busyLte=1001;lte.io.frozen=true;assert(!lte.begin());
    assert(lte.init.result.error==i::Error::lteTimeout&&lte.io.lteReads==1001);
    Fixture lateLte;lateLte.io.jumpAt=27;lateLte.io.jumpUs=50001;assert(!lateLte.begin());
    assert(lateLte.init.result.error==i::Error::lteTimeout&&lateLte.io.lteReads==1);
    Fixture overall;overall.io.jumpAt=1;overall.io.jumpUs=2000000;assert(!overall.begin());
    assert(overall.init.result.error==i::Error::timeout&&overall.io.writes.empty());
    {Fixture f;f.io.ignoreRfSel=true;
        assert(!f.begin()&&f.init.result.error==i::Error::readback);
        assert(f.init.result.address==2&&f.init.result.value==0x33333);
        assert(f.init.result.rfPath==0&&f.init.result.rfAddress==2&&f.init.result.rfExpected==0);}
    for(unsigned which=0;which<3;++which){Fixture f;f.io.ignoreRfSel=which==0;f.io.ignoreRfWe=which==1;f.io.ignoreLte=which==2;
        assert(!f.begin()&&f.init.result.error==i::Error::readback&&!f.shadow.valid&&!f.policy.valid);}
    for(const auto ignored:std::vector<Device::Write>{{0x40,0,1},{0xda20,0,1},{0xda35,0,1},{0xda40,0,1},
        {0xcc07,0,1},{0xc340,0,2},{0xda4c,0,1},{0xda6c,0,1},{0x41,0,1},{0xda30,0,4},{0xda10,0,4},
        {0xda2c,0,4},{0xd200,0,4},{0xd220,0,4},{b::controlPathRegister,0,1},{b::priorityRegister,0,2}}){
        Fixture f;f.io.ignoreAddress=ignored.a;f.io.ignoreWidth=ignored.width;
        assert(!f.run()&&f.init.result.error==i::Error::readback&&!f.shadow.valid&&!f.policy.valid);}
}
static void acknowledgements(){
    Fixture f;assert(f.begin());const auto command=f.io.commands.back();
    assert(f.ack(command.sequence-1,command.id)==b::EventResult::unrelated);
    assert(f.ack(command.sequence,b::policyCommand)==b::EventResult::unrelated);
    assert(f.ack(command.sequence,command.id,0,false)==b::EventResult::unrelated);
    assert(f.io.commands.size()==1&&!f.shadow.valid&&!f.policy.valid);
    uint8_t shortData[3]{};assert(f.init.acceptEvent({{1,0,1},shortData,3})==b::EventResult::unrelated);
    assert(f.ackLast()==b::EventResult::consumed);
    assert(f.ack(command.sequence,command.id)==b::EventResult::unrelated);
    for(unsigned position=0;position<5;++position){Fixture reject;assert(reject.begin());
        for(unsigned k=0;k<position;++k)assert(reject.ackLast()==b::EventResult::consumed);
        const auto c=reject.io.commands.back();assert(reject.ack(c.sequence,c.id,9)==b::EventResult::fault);
        assert(reject.init.result.error==i::Error::rejected&&!reject.shadow.valid&&!reject.policy.valid&&reject.io.poisons);}
    Fixture reused;reused.io.repeatSequence=true;assert(reused.begin());assert(reused.ackLast()==b::EventResult::fault);
    assert(reused.init.result.error==i::Error::sequenceReuse&&!reused.policy.valid);
}
static void lifetimes(){
    Fixture timeout;assert(timeout.begin());timeout.io.time+=300000;
    assert(timeout.ackLast()==b::EventResult::fault&&timeout.init.result.error==i::Error::timeout);
    Fixture noAck;assert(noAck.begin());noAck.io.time+=300000;
    assert(!noAck.init.service()&&!noAck.shadow.valid&&!noAck.policy.valid&&noAck.io.poisons);
    Fixture activeTx;activeTx.io.set32(b::schedulerRegister,1);assert(!activeTx.begin()&&activeTx.io.writes.empty());
    Fixture lateTx;assert(lateTx.begin());lateTx.io.set32(b::schedulerRegister,1);
    assert(lateTx.ackLast()==b::EventResult::fault&&!lateTx.policy.valid);
    Fixture cancel;assert(cancel.begin());cancel.io.cancel=true;
    assert(!cancel.init.service()&&cancel.init.result.error==i::Error::cancelled);
    Fixture gate;gate.io.gate=false;assert(!gate.begin()&&gate.io.writes.empty());
    Fixture btBusy;btBusy.io.scoreboard|=b::btRfkRun;assert(!btBusy.begin()&&btBusy.io.writes.empty());
    Fixture btLate;assert(btLate.begin());btLate.io.scoreboard|=b::btRfkRequest;
    for(unsigned k=0;k<3;++k)assert(btLate.ackLast()==b::EventResult::consumed);
    assert(btLate.ackLast()==b::EventResult::fault&&!btLate.policy.valid);
    Fixture badFw;badFw.io.set32(b::firmwareControl,0xdeadbeef);assert(!badFw.begin()&&badFw.io.writes.empty());
    Fixture dbcc;dbcc.io.set32(0x80,0x40000000);assert(!dbcc.begin()&&dbcc.io.writes.empty());
    Fixture badScbd;badScbd.io.scoreboard=0xffffffff;assert(!badScbd.begin()&&badScbd.io.writes.empty());
    Fixture backwards;backwards.io.backward=true;assert(!backwards.run()&&backwards.init.result.error==i::Error::clock);
    Fixture recursive;recursive.io.reentrant=true;recursive.io.callbackOwner=&recursive.init;
    recursive.io.callback=[](void *p){static_cast<Init *>(p)->service();};
    assert(!recursive.begin()&&recursive.init.result.error==i::Error::ownership&&!recursive.policy.valid);
}
#ifdef BT_INITIALIZATION_NATIVE_TEST
struct NativeTransport {
    IOWorkLoop &loop;std::vector<uint8_t> packet;unsigned completed{};
    bool inGate(){return loop.inGate();}uint64_t nowUs(){return fakeRfk::time;}
    bool publish(const uint8_t *p,size_t bytes){packet.assign(p,p+bytes);return true;}
    static bool event(void *owner,const n::FirmwareEvent &event,uint64_t epoch,uint64_t token){
        auto &t=*static_cast<NativeTransport *>(owner);n::FirmwareAck ack{};
        assert(n::decodeAck(event,ack)&&ack.done&&epoch==77&&token==9);++t.completed;return true;}
};
struct NativeFixture {
    IOPCIDevice device;IOMemoryMap map;IOWorkLoop loop;NativeTransport transport{loop,{},0};
    n::FirmwareCommands<NativeTransport> commands{transport,77};
    n::FirmwareCommandClient<NativeTransport> client{commands,{&transport,NativeTransport::event},9,300000};
    i::MacBtInitializationIo io{&device,&map,&loop,client.link()};
    NativeFixture(){fakeRfk::reset();}
};
static void nativeSmoke(){
    {NativeFixture f;assert(f.io.valid());uint8_t byte=0;uint16_t half=0;uint32_t word=0;
        f.map.set(0xda40,0xaabbccdd);assert(f.io.write8(0xda42,0x11)&&f.map.get(0xda40)==0xaa11ccdd);
        f.map.set(0xc340,0xaabbccdd);assert(f.io.write16(0xc340,0x1122)&&f.map.get(0xc340)==0xaabb1122);
        assert(f.io.read8(0xda42,byte)&&byte==0x11&&f.io.read16(0xc340,half)&&half==0x1122);
        f.map.set(0xd200,0xa5a50000);assert(f.io.read32(0xd200,word)&&word==0xa5a50000);
        assert(f.io.write32(0xd200,0x12340000)&&f.map.get(0xd200)==0x12340000);
        assert(f.io.write32(b::lteControl,0x800f003c)&&f.io.write32(b::lteControl,0xc00f0038));
        assert(!f.io.write32(b::lteControl,0xc00f0039)&&!f.io.write32(b::scoreboardRegister,0x81000000));
        assert(f.io.write32(b::scoreboardRegister,0x81004003));
        for(uint32_t a:{b::firmwareControl,b::cmacFunctionRegister,b::schedulerRegister,0x10000u,0xd201u})
            assert(!f.io.write32(a,0)&&!f.io.write8(a,0)&&!f.io.write16(a,0));
        assert(!f.io.read32(0xd204,word)&&!f.io.read16(0xc341,half)&&!f.io.read8(0xd200,byte));
        f.map.set(0x1174c,0);assert(f.io.writeRf(1,0x33,0xfffff,2));assert(f.map.get(0x10370)==0x13300002);
        assert(!f.io.writeRf(2,0x33,0xfffff,2)&&!f.io.writeRf(0,0x33,0xfffff,3));
        assert(!f.io.writeRf(0,0x3f,0xfffff,0)&&!f.io.writeRf(0,2,0xff,0));
        f.map.set(0x1174c,0x04020000);assert(f.io.readRf(0,0xef,0xfffff,word)&&word==0x20000);
        assert(f.io.radioTrace().readAddress==0x1174c&&f.io.radioTrace().readValue==0x04020000);
        assert(f.io.radioTrace().writeAddress==0x10378&&(f.io.radioTrace().writeValue&0x7ff)==0xef);
        assert(f.io.radioTrace().rfWriteCommand==0x13300002);
        assert(!f.io.readRf(0,0x3f,0xfffff,word));
        assert(!f.io.delayUs(1001));
    }
    {NativeFixture f;uint8_t p[130]{};assert(i::encodeMonitor({0x001d1d00,1,1,true},p));uint8_t seq=99;
        assert(!f.io.submitH2c(i::monitorCommand,false,p,130,seq));
        assert(!f.io.submitH2c(b::policyCommand,true,p,130,seq));
        assert(f.io.submitH2c(i::monitorCommand,true,p,130,seq)&&seq==0&&f.transport.packet.size()==138);
        p[1]=0;assert(f.transport.packet[9]==16);
        uint8_t ackBytes[4]{};n::store32(ackBytes,2|(16<<2)|(2<<8));
        assert(f.commands.accept({{1,0,1},ackBytes,4},77)==n::CommandEvent::completed&&f.transport.completed==1);
        f.io.invalidateFirmwareEpoch();assert(!f.io.write8(0x40,0));}
    for(unsigned invalid=0;invalid<4;++invalid){NativeFixture f;
        if(invalid==0)f.device.command=0;else if(invalid==1)f.device.command=0xffff;
        else if(invalid==2)f.loop.gate=false;else f.io.cancel();
        assert(!f.io.write8(0x40,0x20)&&!f.io.writeRf(0,2,0xfffff,0)&&!f.io.delayUs(1));}
    {NativeFixture f;assert(f.io.write8(0x40,0x20));fakeRfk::time+=2000000;
        assert(!f.io.writeRf(0,2,0xfffff,0)&&f.io.cancelled());}
    {NativeFixture f;assert(f.io.write8(0x40,0x20));fakeRfk::time=0;
        assert(!f.io.write8(0x40,0x20)&&f.io.cancelled());}
    {NativeFixture f;i::MacBtInitializationIo missing(&f.device,&f.map,&f.loop,{});assert(!missing.valid());
        f.map.physical+=4096;i::MacBtInitializationIo wrong(&f.device,&f.map,&f.loop,f.client.link());assert(!wrong.valid());}
    {NativeFixture f;f.map.length=f.device.bar.length=0x10000;
        i::MacBtInitializationIo shortMap(&f.device,&f.map,&f.loop,f.client.link());assert(!shortMap.valid());}
    puts("BT init native adapter passed: actual source against fake IOKit, width/allowlist/RF access and shared queue ACK checks");
}
#endif
int main(){formats();const auto operations=success();variants();faults(operations);acknowledgements();lifetimes();
#ifdef BT_INITIALIZATION_NATIVE_TEST
    nativeSmoke();
#endif
    std::printf("BT initialization passed: %u I/O failure points, 5 acknowledged setup commands, RFE-specific RF masks, invalid-before-ACK outputs\n",operations);
}
