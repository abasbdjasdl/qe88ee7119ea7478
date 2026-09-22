#pragma once
#include <sys/kpi_mbuf.h>
constexpr unsigned IFF_RUNNING=1,IEEE80211_S_RUN=4,IEEE80211_F_TX_MGMT_ONLY=1;
constexpr unsigned IEEE80211_FC1_PROTECTED=0x40,IEEE80211_FC0_TYPE_MASK=0x0c;
constexpr unsigned IEEE80211_FC0_TYPE_DATA=8,IEEE80211_FC0_TYPE_MGT=0;
constexpr unsigned IEEE80211_FC0_VERSION_MASK=3,IEEE80211_FC0_VERSION_0=0,IEEE80211_KEY_SWCRYPTO=1;
struct ieee80211_frame {uint8_t i_fc[2]{},rest[22]{};};
struct ieee80211_node {};
struct ieee80211_key {unsigned k_flags=IEEE80211_KEY_SWCRYPTO;};
struct FakeIf {unsigned if_flags=IFF_RUNNING;void *iface=reinterpret_cast<void*>(1),*netStat=reinterpret_cast<void*>(1);int if_snd{};};
struct ieee80211com {struct {FakeIf ac_if;} ic_ac;unsigned ic_state=IEEE80211_S_RUN,ic_xflags{};int ic_mgtq{};struct {unsigned ic_freq{};} ic_channels[256];};
struct ieee80211_rxinfo {uint32_t rxi_tstamp{};int rxi_rssi{};uint8_t rxi_chan{};};
inline mbuf_t mq_dequeue(int*){return nullptr;}
inline mbuf_t ifq_dequeue(int*){return nullptr;}
inline void ieee80211_release_node(ieee80211com*,ieee80211_node*){}
inline ieee80211_key *ieee80211_get_txkey(ieee80211com*,ieee80211_frame*,ieee80211_node*){static ieee80211_key key;return &key;}
inline mbuf_t ieee80211_encrypt(ieee80211com*,mbuf_t m,ieee80211_key*){return m;}
inline mbuf_t ieee80211_encap(FakeIf*,mbuf_t m,ieee80211_node**){mbuf_freem(m);return nullptr;}
inline ieee80211_node *ieee80211_find_rxnode(ieee80211com*,ieee80211_frame*){static ieee80211_node node;return &node;}
inline void ieee80211_input(FakeIf*,mbuf_t m,ieee80211_node*,ieee80211_rxinfo*){++bridgeInputs;bridgeInputBytes=m->bytes;mbuf_freem(m);}
