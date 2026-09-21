// SPDX-License-Identifier: BSD-3-Clause
// RTL8852BE PCI wire formats adapted from rtw89 pci.c/pci.h (Realtek,
// BSD-3-Clause option), d1fced1b8a741dc9f92b47c69489c24385945f6e.
// Copyright(c) 2020-2022 Realtek Corporation
// Caller owns synchronization, physical mappings, cache sync and MMIO.
#pragma once
#include "NetworkDescriptors.hpp"
namespace rtl8852be { namespace network {
inline void store16(uint8_t *p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void store32(uint8_t *p,uint32_t v){store16(p,uint16_t(v));store16(p+2,uint16_t(v>>16));}
inline bool dma32Range(uint64_t address,size_t length){
    return length && address<=0xffffffffULL && length-1<=0xffffffffULL-address;
}
// The single-band 8852BE implements ACH0..3, MGMT0 and HI0. Do not route an
// unknown qsel to channel 0 or channel 12 (firmware queue).
inline int dataChannel(uint8_t qsel){
    switch(qsel){case 0:return 0;case 1:return 1;case 2:return 2;case 3:return 3;
        case 0x12:return 8;case 0x11:return 9;default:return -1;}
}
struct TxWire {uint8_t wd[64]{};uint8_t bd[8]{};size_t wdBytes{};};
// One nonaggregated MSDU per page: TXWD[/TXWI], TXWP and one 32-bit address
// entry. The 8852BE uses fill_txaddr_info, NOT fill_txaddr_info_v1.
inline DescriptorStatus encodePciTx(TxInfo info,uint16_t page,uint64_t wdDma,
                                    uint64_t frameDma,TxWire &out){
    out={};
    if(page>0x7fff || dataChannel(info.qsel)<0 || info.ch_dma!=dataChannel(info.qsel))
        return DescriptorStatus::unrepresentable;
    const size_t wdBytes=(info.en_wd_info?48:24)+16;
    if(!dma32Range(wdDma,wdBytes)||!dma32Range(frameDma,info.pkt_size))
        return DescriptorStatus::invalidLength;
    info.addr_info_nr=1;
    TxWire encoded{};size_t body=0;
    auto status=encodeTx(info,encoded.wd,sizeof(encoded.wd),body);
    if(status!=DescriptorStatus::ok)return status;
    store16(encoded.wd+body,uint16_t(page|0x8000)); // TXWP valid, other seqs zero
    store16(encoded.wd+body+8,info.pkt_size);
    store16(encoded.wd+body+10,0x8001); // MSDU_LS, address count 1 (upstream)
    store32(encoded.wd+body+12,uint32_t(frameDma));
    store16(encoded.bd,uint16_t(wdBytes));store16(encoded.bd+2,0x4000); // TXBD LS
    store32(encoded.bd+4,uint32_t(wdDma));encoded.wdBytes=wdBytes;out=encoded;
    return DescriptorStatus::ok;
}
inline bool encodeRxBd(uint64_t address,size_t bytes,uint8_t (&out)[8]){
    if(bytes<20||bytes>0x3fff||!dma32Range(address,bytes))return false;
    store16(out,uint16_t(bytes));store16(out+2,0);store32(out+4,uint32_t(address));return true;
}
struct ReleaseReport {uint16_t page{};uint8_t qsel{},macid{},status{};bool polluted{};};
inline bool decodeRelease(const uint8_t *data,size_t bytes,ReleaseReport &out){
    out={};if(!data||bytes<4)return false;
    const uint32_t word=little32(data);
    ReleaseReport r{uint16_t((word>>16)&0x7fff),uint8_t((word>>8)&31),
                    uint8_t(word),uint8_t((word>>13)&7),bool(word>>31)};
    if(dataChannel(r.qsel)<0||r.status>3)return false;
    out=r;return true;
}
enum class QueueStatus {ok,full,invalid,duplicate,notReady};
// A bounded ownership ledger, independent of MMIO. BD-consumed and RPQ report
// may arrive in either order. Neither alone permits reuse. Host must serialize
// calls on its workloop, and drain RPQ before resetting/reusing page IDs.
template<size_t Count> class TxOwnership {
    static_assert(Count>=2 && Count<=0x8000,"TX ring size");
    struct Slot {bool used{},consumed{},reported{};uint8_t qsel{},macid{},status{};void *cookie{};};
    Slot pages_[Count]{};uint16_t ring_[Count]{};
    size_t producer_{},consumer_{},pendingBd_{},outstanding_{},nextPage_{};
public:
    TxOwnership()=default;
    TxOwnership(const TxOwnership &)=delete;
    TxOwnership &operator=(const TxOwnership &)=delete;
    struct Ticket {uint16_t page{},bd{},nextProducer{};};
    struct Completion {void *cookie{};uint8_t status{};bool polluted{};};
private:
    bool polluted_[Count]{};
public:
    size_t outstanding()const{return outstanding_;}
    size_t pendingBd()const{return pendingBd_;}
    size_t producer()const{return producer_;}
    // peek makes no state change: prepare/copy descriptors before commit.
    QueueStatus peek(Ticket &ticket)const{
        if(pendingBd_==Count-1||outstanding_==Count)return QueueStatus::full;
        for(size_t n=0;n<Count;++n){size_t p=(nextPage_+n)%Count;
            if(!pages_[p].used){ticket={uint16_t(p),uint16_t(producer_),uint16_t((producer_+1)%Count)};return QueueStatus::ok;}}
        return QueueStatus::full;
    }
    // Commit immediately before the publish barrier/doorbell. If a doorbell
    // write fails ambiguously, retain the committed lease until DMA shutdown.
    QueueStatus commit(const Ticket &ticket,uint8_t qsel,uint8_t macid,void *cookie){
        Ticket current{};
        if(!cookie||dataChannel(qsel)<0||peek(current)!=QueueStatus::ok||
           ticket.page!=current.page||ticket.bd!=current.bd||ticket.nextProducer!=current.nextProducer)
            return QueueStatus::invalid;
        pages_[ticket.page]={true,false,false,qsel,macid,0,cookie};polluted_[ticket.page]=false;
        ring_[producer_]=ticket.page;producer_=ticket.nextProducer;
        ++pendingBd_;++outstanding_;nextPage_=(ticket.page+1)%Count;return QueueStatus::ok;
    }
    QueueStatus consumedTo(size_t hardwareConsumer){
        if(hardwareConsumer>=Count)return QueueStatus::invalid;
        const size_t advance=(hardwareConsumer+Count-consumer_)%Count;
        if(advance>pendingBd_)return QueueStatus::invalid;
        for(size_t n=0;n<advance;++n){pages_[ring_[consumer_]].consumed=true;consumer_=(consumer_+1)%Count;}
        pendingBd_-=advance;return QueueStatus::ok;
    }
    QueueStatus report(const ReleaseReport &report){
        if(report.page>=Count||report.status>3)return QueueStatus::invalid;
        auto &p=pages_[report.page];
        if(!p.used||p.qsel!=report.qsel||p.macid!=report.macid)return QueueStatus::invalid;
        if(p.reported)return QueueStatus::duplicate;
        p.reported=true;p.status=report.status;polluted_[report.page]=report.polluted;return QueueStatus::ok;
    }
    QueueStatus take(uint16_t page,Completion &out){
        out={};if(page>=Count)return QueueStatus::invalid;
        auto &p=pages_[page];
        if(!p.used||!p.consumed||!p.reported)return QueueStatus::notReady;
        out={p.cookie,p.status,polluted_[page]};p={};polluted_[page]=false;--outstanding_;return QueueStatus::ok;
    }
    // No automatic destructor release or timeout-based reclamation. The host
    // may invoke this ONLY after disabling bus mastering and proving DMA idle,
    // and must discard old hardware reports before starting a new epoch.
    template<class Release> void reclaimAfterDmaStopped(Release release){
        for(size_t p=0;p<Count;++p)if(pages_[p].used)release(pages_[p].cookie);
        for(auto &p:pages_)p={};for(auto &p:polluted_)p=false;
        producer_=consumer_=pendingBd_=outstanding_=nextPage_=0;
    }
};
} }
