// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeScanProbeTx.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace probe=rtl8852be::network::scanprobe;
static unsigned checks;
static void check(bool value,int line){++checks;if(!value){std::fprintf(stderr,"line %d\n",line);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)
static const uint8_t rates[]={0x82,0x84,0x8b,0x96,12,18,24,36,48,72,96,108};
struct Ops {
    struct Node {unsigned refs{1};uint8_t channel{};bool inTree{},collect{};};
    struct Frame {Node *node{};std::vector<uint8_t> body;};
    enum class Fault {none,node,body,busy,prepend,drop,queuedButError,queuedAndDrop};
    Fault fault{};unsigned allocations{},frees{},bodies{},bodyFrees{},locks{},drops{};
    bool locked{},pumping{};Frame *queued{},*hardware{};Node *allocated{};
    Node *createNode(uint8_t channel,const uint8_t *input,size_t count){
        CHECK(input==rates&&count==sizeof(rates));if(fault==Fault::node)return nullptr;
        CHECK(!allocated);allocated=new Node;allocated->channel=channel;++allocations;return allocated;
    }
    Frame *createBody(const uint8_t *body,size_t length){
        if(fault==Fault::body)return nullptr;
        auto *frame=new Frame;frame->body.assign(body,body+length);++bodies;return frame;
    }
    void freeBody(Frame *frame){CHECK(frame);delete frame;++bodyFrees;}
    void retainNode(Node *node){CHECK(node==allocated&&node->refs>0);++node->refs;}
    void releaseNode(Node *node){CHECK(node==allocated&&node->refs>0&&!node->collect&&!node->inTree);--node->refs;}
    unsigned references(Node *node){CHECK(node==allocated);return node->refs;}
    void freeDetachedNode(Node *node){
        CHECK(node==allocated&&node->refs==0&&!node->inTree&&!node->collect);
        delete node;allocated=nullptr;++frees;
    }
    void lockQueue(){CHECK(!locked);locked=true;++locks;}
    void unlockQueue(){CHECK(locked);locked=false;}
    bool queueReady(){CHECK(locked);return fault!=Fault::busy&&!pumping&&!queued;}
    unsigned queueDrops(){CHECK(locked);return drops;}
    void suppressPump(bool value){CHECK(locked&&pumping!=value);pumping=value;}
    int managementOutput(Node *node,Frame *frame){
        CHECK(locked&&pumping&&node->refs==2&&!queued);
        // Exact pinned contracts: prepend error consumes mbuf; mq-full also
        // consumes it but returns success without releasing its node ref.
        if(fault==Fault::prepend){freeBody(frame);return 12;}
        if(fault==Fault::drop){++drops;freeBody(frame);return 0;}
        frame->node=node;queued=frame;
        if(fault==Fault::queuedAndDrop)++drops;
        return fault==Fault::queuedButError?5:0;
    }
    bool queueContainsOnly(Node *node){CHECK(locked);return queued&&queued->node==node;}
    void retireHost(){CHECK(!locked&&queued&&!hardware);auto *frame=queued;queued=nullptr;releaseNode(frame->node);freeBody(frame);}
    void publish(){CHECK(!locked&&queued&&!hardware);hardware=queued;queued=nullptr;}
    void retireHardware(){CHECK(!locked&&hardware&&!queued);auto *frame=hardware;hardware=nullptr;releaseNode(frame->node);freeBody(frame);}
    ~Ops(){CHECK(!allocated&&!queued&&!hardware&&!locked&&!pumping);CHECK(allocations==frees&&bodies==bodyFrees);}
};
static void ownership(){
    for(auto fault:{Ops::Fault::node,Ops::Fault::body,Ops::Fault::busy,Ops::Fault::prepend,Ops::Fault::drop}){
        Ops ops;ops.fault=fault;probe::FrameOwner<Ops> owner;
        CHECK(!owner.queueFrame(ops,6,rates,sizeof(rates)));CHECK(!owner.node());
        CHECK(owner.releaseOwner(ops));CHECK(!ops.allocated&&!ops.queued&&!ops.locked&&!ops.pumping);
    }
    for(auto fault:{Ops::Fault::queuedButError,Ops::Fault::queuedAndDrop}){
        Ops ops;ops.fault=fault;probe::FrameOwner<Ops> owner;
        CHECK(!owner.queueFrame(ops,6,rates,sizeof(rates)));CHECK(owner.node()&&ops.queued);
        CHECK(!owner.releaseOwner(ops)&&owner.node()->refs==2);
        ops.retireHost();CHECK(owner.releaseOwner(ops));
    }
    for(unsigned channel=1;channel<=11;++channel){
        Ops ops;probe::FrameOwner<Ops> owner;
        CHECK(owner.queueFrame(ops,uint8_t(channel),rates,sizeof(rates)));
        CHECK(owner.node()->channel==channel&&owner.node()->refs==2);
        CHECK(!owner.queueFrame(ops,uint8_t(channel),rates,sizeof(rates))&&!owner.releaseOwner(ops));
        // Wildcard SSID, exact current legacy rates, no credentials/HT/HE IE.
        const auto &body=ops.queued->body;
        CHECK(body.size()==18&&body[0]==0&&body[1]==0&&body[2]==1&&body[3]==8);
        CHECK(std::memcmp(body.data()+4,rates,8)==0&&body[12]==50&&body[13]==4);
        CHECK(std::memcmp(body.data()+14,rates+8,4)==0);
        ops.publish();CHECK(!owner.releaseOwner(ops)&&owner.node()->refs==2);
        ops.retireHardware();CHECK(owner.node()->refs==1&&owner.releaseOwner(ops)&&!owner.node());
        CHECK(owner.releaseOwner(ops));
    }
    {Ops ops;probe::FrameOwner<Ops> owner;const uint8_t invalid[]={3};
     CHECK(!owner.queueFrame(ops,1,invalid,1)&&!ops.allocations);}
}
struct Counters {uint64_t completed{},acked{},retryLimit{},expired{},dropped{},polluted{},rejectedReports{};};
static void completion(){
    Counters before{40,30,4,3,3,1,2},after=before;
    CHECK(!probe::exactlyOneSuccessful(before,after));++after.completed;++after.acked;
    CHECK(probe::exactlyOneSuccessful(before,after));
    for(unsigned field=0;field<7;++field){
        auto bad=after;
        switch(field){case 0:++bad.completed;break;case 1:--bad.acked;break;case 2:++bad.retryLimit;break;
            case 3:++bad.expired;break;case 4:++bad.dropped;break;case 5:++bad.polluted;break;case 6:++bad.rejectedReports;break;}
        CHECK(!probe::exactlyOneSuccessful(before,bad));
    }
    before.completed=UINT64_MAX;after=before;after.completed=0;++after.acked;
    CHECK(!probe::exactlyOneSuccessful(before,after));
}
int main(){ownership();completion();std::printf("native scan probe TX: %u checks passed\n",checks);}
