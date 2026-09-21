// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option)
#pragma once
#include "RadioAccess.hpp"
#include "RadioFirmware.hpp"
#include "EfuseCalibration.hpp"
namespace rtl8852be { namespace network {
// Individual RTL8852B programming stages. The caller must implement the full
// power/MAC/BB reset/TX power/RFK/channel order before enabling RX or TX.
// A stage succeeding does not mean the radio is initialized or connected.
template<class Io> class RadioInitialization {
    Io &io_;RadioAccess<Io> rf_;
    struct Sink {
        Io &io;RadioAccess<Io> &rf;
        bool cancelled(){return io.cancelled();}
        bool bbWrite(uint32_t a,uint32_t v){return rf.writeBaseband(a,v);}
        bool rfWrite(uint8_t p,uint32_t a,uint32_t v){return rf.writeRf(p,a,0xfffff,v);}
        bool gainRecord(uint32_t,uint32_t){return false;}
        bool delayUs(unsigned us){return rf.delayMicroseconds(us);}
    };
    struct Check {
        bool cancelled()const{return false;}
        bool bbWrite(uint32_t a,uint32_t){return !(a&3)&&a<0x10000;}
        bool rfWrite(uint8_t p,uint32_t a,uint32_t v){return p<2&&!(a&~0x100ffu)&&!(v&~0xfffffu);}
        bool gainRecord(uint32_t,uint32_t){return false;}
        bool delayUs(unsigned us){return us<=50000;}
    };
public:
    explicit RadioInitialization(Io &io):io_(io),rf_(io){}
    RadioIoStatus ioStatus()const{return rf_.status();}
    RadioTableResult baseband(const RadioTable &table,uint8_t rfe,uint8_t cut){
        if(table.kind!=RadioTableKind::baseband)return {};
        Check check;auto result=applyRadioTable(table,rfe,cut,check);
        if(result.status!=RadioTableStatus::ok)return result;
        Sink sink{io_,rf_};return applyRadioTable(table,rfe,cut,sink);
    }
    RadioIoStatus nctl(uint8_t rfe,uint8_t cut){
        Check check;if(applyRadioTable(nctlTable,rfe,cut,check).status!=RadioTableStatus::ok)return RadioIoStatus::invalid;
        if(!rf_.updateBaseband(0x0c60,3,3)||!rf_.updateBaseband(0x0c6c,1,1)||
           !rf_.updateBaseband(0x58ac,0x08000000,1)||!rf_.updateBaseband(0x78ac,0x08000000,1)||
           !rf_.updateBaseband(0x0c60,2,1)||!rf_.writeBaseband(0x8000,8))return rf_.status();
        bool ready=false;
        for(unsigned n=0;n<=100;++n){uint32_t value=0;
            if(!rf_.writeBaseband(0x8080,4)||!rf_.delayMicroseconds(1)||!rf_.readBaseband(0x8080,value))return rf_.status();
            if(value==4){ready=true;break;}
            if(n!=100&&!rf_.delayMicroseconds(10))return rf_.status();
        }
        if(!ready)return RadioIoStatus::timeout;
        Sink sink{io_,rf_};auto result=applyRadioTable(nctlTable,rfe,cut,sink);
        if(result.status==RadioTableStatus::ok)return RadioIoStatus::ok;
        if(result.status==RadioTableStatus::cancelled)return RadioIoStatus::cancelled;
        return rf_.status()==RadioIoStatus::ok?RadioIoStatus::ioError:rf_.status();
    }
    // Prepare firmware pages before any hardware writes. Expose them only
    // after the complete selected path was written and SWSI drained.
    RadioTableResult radio(const RadioTable &table,uint8_t rfe,uint8_t cut,uint8_t *scratch,size_t bytes,RadioFirmwarePages &out){
        out={};RadioFirmwarePages prepared;RadioTableResult result;
        result.status=prepareRadioFirmware(table,rfe,cut,scratch,bytes,prepared);
        if(result.status!=RadioTableStatus::ok)return result;
        Sink sink{io_,rf_};result=applyRadioTable(table,rfe,cut,sink);
        if(result.status!=RadioTableStatus::ok)return result;
        if(!rf_.drain()){result.status=rf_.status()==RadioIoStatus::cancelled?RadioTableStatus::cancelled:RadioTableStatus::ioError;return result;}
        out=prepared;return result;
    }
    RadioIoStatus powerTrim(const PhyCalibration &calibration){
        // Retain original per-device validity semantics: when either path is
        // programmed, both trim fields are applied as the chip implementation does.
        if(calibration.thermalValid)for(uint8_t p=0;p<2;++p){const auto raw=calibration.thermalTrim[p];
            if(!rf_.writeRf(p,0x43,0xf0000,((raw&1)<<3)|((raw&0x1f)>>1)))return rf_.status();
        }
        if(calibration.paBiasValid)for(uint8_t p=0;p<2;++p){const auto raw=calibration.paBiasTrim[p];
            if(!rf_.writeRf(p,0x60,0xf000,raw&15)||!rf_.writeRf(p,0x60,0xf0000,raw>>4))return rf_.status();
        }
        if(calibration.thermalValid||calibration.paBiasValid){if(!rf_.drain())return rf_.status();}
        return io_.cancelled()?RadioIoStatus::cancelled:RadioIoStatus::ok;
    }
};
} }
