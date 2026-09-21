// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2020 Realtek Corporation (upstream BSD option)
// RTL8852B DDV OTP read path adapted from rtw89/efuse.c. No OTP write command.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace network {
enum class EfuseReadStatus {ok,invalid,busy,ioError,timeout,clockError,cancelled,cleanupFailed};
struct EfuseReadResult {
    EfuseReadStatus status{EfuseReadStatus::invalid},primary{EfuseReadStatus::invalid};
    size_t bytesRead{},validBytes{};uint32_t polls{},last{};bool cleanupAttempted{},restored{};
};
// Raw I/O must remain usable during cancellation so cleanup can run. The owner
// holds the PCI device and serializes power changes through its workloop.
template<class Io> class EfuseReader {
    Io &io_;uint16_t originalIso_{};uint8_t originalPmc_{};uint32_t originalBurst_{};
    uint64_t begin_{},previous_{};bool touched_{},burst_{};
    EfuseReadResult result_{};
    bool active(){
        if(io_.cancelled()){result_.primary=EfuseReadStatus::cancelled;return false;}
        const auto now=io_.nowUs();if(now<previous_){result_.primary=EfuseReadStatus::clockError;return false;}previous_=now;
        if(now-begin_>=5000000){result_.primary=EfuseReadStatus::timeout;return false;}return true;
    }
    bool failed(){result_.primary=EfuseReadStatus::ioError;return false;}
    bool iso(uint16_t mask,uint16_t bits){
        uint16_t v;if(!io_.read16(0,v)||v==0xffff)return false;
        return io_.write16(0,uint16_t((v&~mask)|(bits&mask)));
    }
    bool pmc(uint8_t bits){
        uint8_t v;if(!io_.read8(0xcc,v)||v==0xff)return false;
        return io_.write8(0xcc,uint8_t((v&~4)|(bits&4)));
    }
    bool burst(uint32_t bits){
        uint32_t v;if(!io_.read32(0x38,v)||v==0xffffffff)return false;
        return io_.write32(0x38,(v&~0x80000u)|(bits&0x80000));
    }
    bool restore(){
        result_.cleanupAttempted=true;bool ok=true;
        // Attempt every cleanup step even if a previous access failed. Device
        // removal/write rejection is reported, never silently treated as restored.
        if(burst_)ok=burst(originalBurst_)&&ok;
        ok=iso(0x100,0x100)&&ok;
        ok=iso(0x8000,0)&&ok;
        ok=io_.delayUs(1000)&&ok;
        ok=iso(0x4000,0)&&ok;
        ok=pmc(originalPmc_)&&ok;
        uint16_t v16=0;uint8_t v8=0;uint32_t v32=0;
        ok=io_.read16(0,v16)&&v16!=0xffff&&(v16&0xc100)==(originalIso_&0xc100)&&ok;
        ok=io_.read8(0xcc,v8)&&v8!=0xff&&(v8&4)==(originalPmc_&4)&&ok;
        if(burst_)ok=io_.read32(0x38,v32)&&v32!=0xffffffff&&(v32&0x80000)==(originalBurst_&0x80000)&&ok;
        result_.restored=ok;return ok;
    }
    bool enable(){
        if(!active())return false;
        if(!io_.read16(0,originalIso_)||originalIso_==0xffff||
           !io_.read8(0xcc,originalPmc_)||originalPmc_==0xff)return failed();
        // Only acquire a quiescent rail. Do not turn off a rail owned by another
        // operation or guess how to resume an already-powered eFuse transaction.
        if((originalIso_&0xc100)!=0x100||(originalPmc_&4)){
            result_.primary=EfuseReadStatus::busy;return false;
        }
        if(burst_&&(!io_.read32(0x38,originalBurst_)||originalBurst_==0xffffffff))return failed();
        if(!active())return false;
        touched_=true;
        if(!pmc(4)||!iso(0x4000,0x4000)||!io_.delayUs(1000))return failed();
        if(!active())return false;
        if(!iso(0x8000,0x8000)||!iso(0x100,0))return failed();
        if(burst_&&!burst(0x80000))return failed();return true;
    }
public:
    explicit EfuseReader(Io &io):io_(io){}
    EfuseReadResult readDdv(uint8_t cut,uint32_t address,size_t bytes,uint8_t *out,size_t capacity){
        result_={};touched_=false;burst_=cut==0;
        if(!out||!bytes||bytes>capacity||address>=0x600||bytes>0x600-address||
           !((address<1216&&bytes<=1216-address)||(address>=0x580&&bytes<=0x600-address)))return result_;
        begin_=previous_=io_.nowUs();result_.primary=EfuseReadStatus::ok;
        if(enable())for(size_t i=0;i<bytes;++i){
            if(!active())break;
            // Bits 26:16 encode address; EF_RDY (29) clear requests a read.
            // No write-enable or data-programming bits are ever asserted.
            if(!io_.write32(0x30,uint32_t(address+i)<<16)){failed();break;}
            const auto started=previous_;bool ready=false;
            for(unsigned attempt=0;attempt<1000000;++attempt){
                if(!active())break;
                if(!io_.delayUs(1)||!io_.read32(0x30,result_.last)||result_.last==0xffffffff){failed();break;}
                ++result_.polls;
                if(result_.last&0x20000000){ready=true;break;}
                if(previous_-started>=1000000){result_.primary=EfuseReadStatus::timeout;break;}
            }
            if(!ready){if(result_.primary==EfuseReadStatus::ok)result_.primary=EfuseReadStatus::timeout;break;}
            out[i]=uint8_t(result_.last);++result_.bytesRead;
        }
        result_.status=result_.primary;
        if(touched_&&!restore())result_.status=EfuseReadStatus::cleanupFailed;
        if(result_.status==EfuseReadStatus::ok)result_.validBytes=result_.bytesRead;
        else for(size_t i=0;i<result_.bytesRead;++i)out[i]=0;
        return result_;
    }
};
} }
