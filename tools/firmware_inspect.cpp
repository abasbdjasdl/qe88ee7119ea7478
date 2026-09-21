// SPDX-License-Identifier: BSD-3-Clause
#include "../src/FirmwarePlan.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
int main(int argc, char **argv) {
    using namespace rtl8852be::firmware;
    if(argc!=3) {fprintf(stderr,"usage: firmware_inspect FIRMWARE CUT\n");return 2;}
    char *end=nullptr;long cut=strtol(argv[2],&end,10);
    if(!end || end==argv[2] || *end || cut<0 || cut>15) return 2;
    FILE *f=fopen(argv[1],"rb");if(!f) {perror("firmware");return 2;}
    if(fseek(f,0,SEEK_END)!=0) {fclose(f);return 2;}
    long length=ftell(f);
    if(length<=0 || length>8*1024*1024) {fclose(f);return 2;}
    rewind(f);std::vector<uint8_t> data(static_cast<size_t>(length));
    const bool readOk=fread(data.data(),1,data.size(),f)==data.size();fclose(f);
    if(!readOk) return 2;
    Plan p;const auto result=parse(data.data(),data.size(),static_cast<uint8_t>(cut),p);
    if(result!=Status::ok) {fprintf(stderr,"%s\n",statusName(result));return 1;}
    printf("{\n  \"status\": \"%s\",\n  \"hardwareUploaded\": false,\n",statusName(result));
    printf("  \"cut\": %u, \"type\": %u, \"version\": \"%u.%u.%u.%u\",\n",p.cut,p.type,
           p.version&255,(p.version>>8)&255,(p.version>>16)&255,p.version>>24);
    printf("  \"imageOffset\": %zu, \"imageBytes\": %zu, \"headerBytes\": %zu,\n",p.imageOffset,p.imageBytes,p.headerBytes);
    printf("  \"packetBytes\": %u, \"packetCount\": %u,\n  \"sections\": [\n",packetBytes,p.packetCount);
    for(unsigned i=0;i<p.sectionCount;++i) {
        const auto &s=p.sections[i];
        printf("    {\"type\": %u, \"offset\": %zu, \"payloadBytes\": %u, \"transferBytes\": %u, \"address\": \"0x%08x\", \"checksumTrailer\": %s}%s\n",
               s.type,s.offset,s.payloadBytes,s.transferBytes,s.address,s.checksumTrailer?"true":"false",i+1==p.sectionCount?"":",");
    }
    puts("  ]\n}");
}
