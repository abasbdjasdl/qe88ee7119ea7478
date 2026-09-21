// SPDX-License-Identifier: GPL-2.0-or-later
#include "Net80211PciQueue.hpp"
#include <sys/errno.h>
namespace rtl8852be { namespace network {
static bool validMapping(const DataMapping &m,size_t required){
    return m.bytes&&m.capacity>=required&&dma32Range(m.physical,m.capacity);
}
int Net80211PciQueue::initialize(ieee80211com *ic,uint8_t channel,DataMapping ring,const TxPageMapping (&pages)[count]){
    if(ownership_.outstanding())return EBUSY;
    if(!ic||(channel>3&&channel!=8&&channel!=9)||!validMapping(ring,count*8)||(ring.physical&7))return EINVAL;
    for(const auto &p:pages)if(!validMapping(p.descriptor,64)||!validMapping(p.frame,16383)||(p.descriptor.physical&7))return EINVAL;
    ic_=ic;channel_=channel;ring_=ring;counters_={};
    // Ledger reset is legal only under the caller's stopped-DMA contract.
    ownership_.reclaimAfterDmaStopped([](void *){});
    for(size_t i=0;i<count;++i){pages_[i]=pages[i];leases_[i]={};}
    for(size_t i=0;i<count*8;++i)ring_.bytes[i]=0;
    return 0;
}
bool Net80211PciQueue::full()const{
    TxOwnership<count>::Ticket ticket;
    return !ic_||ownership_.peek(ticket)!=QueueStatus::ok;
}
int Net80211PciQueue::stage(TxLease &lease,TxInfo info,uint16_t &nextProducer){
    nextProducer=0;
    if(!ic_)return ENETDOWN;
    if(!lease.frame||!lease.node||lease.bytes!=mbuf_pkthdr_len(lease.frame)||
       !lease.bytes||lease.bytes>16383||info.pkt_size!=lease.bytes||info.ch_dma!=channel_)return EINVAL;
    TxOwnership<count>::Ticket ticket;
    if(ownership_.peek(ticket)!=QueueStatus::ok)return ENOBUFS;
    auto &page=pages_[ticket.page];TxWire wire;
    if(encodePciTx(info,ticket.page,page.descriptor.physical,page.frame.physical,wire)!=DescriptorStatus::ok)return EINVAL;
    if(mbuf_copydata(lease.frame,0,lease.bytes,page.frame.bytes)!=0)return EINVAL;
    for(size_t i=0;i<wire.wdBytes;++i)page.descriptor.bytes[i]=wire.wd[i];
    if(ownership_.commit(ticket,info.qsel,info.mac_id,&leases_[ticket.page])!=QueueStatus::ok)return EBUSY;
    leases_[ticket.page]=lease;lease={};
    for(size_t i=0;i<8;++i)ring_.bytes[ticket.bd*8+i]=wire.bd[i];
    nextProducer=ticket.nextProducer;return 0;
}
void Net80211PciQueue::drainCompletions(){
    for(uint16_t i=0;i<count;++i){
        TxOwnership<count>::Completion completion;
        if(ownership_.take(i,completion)!=QueueStatus::ok)continue;
        ++counters_.completed;
        if(completion.polluted)++counters_.polluted;
        switch(completion.status){case 0:++counters_.acked;break;case 1:++counters_.retryLimit;break;
            case 2:++counters_.expired;break;case 3:++counters_.dropped;break;}
        releaseTx(ic_,*static_cast<TxLease *>(completion.cookie));
    }
}
int Net80211PciQueue::consumeTo(uint16_t hardwareConsumer){
    if(!ic_)return ENETDOWN;
    if(ownership_.consumedTo(hardwareConsumer)!=QueueStatus::ok)return EINVAL;
    drainCompletions();return 0;
}
int Net80211PciQueue::releaseReport(const ReleaseReport &report){
    if(!ic_)return ENETDOWN;
    if(dataChannel(report.qsel)!=channel_||ownership_.report(report)!=QueueStatus::ok){++counters_.rejectedReports;return EINVAL;}
    drainCompletions();return 0;
}
void Net80211PciQueue::reclaimAfterDmaStopped(){
    ownership_.reclaimAfterDmaStopped([this](void *cookie){releaseTx(ic_,*static_cast<TxLease *>(cookie));++counters_.dropped;});
    ic_=nullptr;ring_={};for(auto &p:pages_)p={};
}
int receiveRxq(ieee80211com *ic,PciRxAssembly &assembly,const uint8_t *dma,size_t bytes,
               uint8_t channel,int rssi,const ReceiveCallbacks &callbacks){
    AssembledRx out;
    switch(assembly.feed(dma,bytes,out)){
        case AssemblyStatus::invalid:return EINVAL;
        case AssemblyStatus::incomplete:return EAGAIN;
        case AssemblyStatus::complete:break;
    }
    switch(out.packet.info.pkt_type){
        case 0:return deliverRealtekRx(ic,out.data,out.bytes,4,channel,rssi);
        case 1:return callbacks.phyReport?callbacks.phyReport(callbacks.context,out.packet):EOPNOTSUPP;
        case 10:{FirmwareEvent event;
            if(!decodeC2h(out.packet.payload,out.packet.length,event))return EINVAL;
            return callbacks.firmwareEvent?callbacks.firmwareEvent(callbacks.context,event):EOPNOTSUPP;}
        default:return EOPNOTSUPP;
    }
}
int receiveRpq(PciRxAssembly &assembly,const uint8_t *dma,size_t bytes,Net80211PciQueue *const (&queues)[13]){
    AssembledRx out;
    // Upstream rejects fragmented release reports; do not carry those across
    // RPQ completions even though the ordinary RXQ supports reassembly.
    if(!dma||bytes<4||(little32(dma)&0xc000)!=0xc000){assembly.reset();return EINVAL;}
    if(assembly.feed(dma,bytes,out)!=AssemblyStatus::complete||out.packet.info.pkt_type!=7||out.packet.length%4)return EINVAL;
    // Validate the entire framing before allowing any ownership changes.
    for(size_t i=0;i<out.packet.length;i+=4){ReleaseReport report;
        if(!decodeRelease(out.packet.payload+i,4,report)||report.page>=Net80211PciQueue::count||!queues[dataChannel(report.qsel)])return EINVAL;}
    int result=0;
    for(size_t i=0;i<out.packet.length;i+=4){ReleaseReport report;
        decodeRelease(out.packet.payload+i,4,report);
        const int error=queues[dataChannel(report.qsel)]->releaseReport(report);if(error)result=error;}
    return result;
}
} }
