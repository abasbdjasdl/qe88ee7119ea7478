#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <cstring>
struct FakeMbuf {std::vector<uint8_t> bytes;};
using mbuf_t=FakeMbuf*;
inline size_t mbuf_pkthdr_len(mbuf_t m){return m->bytes.size();}
inline int mbuf_copydata(mbuf_t m,size_t offset,size_t bytes,void *out){if(offset>m->bytes.size()||bytes>m->bytes.size()-offset)return 1;std::memcpy(out,m->bytes.data()+offset,bytes);return 0;}
