// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/RxDmaQueue.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
using namespace rtl8852be::network;
struct Buffer;
struct Model {
    std::vector<Buffer*> allocated;unsigned attempts{},alive{},failAllocate=~0u,failCpu=~0u,failDevice=~0u,failRelease=~0u;
}model;
struct Buffer {
    unsigned id=~0u;std::vector<uint8_t> data;bool visible{},owned{};
    bool allocate(int*,int*,size_t bytes,uint32_t){
        if(owned)return false;id=model.attempts++;if(id==model.failAllocate)return false;
        data.assign(bytes,0);owned=true;++model.alive;model.allocated.push_back(this);return true;
    }
    DataMapping mapping()const{return owned?DataMapping{const_cast<uint8_t*>(data.data()),0x1000000ULL+uint64_t(id)*65536,data.size()}:DataMapping{};}
    bool syncForDevice(){return owned&&id!=model.failDevice;}
    bool syncForCpu(){return owned&&id!=model.failCpu;}
    bool markDeviceVisible(){if(!owned)return false;visible=true;return true;}
    bool release(){if(!owned)return true;if(visible||id==model.failRelease)return false;owned=false;--model.alive;data.clear();return true;}
    bool releaseAfterDmaStopped(){visible=false;return release();}
    ~Buffer(){assert(!owned);}
};
void reset(){assert(!model.alive);model={};}
struct Received {size_t count{};uint8_t last{};};
void receive(void *p,const uint8_t *data,size_t bytes){assert(bytes==8);auto &r=*static_cast<Received*>(p);++r.count;r.last=data[4];}
void packet(unsigned slot,uint8_t payload,bool valid=true){auto &data=model.allocated[slot+1]->data;store32(data.data(),valid?0xc008:0x3fff);data[4]=payload;}
void run(){
    int device=0,loop=0;RxDmaQueue<Buffer> q;Received r;
    assert(q.allocate(&device,&loop));auto ring=q.ringMapping();assert(ring.capacity==512);
    for(unsigned i=0;i<64;++i){assert(little32(ring.bytes+i*8)==11494);assert(little32(ring.bytes+i*8+4)==model.allocated[i+1]->mapping().physical);}
    assert(!q.poll(1,10,receive,&r).ok&&q.markDeviceVisible());assert(!q.release()&&model.alive==65);
    for(unsigned i=0;i<5;++i)packet(i,uint8_t(i));auto a=q.poll(5,3,receive,&r);
    assert(a.ok&&a.processed==3&&a.consumer==3&&r.count==3);
    a=q.poll(5,3,receive,&r);assert(a.ok&&a.processed==2&&a.consumer==5&&r.count==5);
    a=q.poll(5,3,receive,&r);assert(a.ok&&!a.processed);
    unsigned consumer=5;size_t drops=0;
    for(unsigned i=0;i<100000;++i){const bool valid=i%37!=0;packet(consumer,uint8_t(i),valid);
        consumer=(consumer+1)%64;a=q.poll(uint16_t(consumer),64,receive,&r);
        assert(a.ok&&a.processed==1&&a.consumer==consumer&&a.delivered==size_t(valid));drops+=!valid;
    }
    assert(r.count==100005-drops&&!q.faulted());assert(!q.poll(64,1,receive,&r).ok&&!q.poll(0,0,receive,&r).ok);
    assert(q.releaseAfterDmaStopped()&&!model.alive);
}
void failures(){
    int d=0,l=0;Received r;
    for(unsigned i=0;i<65;++i){reset();model.failAllocate=i;RxDmaQueue<Buffer> q;assert(!q.allocate(&d,&l)&&!model.alive&&!q.ringMapping().bytes);}
    for(bool cpu:{true,false}){
        reset();RxDmaQueue<Buffer> q;assert(q.allocate(&d,&l)&&q.markDeviceVisible());packet(0,0x12);
        if(cpu)model.failCpu=1;else model.failDevice=1;r={};auto p=q.poll(1,1,receive,&r);
        assert(!p.ok&&q.faulted()&&!p.processed&&r.count==size_t(!cpu));
        assert(!q.poll(1,1,receive,&r).ok&&!q.release()&&model.alive==65);
        assert(q.releaseAfterDmaStopped()&&!model.alive);
    }
    reset();model.failAllocate=4;model.failRelease=1;
    {RxDmaQueue<Buffer> q;assert(!q.allocate(&d,&l)&&model.alive==1);assert(!q.allocate(&d,&l));model.failRelease=~0u;assert(q.release()&&!model.alive);}
    reset();model.failDevice=0;{RxDmaQueue<Buffer> q;assert(!q.allocate(&d,&l)&&!model.alive);}
}
void reentry(){
    reset();int d=0,l=0;RxDmaQueue<Buffer> q;assert(q.allocate(&d,&l)&&q.markDeviceVisible());packet(0,1);
    const auto callback=[](void *context,const uint8_t *data,size_t bytes){
        auto &queue=*static_cast<RxDmaQueue<Buffer>*>(context);assert(bytes==8&&data[4]==1);
        Received ignored;assert(!queue.poll(1,1,receive,&ignored).ok);
        assert(!queue.releaseAfterDmaStopped()&&model.alive==65&&data[4]==1);
    };
    assert(q.poll(1,1,callback,&q).ok&&q.releaseAfterDmaStopped()&&!model.alive);
}
int main(){run();failures();reentry();std::puts("RX DMA: 64 exact BDs, 100000 wrap/recycle operations, bounded budget, malformed packets, all 65 allocation failures, sync/cleanup faults and reentrant stop passed");}
