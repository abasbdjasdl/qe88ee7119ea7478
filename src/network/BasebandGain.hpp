// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option)
#pragma once
#include "RadioTables.hpp"
#include "EfuseCalibration.hpp"
namespace rtl8852be { namespace network {
// AX table data, not MMIO addresses. Two receive paths on RTL8852B.
struct BasebandGain {
    int8_t lna_gain[8][2][7]{},tia_gain[8][2][2]{},lna_gain_bypass[8][2][7]{};
    int8_t lna_op1db[8][2][7]{},tia_lna_op1db[8][2][8]{};
    int8_t rpl_ofst_20[8][2]{},rpl_ofst_40[8][2][9]{},rpl_ofst_80[8][2][13]{},rpl_ofst_160[8][2][15]{};
};
inline bool applyGainRecord(BasebandGain *out,uint8_t rfe,uint32_t address,uint32_t value){
    const unsigned type=address&255,path=(address>>8)&255,band=(address>>16)&255,config=address>>24;
    if(path>=2||band>=8||(address>=0xf9&&address<=0xfe))return false;
    int8_t *target=nullptr;unsigned count=0,start=0;
    if(config==0||config==2||config==3){
        if(type<2){start=type?4:0;count=type?3:4;
            if(out)target=config==0?out->lna_gain[band][path]:config==2?out->lna_gain_bypass[band][path]:out->lna_op1db[band][path];
        }else if(config==0&&type==2){count=2;if(out)target=out->tia_gain[band][path];}
        else if(config==3&&(type==2||type==3)){start=type==3?4:0;count=4;if(out)target=out->tia_lna_op1db[band][path];}
        else return false;
    }else if(config==1){
        const unsigned width=type>>4,rxsc=type&15;
        if(width==0){count=1;if(out)target=&out->rpl_ofst_20[band][path];}
        else if(width<=3){
            if(out)target=width==1?out->rpl_ofst_40[band][path]:width==2?out->rpl_ofst_80[band][path]:out->rpl_ofst_160[band][path];
            start=rxsc;
            if(rxsc==0)count=1;
            else if(rxsc==1)count=width==1?2:4;
            else if(rxsc==9&&width>=2)count=width==2?2:4;
            else if(width==3&&rxsc==5)count=4;
            else if(width==3&&rxsc==13)count=2;
            else return false;
        }else return false;
    }else if(config==4&&rfe<50)return true; // upstream eFEM-only records
    else return false;
    if(out)for(unsigned i=0;i<count;++i){target[start+i]=signedByte(uint8_t(value));value>>=8;}
    return true;
}
struct GainCollector {
    BasebandGain *gain;uint8_t rfe;
    bool cancelled()const{return false;}
    bool bbWrite(uint32_t,uint32_t){return false;}bool rfWrite(uint8_t,uint32_t,uint32_t){return false;}bool delayUs(unsigned){return false;}
    bool gainRecord(uint32_t a,uint32_t v){return applyGainRecord(gain,rfe,a,v);}
};
inline RadioTableResult loadBasebandGain(const RadioTable &table,uint8_t rfe,uint8_t cut,BasebandGain &out){
    if(table.kind!=RadioTableKind::gain)return {};
    GainCollector check{nullptr,rfe};auto result=applyRadioTable(table,rfe,cut,check);
    if(result.status!=RadioTableStatus::ok)return result;
    out={};GainCollector collector{&out,rfe};return applyRadioTable(table,rfe,cut,collector);
}
} }
