// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace network {
struct RadioRegister {uint32_t address,value;};
enum class RadioTableKind {baseband,radio,gain};
struct RadioTable {const RadioRegister *registers;size_t count;RadioTableKind kind;uint8_t path;};
extern const RadioTable bbTable,radioATable,radioBTable,nctlTable,gainTable;
enum class RadioTableStatus {ok,invalid,unmatched,malformed,ioError,cancelled};
struct RadioTableResult {RadioTableStatus status{RadioTableStatus::invalid};size_t row{},selected{},writes{},delays{};uint32_t target{};};
// Select a package exactly as upstream rtw89_phy_sel_headline. Do not replace
// an unknown RFE with a guessed common-board value.
inline RadioTableStatus selectRadioTable(const RadioTable &table,uint8_t rfe,uint8_t cut,size_t &headlines,uint32_t &target){
    headlines=0;target=0;
    if(!table.registers||!table.count||table.count>65536||table.path>1)return RadioTableStatus::invalid;
    while(headlines<table.count&&(table.registers[headlines].address>>28)==15)++headlines;
    if(!headlines){target=table.registers[0].address&0x0fffffff;return RadioTableStatus::ok;}
    if(headlines==table.count)return RadioTableStatus::malformed;
    const uint32_t compare=(uint32_t(rfe)<<16)|cut;
    for(size_t i=0;i<headlines;++i)if((table.registers[i].address&0x0fffffff)==compare){target=compare;return RadioTableStatus::ok;}
    const uint32_t anyCut=(uint32_t(rfe)<<16)|255;
    for(size_t i=0;i<headlines;++i)if((table.registers[i].address&0x0fffffff)==anyCut){target=anyCut;return RadioTableStatus::ok;}
    for(unsigned phase=0;phase<2;++phase){bool found=false;unsigned maximum=0;
        for(size_t i=0;i<headlines;++i){const auto value=table.registers[i].address;
            if(((value>>16)&255)!=(phase?255:rfe))continue;
            const unsigned cv=value&255;if(cv>=maximum){maximum=cv;target=value&0x0fffffff;found=true;}}
        if(found)return RadioTableStatus::ok;
    }
    return RadioTableStatus::unmatched;
}
// Validate every branch before performing any writes. The pinned tables use
// flat IF/CHECK/ELIF/CHECK/ELSE/END groups; nesting is intentionally rejected.
inline RadioTableStatus validateRadioBranches(const RadioTable &table,size_t begin,uint32_t selected){
    bool branch=false,needsCheck=false,found=false,seenElse=false;
    for(size_t i=begin;i<table.count;++i){const auto address=table.registers[i].address;const auto op=address>>28;
        switch(op){
            case 8:if(branch)return RadioTableStatus::malformed;branch=true;needsCheck=true;found=false;seenElse=false;break;
            case 9:if(!branch||needsCheck||seenElse)return RadioTableStatus::malformed;needsCheck=true;break;
            case 4:if(!branch||!needsCheck)return RadioTableStatus::malformed;needsCheck=false;
                if((table.registers[i-1].address&0x0fffffff)==selected)found=true;break;
            case 10:if(!branch||needsCheck||seenElse)return RadioTableStatus::malformed;
                if(!found)return RadioTableStatus::unmatched;seenElse=true;break;
            case 11:if(!branch||needsCheck)return RadioTableStatus::malformed;branch=false;break;
            default:if(needsCheck||op==15)return RadioTableStatus::malformed;break;
        }
    }
    return branch?RadioTableStatus::malformed:RadioTableStatus::ok;
}
// Sink implements cancelled(), bbWrite(address,value), rfWrite(path,address,
// value), gainRecord(address,value), delayUs(us). All return bool except
// cancelled(). RF v1's 0xf9..0xfe are RF addresses, NOT delay opcodes.
template<class Sink> RadioTableResult applyRadioTable(const RadioTable &table,uint8_t rfe,uint8_t cut,Sink &sink){
    RadioTableResult result;size_t start=0;
    result.status=selectRadioTable(table,rfe,cut,start,result.target);if(result.status!=RadioTableStatus::ok)return result;
    result.status=validateRadioBranches(table,start,result.target);if(result.status!=RadioTableStatus::ok)return result;
    bool matched=true,found=false;uint32_t target=0;
    for(size_t i=start;i<table.count;++i){result.row=i;if(sink.cancelled()){result.status=RadioTableStatus::cancelled;return result;}
        const auto &r=table.registers[i];
        switch(r.address>>28){
            case 8:case 9:target=r.address&0x0fffffff;continue;
            case 10:matched=false;continue;
            case 11:matched=true;found=false;continue;
            case 4:matched=!found&&target==result.target;if(matched)found=true;continue;
            default:break;
        }
        if(!matched)continue;++result.selected;bool ok=false;
        if(table.kind==RadioTableKind::baseband){
            unsigned delay=0;switch(r.address){case 0xfe:delay=50000;break;case 0xfd:delay=5000;break;
                case 0xfc:delay=1000;break;case 0xfb:delay=50;break;case 0xfa:delay=5;break;case 0xf9:delay=1;break;}
            if(delay){ok=sink.delayUs(delay);if(ok)++result.delays;}
            else if(r.value==0xbabecafe)continue;
            else{ok=sink.bbWrite(r.address,r.value);if(ok)++result.writes;}
        }else if(table.kind==RadioTableKind::radio){ok=sink.rfWrite(table.path,r.address,r.value);if(ok)++result.writes;}
        else{ok=sink.gainRecord(r.address,r.value);if(ok)++result.writes;}
        if(!ok){result.status=RadioTableStatus::ioError;return result;}
    }
    result.status=RadioTableStatus::ok;return result;
}
} }
