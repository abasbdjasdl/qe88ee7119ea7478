// SPDX-License-Identifier: GPL-2.0-or-later
// Uses itlwm's OpenBSD encapsulation, software ciphers and receive state machine.
// No Intel register access, no fake association/link state, no key logging.
#include "NetworkDescriptors.hpp"
#include "Net80211PacketBridge.hpp"
#include <compat.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_proto.h>
namespace rtl8852be { namespace network {
void releaseTx(ieee80211com *ic,TxLease &lease){
    if(lease.frame)mbuf_freem(lease.frame);
    if(lease.node&&ic)ieee80211_release_node(ic,lease.node);
    lease={};
}
static int protectFrame(ieee80211com *ic,TxLease &out){
    struct ieee80211_frame header{};
    if(mbuf_pkthdr_len(out.frame)<sizeof(header)||mbuf_copydata(out.frame,0,sizeof(header),&header)!=0){releaseTx(ic,out);return EINVAL;}
    if(header.i_fc[1]&IEEE80211_FC1_PROTECTED){
        auto *key=ieee80211_get_txkey(ic,&header,out.node);
        if(!key||!(key->k_flags&IEEE80211_KEY_SWCRYPTO)){releaseTx(ic,out);return EACCES;}
        out.frame=ieee80211_encrypt(ic,out.frame,key); // consumes input even on failure
        if(!out.frame){releaseTx(ic,out);return ENOBUFS;}
    }
    out.bytes=mbuf_pkthdr_len(out.frame);
    if(!out.bytes||out.bytes>16383){releaseTx(ic,out);return EMSGSIZE;}
    return 0;
}
int prepareEthernetTx(ieee80211com *ic,mbuf_t ethernet,TxLease &out){
    // Do not overwrite a pending frame's ownership.
    if(out.frame||out.node){if(ethernet)mbuf_freem(ethernet);return EBUSY;}
    out={};
    if(!ethernet)return EINVAL;
    if(!ic||ic->ic_state!=IEEE80211_S_RUN){mbuf_freem(ethernet);return ENETDOWN;}
    if(mbuf_pkthdr_len(ethernet)<14){mbuf_freem(ethernet);return EINVAL;}
    if(mbuf_len(ethernet)<14&&mbuf_pullup(&ethernet,14)!=0)return ENOBUFS;
    auto *ifp=&ic->ic_ac.ac_if;
    out.frame=ieee80211_encap(ifp,ethernet,&out.node);
    if(!out.frame){out={};return ENOBUFS;}
    return protectFrame(ic,out);
}
int prepareNextTx(ieee80211com *ic,TxLease &out){
    if(out.frame||out.node)return EBUSY;
    out={};
    if(!ic||!(ic->ic_ac.ac_if.if_flags&IFF_RUNNING))return ENETDOWN;
    auto frame=mq_dequeue(&ic->ic_mgtq);
    if(frame){
        out.frame=frame;
        out.node=reinterpret_cast<ieee80211_node *>(mbuf_pkthdr_rcvif(frame));
        if(!out.node){releaseTx(ic,out);return EINVAL;}
        return protectFrame(ic,out);
    }
    if(ic->ic_state!=IEEE80211_S_RUN||(ic->ic_xflags&IEEE80211_F_TX_MGMT_ONLY))return EAGAIN;
    frame=ifq_dequeue(&ic->ic_ac.ac_if.if_snd);
    if(!frame)return EAGAIN;
    return prepareEthernetTx(ic,frame,out);
}
int deliverRealtekRx(ieee80211com *ic,const uint8_t *dma,size_t bytes,size_t descriptorOffset,uint8_t channel,int rssi){
    if(!ic||!ic->ic_ac.ac_if.iface||!ic->ic_ac.ac_if.netStat)return ENETDOWN;
    RxPacket rx;
    if(decodeRx(dma,bytes,descriptorOffset,rx)!=DescriptorStatus::ok)return EINVAL;
    // C2H/PPDU/TX reports belong to their hardware handlers, never net80211.
    if(rx.info.pkt_type!=0||hardwareDecrypted(rx.info))return EOPNOTSUPP;
    if(rx.length<sizeof(ieee80211_frame)+4)return EINVAL;
    const auto type=rx.payload[0]&IEEE80211_FC0_TYPE_MASK;
    if(type!=IEEE80211_FC0_TYPE_DATA&&type!=IEEE80211_FC0_TYPE_MGT)return EOPNOTSUPP;
    if((rx.payload[0]&IEEE80211_FC0_VERSION_MASK)!=IEEE80211_FC0_VERSION_0)return EINVAL;
    if(!channel||!ic->ic_channels[channel].ic_freq)return EINVAL;
    const size_t frameBytes=rx.length-4; // rtw89 advertises RX_INCLUDES_FCS
    mbuf_t frame=nullptr;
    if(mbuf_allocpacket(MBUF_DONTWAIT,frameBytes,nullptr,&frame)!=0)return ENOBUFS;
    if(mbuf_copyback(frame,0,frameBytes,rx.payload,MBUF_DONTWAIT)!=0){mbuf_freem(frame);return ENOBUFS;}
    if(mbuf_len(frame)<sizeof(ieee80211_frame)&&mbuf_pullup(&frame,sizeof(ieee80211_frame))!=0)return ENOBUFS;
    auto *header=static_cast<ieee80211_frame *>(mbuf_data(frame));
    auto *node=ieee80211_find_rxnode(ic,header);
    if(!node){mbuf_freem(frame);return ENOENT;}
    ieee80211_rxinfo info{};info.rxi_tstamp=rx.info.free_run_cnt;info.rxi_rssi=rssi;info.rxi_chan=channel;
    // flags=0 deliberately: net80211 must perform decryption and replay checks.
    ieee80211_input(&ic->ic_ac.ac_if,frame,node,&info);
    ieee80211_release_node(ic,node);
    return 0;
}
} }
