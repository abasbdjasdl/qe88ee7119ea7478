// SPDX-License-Identifier: GPL-2.0-or-later
// Local administrator control tool; no keys in argv or diagnostic output.
#include "../src/network/WirelessControl.hpp"
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <cstdio>
#include <cstring>
using namespace rtl8852be::network;
namespace {
struct Connection {
    io_connect_t value{IO_OBJECT_NULL};
    ~Connection(){if(value)IOServiceClose(value);}
    kern_return_t close(){
        if(!value)return kIOReturnNotOpen;
        const auto result=IOServiceClose(value);value=IO_OBJECT_NULL;return result;
    }
};
bool zero(const void *data,size_t length){
    const auto *p=static_cast<const uint8_t*>(data);
    for(size_t i=0;i<length;++i)if(p[i])return false;
    return true;
}
bool validEvent(const authevents::Event &e,size_t length){
    if(length!=sizeof(e)||e.version!=1||!e.generation||e.kind>authevents::eapol||
       e.length>sizeof(e.body)||(e.flags&~7u)||!zero(e.reserved,sizeof(e.reserved)))return false;
    const bool active=(e.flags&1)!=0,overflow=(e.flags&2)!=0;
    if(active){
        if(!e.epoch||!e.operation||!e.channel||!authevents::unicast(e.own)||
           !authevents::unicast(e.peer)||authevents::equal(e.own,e.peer))return false;
    }else if(e.epoch||e.operation||e.channel||!zero(e.own,6)||!zero(e.peer,6))return false;
    if(overflow){if(!active||!e.dropped||e.dropped>9||e.kind!=authevents::empty)return false;}
    else if(e.dropped)return false;
    if(e.kind==authevents::empty){
        if(e.length||e.sampledAtUs||(e.flags&4)||!zero(e.source,6))return false;
    }else{
        if(!active||overflow||e.length<(e.kind==authevents::eapol?4u:6u))return false;
        if(e.kind!=authevents::eapol&&!authevents::equal(e.source,e.peer))return false;
    }
    // The wire ABI must not expose bytes outside its declared payload length.
    return zero(e.body+e.length,sizeof(e.body)-e.length);
}
struct Probe {
    unsigned failures{};
    bool check(const char *name,kern_return_t got,kern_return_t expected){
        std::printf("probe %s got=0x%08x expected=0x%08x\n",name,got,expected);
        if(got!=expected){++failures;return false;}return true;
    }
    bool command(io_connect_t connection,uint32_t selector,const char *name,kern_return_t expected){
        return check(name,IOConnectCallStructMethod(connection,selector,nullptr,0,nullptr,nullptr),expected);
    }
    bool read(io_connect_t connection,const char *name,kern_return_t expected){
        authevents::Event event{};size_t length=sizeof(event);
        const auto result=IOConnectCallStructMethod(connection,control::captureRead,nullptr,0,&event,&length);
        bool ok=check(name,result,expected);
        if(result==KERN_SUCCESS){
            const bool shape=validEvent(event,length);
            ok=check("capture-event-shape",shape?KERN_SUCCESS:kIOReturnBadArgument,KERN_SUCCESS)&&ok;
            if(shape)std::printf("capture metadata kind=%u bytes=%u flags=%u dropped=%u generation=%llu epoch=%llu operation=%llu\n",
                event.kind,event.length,unsigned(event.flags),event.dropped,(unsigned long long)event.generation,
                (unsigned long long)event.epoch,(unsigned long long)event.operation);
        }
        // EAPOL/management payloads are never printed or retained by this probe.
        selection::wipe(&event,sizeof(event));return ok;
    }
};
kern_return_t probe(io_service_t service,io_connect_t connection){
    // No valid join/disconnect or frame transmission is sent. Capture owns only
    // a short-lived observation queue, never the authentication state machine.
    Probe p;
    p.command(connection,0xffffffff,"unknown-selector",kIOReturnUnsupported);
    p.command(connection,control::join,"empty-join",kIOReturnBadArgument);
    control::Join invalid{};
    p.check("invalid-version",IOConnectCallStructMethod(connection,control::join,&invalid,sizeof(invalid),nullptr,nullptr),kIOReturnBadArgument);
    control::Status status{};size_t length=sizeof(status)-1;
    p.check("short-output",IOConnectCallStructMethod(connection,control::status,nullptr,0,&status,&length),kIOReturnBadArgument);
    uint64_t scalar=0;
    p.check("unexpected-scalar",IOConnectCallScalarMethod(connection,control::status,&scalar,1,nullptr,nullptr),kIOReturnBadArgument);
    uint8_t byte=0;
    p.check("capture-begin-input",IOConnectCallStructMethod(connection,control::captureBegin,&byte,1,nullptr,nullptr),kIOReturnBadArgument);
    p.check("capture-end-input",IOConnectCallStructMethod(connection,control::captureEnd,&byte,1,nullptr,nullptr),kIOReturnBadArgument);
    authevents::Event event{};length=sizeof(event)-1;
    p.check("capture-short-output",IOConnectCallStructMethod(connection,control::captureRead,nullptr,0,&event,&length),kIOReturnBadArgument);
    length=sizeof(event);
    p.check("capture-read-input",IOConnectCallStructMethod(connection,control::captureRead,&byte,1,&event,&length),kIOReturnBadArgument);
    length=sizeof(event);
    p.check("capture-begin-output",IOConnectCallStructMethod(connection,control::captureBegin,nullptr,0,&event,&length),kIOReturnBadArgument);
    selection::wipe(&event,sizeof(event));
    p.read(connection,"capture-read-before-begin",kIOReturnNotOpen);
    p.command(connection,control::captureEnd,"capture-end-before-begin",kIOReturnNotOpen);
    if(p.command(connection,control::captureBegin,"capture-begin",KERN_SUCCESS)){
        p.command(connection,control::captureBegin,"capture-begin-same-owner",KERN_SUCCESS);
        p.read(connection,"capture-read-owner",KERN_SUCCESS);
        Connection second;
        if(p.check("capture-second-open",IOServiceOpen(service,mach_task_self(),control::connectionType,&second.value),KERN_SUCCESS)){
            p.command(second.value,control::captureBegin,"capture-second-exclusive",kIOReturnExclusiveAccess);
            p.read(second.value,"capture-second-read-denied",kIOReturnNotOpen);
            p.command(second.value,control::captureEnd,"capture-second-end-denied",kIOReturnNotOpen);
            p.read(connection,"capture-owner-after-denied-end",KERN_SUCCESS);
            if(p.command(connection,control::captureEnd,"capture-owner-end",KERN_SUCCESS)){
                p.read(connection,"capture-read-after-end",kIOReturnNotOpen);
                p.command(connection,control::captureEnd,"capture-double-end",kIOReturnNotOpen);
                if(p.command(second.value,control::captureBegin,"capture-acquire-after-end",KERN_SUCCESS)){
                    p.read(second.value,"capture-second-read-owner",KERN_SUCCESS);
                    // Deliberately omit captureEnd: clientClose must release it.
                    p.check("capture-owner-close",second.close(),KERN_SUCCESS);
                    if(p.command(connection,control::captureBegin,"capture-acquire-after-close",KERN_SUCCESS)){
                        p.read(connection,"capture-read-after-close-release",KERN_SUCCESS);
                        p.command(connection,control::captureEnd,"capture-final-end",KERN_SUCCESS);
                        p.read(connection,"capture-final-read-denied",kIOReturnNotOpen);
                    }
                }
            }
            // Also close second on every failure path. The outer connection is
            // closed by main even if a failed RPC left it owning the capture.
            if(second.value)p.check("capture-second-cleanup-close",second.close(),KERN_SUCCESS);
        }
    }
    std::printf("probe failures=%u; RX availability, non-admin and concurrent provider-stop tests remain separate\n",p.failures);
    return p.failures?kIOReturnError:KERN_SUCCESS;
}
}
int main(int argc,char **argv){
    if(argc!=2||(strcmp(argv[1],"status")&&strcmp(argv[1],"probe")&&strcmp(argv[1],"join")&&strcmp(argv[1],"disconnect"))){
        std::fprintf(stderr,"Usage: r16-wireless-control status|probe|join|disconnect (join reads a binary v1 request from stdin)\n");return 2;
    }
    io_iterator_t iterator=IO_OBJECT_NULL;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("R16NetworkController"),&iterator)!=KERN_SUCCESS)return 3;
    io_service_t service=IOIteratorNext(iterator),extra=IOIteratorNext(iterator);IOObjectRelease(iterator);
    if(!service||extra){if(service)IOObjectRelease(service);if(extra)IOObjectRelease(extra);
        std::fprintf(stderr,"Expected exactly one R16 controller\n");return 3;}
    Connection opened;auto &connection=opened.value;
    auto result=IOServiceOpen(service,mach_task_self(),control::connectionType,&connection);
    if(result!=KERN_SUCCESS){IOObjectRelease(service);std::fprintf(stderr,"Control open failed: 0x%08x\n",result);return 4;}
    if(!strcmp(argv[1],"status")||!strcmp(argv[1],"probe")){
        control::Status s{};size_t length=sizeof(s);
        result=IOConnectCallStructMethod(connection,control::status,nullptr,0,&s,&length);
        if(result==KERN_SUCCESS&&(length!=sizeof(s)||s.version!=control::version||s.count>control::cacheCapacity))result=kIOReturnBadArgument;
        if(result==KERN_SUCCESS)std::printf("version=%u link=%u flags=%u cached=%u generation=%llu channel=%u signalPercent=%u\n",
            s.version,s.link,s.flags,s.count,(unsigned long long)s.selectionGeneration,s.current.channel,s.current.signalPercent);
        if(result==KERN_SUCCESS&&!strcmp(argv[1],"probe"))result=probe(service,connection);
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
    IOObjectRelease(service);
    const auto closed=opened.close();
    if(closed!=KERN_SUCCESS){std::fprintf(stderr,"Control close failed: 0x%08x\n",closed);if(result==KERN_SUCCESS)result=closed;}
    if(result!=KERN_SUCCESS){std::fprintf(stderr,"Control request failed: 0x%08x\n",result);return 5;}
    return 0;
}
