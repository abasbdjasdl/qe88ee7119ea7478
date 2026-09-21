// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option)
// RTL8852B AX RF v1 access, adapted from pinned rtw89 phy.c/reg.h.
#pragma once
#include <stdint.h>
namespace rtl8852be { namespace network {
enum class RadioIoStatus {ok,invalid,ioError,timeout,cancelled};
// IO uses absolute BAR2 offsets and boolean read32(offset,out), write32,
// delayUs and cancelled methods. Owning workloop serializes all operations.
// Power/clock enable and PHY initialization order remain controller duties.
template<class Io> class RadioAccess {
    Io &io_;RadioIoStatus status_{RadioIoStatus::ok};
    bool ready(){if(io_.cancelled()){status_=RadioIoStatus::cancelled;return false;}return true;}
    bool read(uint32_t relative,uint32_t &v){
        if(!ready())return false;
        if(!io_.read32(0x10000+relative,v)){status_=RadioIoStatus::ioError;return false;}return true;
    }
    bool write(uint32_t relative,uint32_t v){
        if(!ready())return false;
        if(!io_.write32(0x10000+relative,v)){status_=RadioIoStatus::ioError;return false;}return true;
    }
    bool delay(unsigned us){if(!ready())return false;if(!io_.delayUs(us)){status_=RadioIoStatus::ioError;return false;}return true;}
    static unsigned shift(uint32_t mask){unsigned n=0;while(!(mask&1)){mask>>=1;++n;}return n;}
    bool update(uint32_t address,uint32_t mask,uint32_t data){
        uint32_t value=0;
        if(mask!=0xffffffff&&!read(address,value))return false;
        return write(address,(value&~mask)|((data<<shift(mask))&mask));
    }
    bool poll(uint32_t mask,bool set){
        // Finite iteration count also bounds a stalled/non-monotonic clock.
        for(unsigned n=0;n<=30;++n){uint32_t v=0;if(!read(0x174c,v))return false;
            if(v==0xffffffff){status_=RadioIoStatus::ioError;return false;}
            if(set?(v&mask)==mask:(v&mask)==0)return true;
            if(n!=30&&!delay(1))return false;
        }
        status_=RadioIoStatus::timeout;return false;
    }
    bool validate(uint8_t path,uint32_t address,uint32_t mask){
        status_=RadioIoStatus::ok;
        if(path>1||(address&~0x100ffu)||!mask||(mask&~0xfffffu)){
            status_=RadioIoStatus::invalid;return false;
        }
        return ready();
    }
public:
    explicit RadioAccess(Io &io):io_(io){}
    RadioIoStatus status()const{return status_;}
    bool readRf(uint8_t path,uint32_t address,uint32_t mask,uint32_t &value){
        value=0;if(!validate(path,address,mask))return false;uint32_t raw=0;
        if(address&0x10000){
            if(!read((path?0xf000:0xe000)+((address&255)<<2),raw))return false;
        }else{
            if(!poll(0x03000000,false)||!update(0x378,0x7ff,(uint32_t(path)<<8)|address)||
               !delay(2)||!poll(0x04000000,true)||!read(0x174c,raw))return false;
            if(raw==0xffffffff){status_=RadioIoStatus::ioError;return false;}
        }
        value=(raw&mask)>>shift(mask);return true;
    }
    bool writeRf(uint8_t path,uint32_t address,uint32_t mask,uint32_t data){
        if(!validate(path,address,mask))return false;
        if(address&0x10000)return update((path?0xf000:0xe000)+((address&255)<<2),mask,data)&&delay(1);
        if(!poll(0x03000000,false))return false;
        data&=0xfffff;uint32_t maskEnable=0;
        if(mask!=0xfffff){
            if(!update(0x374,0xfffff,mask))return false;
            maskEnable=0x80000000;data=(data<<shift(mask))&0xfffff;
        }
        return write(0x370,maskEnable|(uint32_t(path)<<28)|(address<<20)|data);
    }
    // The final analog write has no next operation to poll its completion.
    bool drain(){status_=RadioIoStatus::ok;return poll(0x03000000,false);}
    bool writeBaseband(uint32_t relative,uint32_t value){
        status_=RadioIoStatus::ok;
        if((relative&3)||relative>=0x10000){status_=RadioIoStatus::invalid;return false;}
        return write(relative,value);
    }
    bool readBaseband(uint32_t relative,uint32_t &value){
        value=0;status_=RadioIoStatus::ok;
        if((relative&3)||relative>=0x10000){status_=RadioIoStatus::invalid;return false;}
        return read(relative,value);
    }
    bool updateBaseband(uint32_t relative,uint32_t mask,uint32_t value){
        status_=RadioIoStatus::ok;
        if((relative&3)||relative>=0x10000||!mask){status_=RadioIoStatus::invalid;return false;}
        return update(relative,mask,value);
    }
    bool delayMicroseconds(unsigned us){status_=RadioIoStatus::ok;return delay(us);}
};
} }
