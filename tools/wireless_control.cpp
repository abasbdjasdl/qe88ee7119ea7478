// SPDX-License-Identifier: GPL-2.0-or-later
// Local administrator control tool; no keys in argv or diagnostic output.
#include "../src/network/WirelessControl.hpp"
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <cstdio>
#include <cstring>
using namespace rtl8852be::network;
int main(int argc,char **argv){
    if(argc!=2||(strcmp(argv[1],"status")&&strcmp(argv[1],"join")&&strcmp(argv[1],"disconnect"))){
        std::fprintf(stderr,"Usage: r16-wireless-control status|join|disconnect (join reads a binary v1 request from stdin)\n");return 2;
    }
    io_iterator_t iterator=IO_OBJECT_NULL;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("R16NetworkController"),&iterator)!=KERN_SUCCESS)return 3;
    io_service_t service=IOIteratorNext(iterator),extra=IOIteratorNext(iterator);IOObjectRelease(iterator);
    if(!service||extra){if(service)IOObjectRelease(service);if(extra)IOObjectRelease(extra);
        std::fprintf(stderr,"Expected exactly one R16 controller\n");return 3;}
    io_connect_t connection=IO_OBJECT_NULL;
    auto result=IOServiceOpen(service,mach_task_self(),control::connectionType,&connection);IOObjectRelease(service);
    if(result!=KERN_SUCCESS){std::fprintf(stderr,"Control open failed: 0x%08x\n",result);return 4;}
    if(!strcmp(argv[1],"status")){
        control::Status s{};size_t length=sizeof(s);
        result=IOConnectCallStructMethod(connection,control::status,nullptr,0,&s,&length);
        if(result==KERN_SUCCESS&&(length!=sizeof(s)||s.version!=control::version||s.count>64))result=kIOReturnBadArgument;
        if(result==KERN_SUCCESS)std::printf("version=%u link=%u flags=%u cached=%u generation=%llu channel=%u signalPercent=%u\n",
            s.version,s.link,s.flags,s.count,(unsigned long long)s.selectionGeneration,s.current.channel,s.current.signalPercent);
    }else if(!strcmp(argv[1],"disconnect")){
        result=IOConnectCallStructMethod(connection,control::disconnect,nullptr,0,nullptr,nullptr);
    }else{
        control::Join request{};selection::Join parsed;
        const bool valid=std::fread(&request,1,sizeof(request),stdin)==sizeof(request)&&
            std::fgetc(stdin)==EOF&&control::decode(request,parsed);
        result=valid?IOConnectCallStructMethod(connection,control::join,&request,sizeof(request),nullptr,nullptr):kIOReturnBadArgument;
        selection::wipe(&request,sizeof(request));selection::wipe(&parsed,sizeof(parsed));
        if(result==KERN_SUCCESS)std::puts("Connection request queued; association is not yet confirmed");
    }
    IOServiceClose(connection);
    if(result!=KERN_SUCCESS){std::fprintf(stderr,"Control request failed: 0x%08x\n",result);return 5;}
    return 0;
}
