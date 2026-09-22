// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2020 Realtek Corporation (upstream BSD option)
// DAV read protocol from rtw89 efuse.c/mac.c, pinned d1fced1b8a741dc9f92b47c69489c24385945f6e.
#pragma once
#include "EfuseCalibration.hpp"
namespace rtl8852be { namespace network {
enum class DavStatus {ok,invalid,busy,ioError,timeout,clockError,cancelled,cleanupFailed,requiresReset,decodeFailed};
struct DavResult {
    DavStatus status{DavStatus::invalid},primary{DavStatus::invalid};
    EfuseStatus decode{EfuseStatus::invalid};
    size_t bytesRead{},validBytes{};
    uint32_t commands{},polls{};
    bool cleanupAttempted{},busIdle{},readEngineIdle{},requiresReset{},logicalValid{};
};
// Validate the *entire* 0x270 command before native MMIO. Modes 2/3 and OTP
// programming are never exposed. The only mode-0 DAV trigger is a read.
inline bool davCommandAllowed(uint32_t command){
    if((command&0xfe000000u)!=0x80000000u)return false;
    const auto reg=uint8_t(command),data=uint8_t(command>>8),mask=uint8_t(command>>16);
    if(command&0x01000000u)return !data&&!mask&&(reg==0x63||reg==0x7a);
    if(reg==0x62)return mask==0xff&&data<96;
    return reg==0x63&&((mask==0xff&&data==0x40)||(mask==7&&data==0)||(mask==0xc0&&data==0));
}
// Caller serializes the complete operation against power changes and all other
// XTAL SI users. Raw I/O remains usable after cancellation for bounded draining.
// DAV has no DDV power-cut/isolation sequence in the pinned 8852B implementation.
template<class Io> class DavEfuseReader {
    Io &io_;DavResult result_{};uint64_t start_{},previous_{},davStarted_{},cleanupStarted_{},cleanupPrevious_{};
    uint32_t steps_{},cleanupSteps_{},cleanupDelay_{};
    bool pending_{},touched_{},poisoned_{},davPolling_{};
    static constexpr uint32_t port=0x270,pollBit=0x80000000u;
    bool fail(DavStatus status){if(result_.primary==DavStatus::ok)result_.primary=status;return false;}
    bool cleanActive(){
        const auto now=io_.nowUs();if(now<cleanupPrevious_)return false;cleanupPrevious_=now;
        return now-cleanupStarted_<100000;
    }
    bool cleanDelay(unsigned us){
        if(!cleanActive()||us>100000-cleanupDelay_)return false;
        cleanupDelay_+=us;return io_.delayUs(us)&&cleanActive();
    }
    bool active(){
        if(io_.cancelled())return fail(DavStatus::cancelled);
        const auto now=io_.nowUs();if(now<previous_)return fail(DavStatus::clockError);previous_=now;
        if(now-start_>=5000000||(davPolling_&&now-davStarted_>=10000)||++steps_>200000)return fail(DavStatus::timeout);
        return true;
    }
    bool siActive(bool cleanup,uint64_t started){
        if(cleanup)return cleanActive()&&cleanupPrevious_-started<50000;
        if(!active())return false;
        return previous_-started<50000||fail(DavStatus::timeout);
    }
    bool idle(bool cleanup,uint64_t started){
        // Iteration bound also applies when the backend clock is frozen.
        for(unsigned i=0;i<=1000;++i){
            if(!siActive(cleanup,started))return false;
            if(cleanup&&++cleanupSteps_>20000)return false;
            uint32_t value=0;
            if(!io_.read32(port,value)||value==0xffffffffu)return cleanup?false:fail(DavStatus::ioError);
            ++result_.polls;
            // A single MMIO operation can itself consume the deadline. Do not
            // accept a late ready value merely because the poll count is small.
            if(!siActive(cleanup,started))return false;
            if(!(value&pollBit)){pending_=false;return true;}
            if(i==1000)break;
            if(!(cleanup?cleanDelay(50):io_.delayUs(50)))return cleanup?false:fail(DavStatus::ioError);
        }
        return cleanup?false:fail(DavStatus::timeout);
    }
    bool transact(uint32_t command,uint8_t *data,bool cleanup=false){
        if(!davCommandAllowed(command))return cleanup?false:fail(DavStatus::invalid);
        if(!(cleanup?cleanActive():active()))return false;
        // Never overwrite a command whose completion has not been observed.
        if(pending_&&!idle(cleanup,cleanup?cleanupPrevious_:previous_))return false;
        const auto started=cleanup?cleanupPrevious_:previous_;
        touched_=pending_=true;++result_.commands;
        if(!io_.write32(port,command))return cleanup?false:fail(DavStatus::ioError);
        if(!idle(cleanup,started))return false;
        if(data&&!io_.read8(port+1,*data))return cleanup?false:fail(DavStatus::ioError);
        return siActive(cleanup,started);
    }
    bool write(uint8_t reg,uint8_t value,uint8_t mask){return transact(pollBit|uint32_t(mask)<<16|uint32_t(value)<<8|reg,nullptr);}
    bool read(uint8_t reg,uint8_t &value,bool cleanup=false){return transact(pollBit|0x01000000u|reg,&value,cleanup);}
    bool cleanup(){
        result_.cleanupAttempted=true;
        cleanupSteps_=cleanupDelay_=0;
        cleanupStarted_=cleanupPrevious_=io_.nowUs();
        // Never issue a reset, restore an arbitrary OTP mode, or start another
        // efuse read on error. Drain the outstanding SI and DAV read engines.
        if(!idle(true,cleanupStarted_))return false;
        result_.busIdle=true;
        for(unsigned i=0;i<=10000;++i){
            uint8_t control=0;
            if(!read(0x63,control,true)){result_.busIdle=!pending_;return false;}
            if((control&0xc0)==0x40||(control&0x20)){
                result_.readEngineIdle=true;return true;
            }
            if(i==10000||!cleanDelay(1))break;
        }
        return false;
    }
public:
    explicit DavEfuseReader(Io &io):io_(io){}
    DavResult readPhysical(uint32_t address,size_t bytes,uint8_t *out,size_t capacity){
        result_={};pending_=touched_=davPolling_=false;steps_=0;
        if(poisoned_){result_.status=result_.primary=DavStatus::requiresReset;result_.requiresReset=true;return result_;}
        if(!out||!bytes||bytes>capacity||address>=96||bytes>96-address)return result_;
        result_.primary=DavStatus::ok;start_=previous_=io_.nowUs();
        uint32_t initial=0;
        if(active()){
            if(!io_.read32(port,initial)||initial==0xffffffffu)fail(DavStatus::ioError);
            else if(initial&pollBit)fail(DavStatus::busy);
        }
        if(result_.primary==DavStatus::ok)for(size_t i=0;i<bytes;++i){
            // Full 0x40 prepares the reader; high bits select 0..95; clearing
            // mode bits starts a read. None is an OTP programming request.
            if(!write(0x63,0x40,0xff)||!write(0x62,uint8_t(address+i),0xff)||
               !write(0x63,0,7)||!write(0x63,0,0xc0))break;
            bool ready=false;davStarted_=previous_;davPolling_=true;
            for(unsigned attempt=0;attempt<=10000;++attempt){
                uint8_t control=0;if(!read(0x63,control))break;
                if(control&0x20){ready=true;break;}
                if(attempt==10000){fail(DavStatus::timeout);break;}
                if(!io_.delayUs(1)){fail(DavStatus::ioError);break;}
            }
            davPolling_=false;if(!ready)break;
            uint8_t value=0;if(!read(0x7a,value))break;
            out[i]=value;++result_.bytesRead;
        }
        result_.status=result_.primary;
        if(touched_&&!cleanup()){
            result_.status=DavStatus::cleanupFailed;result_.requiresReset=poisoned_=true;
        }
        if(result_.status==DavStatus::ok)result_.validBytes=result_.bytesRead;
        else for(size_t i=0;i<result_.bytesRead;++i)out[i]=0;
        return result_;
    }
    // Physical storage remains caller-owned for provenance/logging. Logical
    // output stays untouched on read or decode failure; never parse a short bank.
    DavResult readLogical(uint8_t *physical,size_t physicalCapacity,uint8_t *logical,size_t logicalCapacity){
        if(!physical||physicalCapacity<96||!logical||logicalCapacity<16){DavResult invalid{};return invalid;}
        const auto in=reinterpret_cast<uintptr_t>(physical),out=reinterpret_cast<uintptr_t>(logical);
        if(out>=in?out-in<96:in-out<16){DavResult invalid{};return invalid;}
        auto result=readPhysical(0,96,physical,physicalCapacity);
        if(result.status!=DavStatus::ok)return result;
        result.decode=decodeEfuse(physical,96,logical,16,4);
        result.logicalValid=result.decode==EfuseStatus::ok;
        if(!result.logicalValid)result.status=result.primary=DavStatus::decodeFailed;
        return result;
    }
};
} }
