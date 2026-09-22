// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace rtl8852be { namespace network {
enum class QueueServiceResult {drained,more,fault};
struct QueueReceive {
    void *context{};
    // Synchronous borrowed DMA view. Decode/copy through receiveRxq/receiveRpq
    // before returning. true also permits an intentionally dropped bad packet.
    bool (*rxq)(void *,const uint8_t *,size_t){};
    bool (*rpq)(void *,const uint8_t *,size_t){};
};
// Binds real native DMA queues to runtime index/doorbell operations. All calls
// run on one workloop gate. Owners outlive this binding; teardown is deferred
// until drain() returns. No firmware/MAC/PHY readiness is invented here.
template<class Runtime,class Tx,class Firmware,class Rx> class PciQueueService {
    Runtime &runtime_;Tx *tx_[6];Firmware &fw_;Rx &rxq_,&rpq_;
    QueueReceive receive_;bool serving_{},faulted_{};
    struct Sink {PciQueueService *self;bool rpq;bool failed{};};
    static void receive(void *p,const uint8_t *bytes,size_t length){
        auto &s=*static_cast<Sink *>(p);if(s.failed)return;
        auto &o=*s.self;
        if(!o.runtime_.running()){s.failed=true;return;}
        auto callback=s.rpq?o.receive_.rpq:o.receive_.rxq;
        if(!callback(o.receive_.context,bytes,length)||!o.runtime_.running())s.failed=true;
    }
    QueueServiceResult fault(){faulted_=true;return QueueServiceResult::fault;}
public:
    PciQueueService(Runtime &runtime,Tx *const (&tx)[6],Firmware &firmware,Rx &rxq,Rx &rpq,QueueReceive receive):
        runtime_(runtime),fw_(firmware),rxq_(rxq),rpq_(rpq),receive_(receive){for(unsigned i=0;i<6;++i)tx_[i]=tx[i];}
    PciQueueService(const PciQueueService &)=delete;PciQueueService &operator=(const PciQueueService &)=delete;
    bool serving()const{return serving_;}
    bool faulted()const{return faulted_;}
    QueueServiceResult drain(){
        if(serving_||faulted_||!runtime_.running()||!receive_.rxq||!receive_.rpq)return fault();
        for(auto *q:tx_)if(!q)return fault();
        serving_=true;struct Guard{bool &value;~Guard(){value=false;}} guard{serving_};
        // Update all TXBD consumers before RPQ: either arrival order is valid
        // and the queue's two-condition lease ledger decides when to free.
        for(unsigned i=0;i<7;++i){uint16_t host=0,hardware=0;
            if(!runtime_.readIndices(i,host,hardware))return fault();
            const int error=i<6?tx_[i]->completions().consumeTo(hardware):fw_.consumeTo(hardware);
            if(error)return fault();
        }
        bool more=false;
        // RPQ first unblocks TX credits, then RXQ delivers data and C2H events.
        for(unsigned pass=0;pass<2;++pass){const unsigned ring=pass?7:8;
            Rx &queue=pass?rxq_:rpq_;uint16_t host=0,hardware=0;
            if(!runtime_.readIndices(ring,host,hardware))return fault();
            Sink sink{this,pass==0,false};
            const auto result=queue.poll(hardware,32,receive,&sink);
            if(!result.ok||sink.failed||faulted_||!runtime_.running())return fault();
            if(!runtime_.publish(ring,result.consumer))return fault();
            // Re-read after processing/publication: descriptors arriving during
            // the callback remain pending even if their IRQ was coalesced.
            if(!runtime_.readIndices(ring,host,hardware))return fault();
            more=(host!=hardware)||more;
        }
        return more?QueueServiceResult::more:QueueServiceResult::drained;
    }
};
} }
