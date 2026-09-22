// SPDX-License-Identifier: BSD-3-Clause
// Test reader composition/publication. Physical reader fault coverage is in the
// separate DDV and DAV tests; these readers supply completed banks explicitly.
#include "../src/network/DeviceCalibration.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace n=rtl8852be::network;
struct Ddv {
    uint8_t cutSeen{},bank[1216],phy[128];unsigned calls{},failCall{},shortCall{},dirtyCall{};bool busy{};
    Ddv(){std::memset(bank,255,sizeof bank);std::memset(phy,255,sizeof phy);unsigned cursor=4;
        // Logical board offsets 0x2c8 (RFE at +2) and 0x400 (PCIe MAC).
        const uint8_t rfe[]={0,0,5,0,0,0,0,0},mac[]={2,0x12,0x34,0x56,0x78,0x9a,0,0};
        for(unsigned row=0;row<2;++row){const unsigned block=(row?0x400:0x2c8)/8;
            bank[cursor++]=uint8_t(block>>4);bank[cursor++]=uint8_t((block&15)<<4);
            const auto *v=row?mac:rfe;for(unsigned i=0;i<8;++i)bank[cursor++]=v[i];}
        phy[0x5e9-0x580]=0xaa;phy[0x5d6-0x580]=0xfb;
    }
    n::EfuseReadResult readDdv(uint8_t cut,uint32_t address,size_t bytes,uint8_t *out,size_t capacity){
        ++calls;cutSeen=cut;assert(capacity==bytes);assert((calls==1&&address==0&&bytes==1216)||(calls==2&&address==0x580&&bytes==128));
        n::EfuseReadResult r{};
        if(busy){r.status=r.primary=n::EfuseReadStatus::busy;return r;}
        std::memcpy(out,calls==1?bank:phy,bytes);r.bytesRead=bytes;r.validBytes=bytes;
        r.status=r.primary=n::EfuseReadStatus::ok;r.restored=true;
        if(calls==failCall){r.status=r.primary=n::EfuseReadStatus::ioError;r.validBytes=0;}
        if(calls==shortCall)--r.validBytes;
        if(calls==dirtyCall){r.status=n::EfuseReadStatus::cleanupFailed;r.restored=false;}
        return r;
    }
};
struct Dav {
    unsigned calls{};bool fail{},dirty{},shortRead{},invalidLogical{};
    n::DavResult readLogical(uint8_t *raw,size_t capacity,uint8_t *logical,size_t bytes){
        ++calls;assert(capacity==96&&bytes==16);std::memset(raw,255,capacity);std::memset(logical,0x77,bytes);
        n::DavResult r{};r.status=r.primary=n::DavStatus::ok;r.validBytes=r.bytesRead=96;
        r.busIdle=r.readEngineIdle=r.logicalValid=true;
        if(fail){r.status=r.primary=n::DavStatus::ioError;r.validBytes=0;r.logicalValid=false;}
        if(dirty){r.status=n::DavStatus::cleanupFailed;r.requiresReset=true;r.busIdle=false;}
        if(shortRead)--r.validBytes;
        if(invalidLogical){r.logicalValid=false;r.status=n::DavStatus::decodeFailed;}
        return r;
    }
};
int main(){
    for(uint8_t cut=0;cut<=1;++cut){Ddv ddv;Dav dav;n::DeviceCalibration<Ddv,Dav> reader(ddv,dav);
        assert(!reader.snapshot()&&reader.read(cut));const auto *s=reader.snapshot();assert(s);
        assert(ddv.calls==2&&dav.calls==1&&ddv.cutSeen==cut&&s->cut==cut);
        assert(s->board.identityValid&&s->board.mac[0]==2&&s->board.mac[5]==0x9a&&s->board.rfe==5);
        assert(s->phy.powerValid&&s->phy.tssiTrim[0][0]==-5&&s->logical[2048]==0x77&&s->logical[2063]==0x77);
        assert(!reader.read(cut)&&ddv.calls==2&&reader.snapshot()==s);}
    for(unsigned point=0;point<13;++point){Ddv ddv;Dav dav;n::DeviceCalibration<Ddv,Dav> reader(ddv,dav);
        if(point==0)ddv.failCall=1;if(point==1)ddv.shortCall=1;if(point==2)ddv.dirtyCall=1;
        if(point==3){ddv.bank[4]=0xff;} // no programmed identity
        if(point==4)std::memset(ddv.bank,0,sizeof ddv.bank); // truncated final physical record
        if(point==5)dav.fail=true;if(point==6)dav.dirty=true;if(point==7)dav.shortRead=true;
        if(point==8)dav.invalidLogical=true;if(point==9)ddv.failCall=2;if(point==10)ddv.shortCall=2;
        if(point==11)ddv.dirtyCall=2;if(point==12)ddv.busy=true;
        assert(!reader.read(1)&&!reader.snapshot()&&reader.result().stage==n::CalibrationStage::failed);
        assert(reader.result().requiresRecovery==(point==2||point==6||point==11));
        const auto before=ddv.calls+dav.calls;assert(!reader.read(1)&&before==ddv.calls+dav.calls);
        if(point<=2||point==4||point==12)assert(dav.calls==0&&ddv.calls==1);
        if(point>=5&&point<=8)assert(ddv.calls==1&&dav.calls==1);
    }
    {Ddv ddv;Dav dav;n::DeviceCalibration<Ddv,Dav> r(ddv,dav);assert(!r.read(2)&&ddv.calls==0&&dav.calls==0);}
    puts("PASS: atomic DDV/DAV/PHY calibration composition, exact bank bounds/cut, parsed identity/power/TSSI, per-stage failures/recovery retention and no partial snapshot publication; modeled reader composition");
}
