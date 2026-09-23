// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/AuthenticationEvents.hpp"
#include "../src/network/AuthenticationBinding.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
using namespace rtl8852be::network::authevents;
static const uint8_t own[6]={2,1,2,3,4,5},peer[6]={2,6,7,8,9,10},source[6]={2,11,12,13,14,15};
static size_t frame(uint8_t *f,bool eapol=false,size_t body=6){
    std::memset(f,0,2300);f[0]=eapol?8:0xb0;f[1]=eapol?2:0;
    std::memcpy(f+4,own,6);std::memcpy(f+10,peer,6);std::memcpy(f+16,eapol?source:peer,6);
    if(!eapol){f[24]=3;f[26]=1;return 24+body;}
    const uint8_t llc[8]={0xaa,0xaa,3,0,0,0,0x88,0x8e};std::memcpy(f+24,llc,8);
    f[32]=2;f[33]=3;f[34]=uint8_t((body-4)>>8);f[35]=uint8_t(body-4);return 32+body;
}
static void associationBinding(){
    using State=rtl8852be::station::State;
    using Token=rtl8852be::station::Token;
    const Token live{9,72};
    // A reader starting after authentication was sent must still bind during
    // CMAC/firmware-join/CAM installation, before EAPOL starts arriving.
    const State eligible[]={State::authenticating,State::installingAssociationCmac,
        State::joining,State::installingAssociationCam,State::associated,State::authorized};
    uint8_t f[2300];const size_t n=frame(f,true,12);
    int reader;
    for(auto stage:eligible){
        Queue q;Event e;
        assert(!q.needsBinding()&&q.enable(&reader)&&q.needsBinding());
        assert(currentAssociation(stage,live,live,false));
        assert(q.bind(live.epoch,live.operation,own,peer,1)&&!q.needsBinding());
        assert(q.observe(f,n,1,1)&&q.read(&reader,e)&&e.kind==eapol);
        assert(e.epoch==live.epoch&&e.operation==live.operation);
        // Reaching a later association state must not require a fresh binding
        // that would erase an already queued authentication transcript.
        const uint64_t generation=e.generation;
        assert(currentAssociation(State::authorized,live,live,false));
        assert(!q.needsBinding()&&q.observe(f,n,1,2)&&q.read(&reader,e));
        assert(e.generation==generation);
        assert(!currentAssociation(stage,live,live,true));
        assert(!currentAssociation(stage,Token{8,72},live,false));
        assert(!currentAssociation(stage,Token{9,71},live,false));
        assert(!currentAssociation(stage,live,Token{9,71},false));
    }
    const State inactive[]={State::stopped,State::preparing,State::creatingRole,
        State::initialNoLink,State::installingIdle,State::idle,State::scanningBegin,
        State::scanningTune,State::scanningDwell,State::scanningRestore,State::scanningEnd,
        State::preparingAuthentication,State::disconnectingCmac,State::disconnecting,
        State::disconnectingCam,State::removingRole,State::removingCam,State::faulted};
    for(auto stage:inactive)assert(!currentAssociation(stage,live,live,false));
    assert(!currentAssociation(static_cast<State>(255),live,live,false));
    for(auto token:{Token{},Token{0,72},Token{9,0}})
        assert(!currentAssociation(State::authorized,token,token,false));

    Queue q;Event e;
    assert(q.enable(&reader)&&q.bind(live.epoch,live.operation,own,peer,1));
    for(unsigned i=0;i<8;++i)assert(q.observe(f,n,1,i));
    assert(!q.observe(f,n,1,9)&&!q.needsBinding());
    // Polling must preserve a latched loss instead of silently re-binding.
    assert(currentAssociation(State::authorized,live,live,false));
    assert(q.read(&reader,e)&&(e.flags&2)&&e.dropped==9);
    const uint64_t lostGeneration=e.generation;
    assert(!q.needsBinding()&&q.read(&reader,e)&&e.generation==lostGeneration&&(e.flags&2));
    q.invalidate();
    assert(q.needsBinding()&&q.read(&reader,e)&&e.generation!=lostGeneration&&e.flags==0);
    assert(!currentAssociation(State::disconnecting,live,live,false));
    assert(!currentAssociation(State::authorized,Token{9,73},live,false));
    q.disable();
    assert(!q.needsBinding()&&!q.owned(&reader)&&!q.read(&reader,e));
}
int main(){
    associationBinding();
    Queue q;int a,b;Event e;uint8_t f[2300];size_t n=frame(f);
    assert(!q.bind(1,1,own,peer,1)&&!q.read(&a,e));
    assert(q.enable(&a)&&!q.enable(&b)&&q.bind(1,2,own,peer,1));
    assert(q.observe(f,n,1,50)&&!q.read(&b,e));
    assert(q.read(&a,e)&&e.kind==authentication&&e.epoch==1&&e.operation==2&&e.sampledAtUs==50);
    assert(e.length==6&&e.body[0]==3&&!memcmp(e.source,peer,6));uint64_t generation=e.generation;
    assert(q.read(&a,e)&&e.kind==empty&&e.body[0]==0&&e.flags==1);
    for(unsigned bad=0;bad<7;++bad){
        n=frame(f);switch(bad){case 0:f[4]^=1;break;case 1:f[10]^=1;break;
        case 2:f[16]^=1;break;case 3:f[1]|=0x40;break;case 4:f[1]|=4;break;
        case 5:f[22]=1;break;case 6:f[1]|=0x80;break;}
        assert(!q.observe(f,n,1,51));
    }
    n=frame(f,true,12);assert(q.observe(f,n,1,52)&&q.read(&a,e));
    assert(e.kind==eapol&&e.length==12&&!memcmp(e.source,source,6));
    f[35]=100;assert(!q.observe(f,n,1,53));
    n=frame(f);for(unsigned i=0;i<8;++i)assert(q.observe(f,n,1,60+i));
    assert(!q.observe(f,n,1,70)&&q.read(&a,e));
    assert(e.kind==empty&&(e.flags&2)&&e.dropped==9&&e.body[0]==0);
    assert(!q.observe(f,n,1,71));
    assert(q.bind(1,3,own,peer,1)&&q.read(&a,e)&&e.generation!=generation&&!(e.flags&2));
    n=frame(f,true,2048);assert(q.observe(f,n,1,80));
    n=frame(f,true,2049);assert(!q.observe(f,n,1,81)&&q.read(&a,e));
    assert(e.kind==empty&&(e.flags&2)&&e.dropped==2);
    q.invalidate();assert(q.read(&a,e)&&e.flags==0&&e.epoch==0&&e.peer[0]==0);
    assert(!q.disable(&b)&&q.disable(&a)&&!q.read(&a,e));
    assert(q.enable(&b)&&q.bind(2,1,own,peer,11));
    n=frame(f);assert(!q.observe(f,n,1,100)&&q.observe(f,n,11,100));
    assert(q.bind(2,2,own,peer,6)&&q.read(&b,e)&&e.kind==empty&&e.channel==6);
    // Exercise length handling using exact-sized allocations under ASan.
    uint32_t random=42;
    for(unsigned i=0;i<5000;++i){
        random=random*1664525u+1013904223u;size_t size=random%2300;
        std::vector<uint8_t> bytes(size);for(auto &v:bytes){random=random*1664525u+1013904223u;v=uint8_t(random>>24);}
        q.observe(bytes.data(),bytes.size(),6,101+i);
    }
    puts("Authentication RX events: association binding, stale tokens, peer/session isolation, parsing, loss, wiping, ownership and bounds passed");
}
