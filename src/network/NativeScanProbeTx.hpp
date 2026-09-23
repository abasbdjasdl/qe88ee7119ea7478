// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeScanProbe.hpp"
namespace rtl8852be { namespace network { namespace scanprobe {
template<class Counters> bool exactlyOneSuccessful(const Counters &before,const Counters &after){
    return before.completed!=UINT64_MAX&&before.acked!=UINT64_MAX&&
        after.completed==before.completed+1&&after.acked==before.acked+1&&
        after.retryLimit==before.retryLimit&&after.expired==before.expired&&
        after.dropped==before.dropped&&after.polluted==before.polluted&&
        after.rejectedReports==before.rejectedReports;
}
// Shared ownership algorithm. Ops is the gated net80211 adapter in the kernel;
// tests inject allocation/queue failures into this exact algorithm. The owner
// reference outlives every queued, prepared and DMA-owned frame reference.
template<class Ops> class FrameOwner {
public:
    using Node=typename Ops::Node;
private:
    Node *node_{};
public:
    Node *node()const{return node_;}
    bool queueFrame(Ops &ops,uint8_t channel,const uint8_t *rates,size_t count){
        if(node_)return false;
        uint8_t body[maxBytes]{};const auto encoded=encode(rates,count,body,sizeof(body));
        if(encoded.error!=Error::none)return false;
        node_=ops.createNode(channel,rates,count);if(!node_)return false;
        auto frame=ops.createBody(body,encoded.length);
        if(!frame){releaseOwner(ops);return false;}
        ops.lockQueue();
        if(!ops.queueReady()){
            ops.unlockQueue();ops.freeBody(frame);releaseOwner(ops);return false;
        }
        const auto drops=ops.queueDrops();
        ops.retainNode(node_); // Exactly one reference travels with the frame.
        ops.suppressPump(true);
        const int error=ops.managementOutput(node_,frame); // consumes frame, even on error
        ops.suppressPump(false);
        const bool queued=ops.queueContainsOnly(node_);
        const bool accepted=!error&&queued&&ops.queueDrops()==drops;
        // Pinned mgmt_output ignores mq_enqueue failure. Its full-queue path
        // frees only the mbuf, so no queue owns the extra node reference.
        if(!queued)ops.releaseNode(node_);
        ops.unlockQueue();
        if(!accepted&&!queued)releaseOwner(ops);
        return accepted;
    }
    bool releaseOwner(Ops &ops){
        if(!node_)return true;
        if(ops.references(node_)!=1)return false;
        // A detached CACHE node must never enter ieee80211_free_node/RB_REMOVE.
        ops.releaseNode(node_);ops.freeDetachedNode(node_);node_=nullptr;return true;
    }
};
} } }
