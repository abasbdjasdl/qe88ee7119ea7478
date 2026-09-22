#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <cstring>
#include <cerrno>
struct FakeMbuf {std::vector<uint8_t> bytes;size_t packet{},first{};};
using mbuf_t=FakeMbuf*;
constexpr int MBUF_DONTWAIT=0;
extern bool bridgeFailAlloc,bridgeFailCopy;
extern unsigned bridgeFreed,bridgeInputs;
extern std::vector<uint8_t> bridgeInputBytes;
inline size_t mbuf_pkthdr_len(mbuf_t m){return m->packet;}
inline size_t mbuf_len(mbuf_t m){return m->first;}
inline void *mbuf_data(mbuf_t m){return m->bytes.data();}
inline void *mbuf_pkthdr_rcvif(mbuf_t){return nullptr;}
inline void mbuf_freem(mbuf_t m){if(m){++bridgeFreed;delete m;}}
inline int mbuf_allocpacket(int,size_t n,unsigned*,mbuf_t *out){if(bridgeFailAlloc)return ENOBUFS;*out=new FakeMbuf;(*out)->bytes.resize(n);return 0;}
inline int mbuf_copyback(mbuf_t m,size_t off,size_t n,const void *p,int){if(bridgeFailCopy)return ENOBUFS;m->bytes.resize(off+n);memcpy(m->bytes.data()+off,p,n);m->packet=m->first=off+n;return 0;}
inline int mbuf_copydata(mbuf_t m,size_t off,size_t n,void *out){if(off>m->bytes.size()||n>m->bytes.size()-off)return EINVAL;memcpy(out,m->bytes.data()+off,n);return 0;}
inline int mbuf_pullup(mbuf_t *m,size_t n){if((*m)->packet<n){mbuf_freem(*m);*m=nullptr;return ENOBUFS;}(*m)->first=(*m)->packet;return 0;}
