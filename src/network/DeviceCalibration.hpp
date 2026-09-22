// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "EfuseReader.hpp"
#include "DavEfuseReader.hpp"
namespace rtl8852be { namespace network {
// Allocate this object off the kernel stack. Raw banks and both logical banks
// remain together with the parsed calibration for this physical device epoch.
struct CalibrationSnapshot {
    uint8_t ddv[1216]{},dav[96]{},phycap[128]{},logical[2064]{};
    BoardCalibration board{};PhyCalibration phy{};uint8_t cut{};
};
enum class CalibrationStage {idle,ddv,decodeDdv,dav,phycap,parse,complete,failed};
struct CalibrationReadResult {
    CalibrationStage stage{CalibrationStage::idle},failedStage{CalibrationStage::idle};
    EfuseReadResult ddv{},phycap{};DavResult dav{};EfuseStatus decodeDdv{EfuseStatus::invalid};
    bool requiresRecovery{};
};
// Composition of the real DDV/DAV readers; no optional hooks or success flags
// supplied by a controller. One-shot per device epoch, serialized with power/
// XTAL SI access. Partial/blank identity data never becomes a usable snapshot.
template<class DdvReader,class DavReader> class DeviceCalibration {
    DdvReader &ddv_;DavReader &dav_;CalibrationSnapshot snapshot_{};CalibrationReadResult result_{};
    bool fail(bool recovery=false){result_.failedStage=result_.stage;result_.stage=CalibrationStage::failed;
        result_.requiresRecovery=recovery;return false;}
public:
    DeviceCalibration(DdvReader &ddv,DavReader &dav):ddv_(ddv),dav_(dav){}
    DeviceCalibration(const DeviceCalibration&)=delete;DeviceCalibration&operator=(const DeviceCalibration&)=delete;
    const CalibrationReadResult &result()const{return result_;}
    const CalibrationSnapshot *snapshot()const{return result_.stage==CalibrationStage::complete?&snapshot_:nullptr;}
    bool read(uint8_t verifiedCut){
        if(result_.stage!=CalibrationStage::idle||verifiedCut>1)return false;
        snapshot_.cut=verifiedCut;result_.stage=CalibrationStage::ddv;
        result_.ddv=ddv_.readDdv(verifiedCut,0,1216,snapshot_.ddv,sizeof(snapshot_.ddv));
        if(result_.ddv.status!=EfuseReadStatus::ok||result_.ddv.validBytes!=1216||!result_.ddv.restored)
            return fail(result_.ddv.status==EfuseReadStatus::cleanupFailed||
                (result_.ddv.status==EfuseReadStatus::ok&&!result_.ddv.restored));
        result_.stage=CalibrationStage::decodeDdv;
        result_.decodeDdv=decodeEfuse(snapshot_.ddv,1216,snapshot_.logical,2048,4);
        if(result_.decodeDdv!=EfuseStatus::ok)return fail();
        result_.stage=CalibrationStage::dav;
        result_.dav=dav_.readLogical(snapshot_.dav,96,snapshot_.logical+2048,16);
        if(result_.dav.status!=DavStatus::ok||result_.dav.validBytes!=96||!result_.dav.logicalValid||
           !result_.dav.busIdle||!result_.dav.readEngineIdle)
            return fail(result_.dav.requiresReset||(result_.dav.status==DavStatus::ok&&
                (!result_.dav.busIdle||!result_.dav.readEngineIdle)));
        result_.stage=CalibrationStage::phycap;
        result_.phycap=ddv_.readDdv(verifiedCut,0x580,128,snapshot_.phycap,sizeof(snapshot_.phycap));
        if(result_.phycap.status!=EfuseReadStatus::ok||result_.phycap.validBytes!=128||!result_.phycap.restored)
            return fail(result_.phycap.status==EfuseReadStatus::cleanupFailed||
                (result_.phycap.status==EfuseReadStatus::ok&&!result_.phycap.restored));
        result_.stage=CalibrationStage::parse;
        if(!parseBoardCalibration(snapshot_.logical,2064,snapshot_.board)||!snapshot_.board.identityValid||
           !parsePhyCalibration(snapshot_.phycap,128,snapshot_.phy))return fail();
        result_.stage=CalibrationStage::complete;return true;
    }
};
} }
