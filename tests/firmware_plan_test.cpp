// SPDX-License-Identifier: BSD-3-Clause
#include "../src/FirmwarePlan.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace rtl8852be::firmware;
void put(std::vector<uint8_t> &b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b.at(p+i) = static_cast<uint8_t>(v >> (8*i));
}
// Synthetic metadata and zero payload only; no modified Realtek firmware files.
std::vector<uint8_t> fixture() {
    std::vector<uint8_t> b(32+48+2021);
    b[0]=255; b[1]=1; b[16]=1; b[17]=5;
    put(b,20,32); put(b,24,static_cast<uint32_t>(b.size()-32));
    put(b,32,0x88520102); b[57]=1;
    put(b,64,0xb8970000); put(b,68,0x02000000|2021);
    return b;
}
void expect(const std::vector<uint8_t> &b, Status expected, uint8_t cut=1) {
    Plan plan{}; plan.valid=true; plan.packetCount=123;
    assert(parse(b.data(),b.size(),cut,plan)==expected);
    if (expected != Status::ok) assert(!plan.valid && plan.packetCount==0);
}
void verifyChunks(const Plan &p, size_t fileBytes) {
    size_t covered=0;
    for (uint32_t i=0;i<p.packetCount;++i) {
        Chunk c; assert(chunkAt(p,i,c));
        assert(c.bytes>0 && c.bytes<=packetBytes && fits(c.offset,c.bytes,fileBytes));
        assert(c.offset==p.imageOffset+p.headerBytes+covered);
        const auto &s=p.sections[c.section];
        assert(fits(c.sectionOffset,c.bytes,s.transferBytes));
        covered+=c.bytes;
    }
    assert(covered==p.imageBytes-p.headerBytes);
    Chunk c; assert(!chunkAt(p,p.packetCount,c));
}
int main(int argc,char **argv) {
    auto b=fixture(); Plan plan;
    assert(parse(b.data(),b.size(),1,plan)==Status::ok);
    assert(plan.sections[0].address==0x18970000 && plan.packetCount==2);
    verifyChunks(plan,b.size());
    Chunk tail; assert(chunkAt(plan,1,tail) && tail.bytes==1 && tail.sectionOffset==2020);
    assert(parse(nullptr,123,1,plan)==Status::truncated);
    for(size_t n=0;n<b.size();++n) {
        assert(parse(b.data(),n,1,plan)!=Status::ok && !plan.valid);
    }
    auto v=b; v[0]=0; expect(v,Status::badContainer);
    v=b; v[1]=0; expect(v,Status::badContainer);
    v=b; v[1]=65; expect(v,Status::tooManyImages);
    v=b; put(v,20,0xfffffff0); expect(v,Status::imageBounds);
    v=b; put(v,24,0xffffffff); expect(v,Status::imageBounds);
    v=b; put(v,20,16); expect(v,Status::imageBounds);
    expect(b,Status::noMatchingImage,2);
    v=b; v[18]=1; expect(v,Status::noMatchingImage);
    v=b; v[17]=3; expect(v,Status::noMatchingImage);
    v=b; v[17]=1; expect(v,Status::ok);
    v=b; v[47]=1; expect(v,Status::unsupportedHeader);
    v=b; put(v,32,0x88510102); expect(v,Status::wrongFamily);
    for(auto count:{0,11,255}) {v=b;v[57]=static_cast<uint8_t>(count);expect(v,Status::sectionCount);}
    v=b; put(v,60,0x10000); expect(v,Status::dynamicHeader);
    v=b; put(v,68,0x0f000000|2021); expect(v,Status::unsupportedSection);
    v=b; put(v,68,0x09000000|2021); put(v,72,1); expect(v,Status::unsupportedSection);
    v=b; put(v,68,0x02000000); expect(v,Status::sectionBounds);
    v=b; put(v,68,0x02ffffff); expect(v,Status::sectionBounds);
    v=b; put(v,64,0xffffffff); expect(v,Status::addressOverflow);
    v=b; put(v,68,0x02000000|2020); expect(v,Status::trailingData);
    v=b; put(v,68,0x12000000|2021); expect(v,Status::sectionBounds);
    v=b; put(v,68,0x32000000|2013);
    assert(parse(v.data(),v.size(),1,plan)==Status::ok);
    assert(plan.sections[0].checksumTrailer && plan.sections[0].redownload && plan.sections[0].transferBytes==2021);
    // Two entries, non-overlapping images; CE preferred even when normal first.
    std::vector<uint8_t> two(48+2*(b.size()-32));
    two[0]=255;two[1]=2;
    for(unsigned i=0;i<2;++i) {
        size_t e=16+i*16, start=48+i*(b.size()-32);
        two[e]=1;two[e+1]=i?5:1;
        put(two,e+4,static_cast<uint32_t>(start));put(two,e+8,static_cast<uint32_t>(b.size()-32));
        for(size_t j=32;j<b.size();++j) two[start+j-32]=b[j];
    }
    assert(parse(two.data(),two.size(),1,plan)==Status::ok && plan.type==5 && plan.imageOffset==48+b.size()-32);
    v=two;v[17]=5;expect(v,Status::ambiguousImage);
    v=two;put(v,36,48);expect(v,Status::imageOverlap);
    // Dynamic header eight-byte prefix plus opaque feature bytes.
    v=b;v.insert(v.begin()+80,16,0);put(v,24,static_cast<uint32_t>(v.size()-32));
    v[46]=64;put(v,60,0x10000);put(v,80,16);
    assert(parse(v.data(),v.size(),1,plan)==Status::ok && plan.headerBytes==64);
    put(v,80,15);expect(v,Status::dynamicHeader);
    puts("PASS: firmware selection, truncation, bounds, overlap, ambiguity, sections and packet tails");
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        assert(fseek(f,0,SEEK_END)==0);long size=ftell(f);assert(size>0 && size<8*1024*1024);
        rewind(f);std::vector<uint8_t> real(static_cast<size_t>(size));
        assert(fread(real.data(),1,real.size(),f)==real.size());fclose(f);
        for(uint8_t cut:{uint8_t(1),uint8_t(2)}) {
            const auto result=parse(real.data(),real.size(),cut,plan);
            if(result!=Status::ok) fprintf(stderr,"real firmware: %s\n",statusName(result));
            assert(result==Status::ok && plan.type==5 && plan.sectionCount==3);
            verifyChunks(plan,real.size());
            printf("PASS: real firmware cut=%u version=%u.%u.%u.%u image=%zu bytes=%zu header=%zu packets=%u\n",cut,
                   plan.version&255,(plan.version>>8)&255,(plan.version>>16)&255,plan.version>>24,
                   plan.imageOffset,plan.imageBytes,plan.headerBytes,plan.packetCount);
        }
        expect(real,Status::noMatchingImage,0);
    }
}
