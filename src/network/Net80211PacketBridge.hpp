// SPDX-License-Identifier: GPL-2.0-or-later
// Interface to the pinned itlwm/OpenBSD network stack; no device probing.
#pragma once
#include <sys/kpi_mbuf.h>
#include <stdint.h>
#include <stddef.h>
struct ieee80211com;
struct ieee80211_node;
namespace rtl8852be { namespace network {
// A frame and node are owned until releaseTx. The hardware driver must retain
// this lease until TX completion or proven DMA shutdown, never just publish.
struct TxLease {mbuf_t frame{};ieee80211_node *node{};size_t bytes{};};
// All functions require the controller's command gate, with net80211 attached.
// Consumes ethernet on every path. Returns an encrypted 802.11 frame on success.
int prepareEthernetTx(ieee80211com *ic,mbuf_t ethernet,TxLease &out);
// Management traffic goes first (including while scanning/authenticating).
// Data is dequeued only in RUN and outside TX_MGMT_ONLY; empty returns EAGAIN.
int prepareNextTx(ieee80211com *ic,TxLease &out);
void releaseTx(ieee80211com *ic,TxLease &lease);
// Complete first+last DMA segment. Includes Realtek RXWD and the on-air FCS.
// A valid initialized channel/RSSI must come from the chip/PHY report layer.
// No hardware-decrypted frames are accepted by this software-crypto bridge.
int deliverRealtekRx(ieee80211com *ic,const uint8_t *dma,size_t bytes,size_t descriptorOffset,uint8_t channel,int rssi);
} }
