// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "MacEfuseIo.hpp"
#include "MacDavEfuseIo.hpp"
#include "DeviceCalibration.hpp"
namespace rtl8852be { namespace network {
// Provider/map are borrowed and held by the controller. This owns all raw I/O,
// reader and snapshot lifetimes in declaration order. Allocate on the heap.
class MacDeviceCalibration {
    MacEfuseIo ddvIo_;MacDavEfuseIo davIo_;
    EfuseReader<MacEfuseIo> ddv_;DavEfuseReader<MacDavEfuseIo> dav_;
    DeviceCalibration<EfuseReader<MacEfuseIo>,DavEfuseReader<MacDavEfuseIo>> calibration_;
public:
    MacDeviceCalibration(IOPCIDevice *device,IOMemoryMap *map):ddvIo_(device,map),davIo_(device,map),
        ddv_(ddvIo_),dav_(davIo_),calibration_(ddv_,dav_){}
    MacDeviceCalibration(const MacDeviceCalibration&)=delete;MacDeviceCalibration&operator=(const MacDeviceCalibration&)=delete;
    bool valid()const{return ddvIo_.valid()&&davIo_.valid();}
    bool read(uint8_t verifiedCut){return valid()&&calibration_.read(verifiedCut);}
    void cancel(){ddvIo_.cancel();davIo_.cancel();}
    const CalibrationSnapshot *snapshot()const{return calibration_.snapshot();}
    const CalibrationReadResult &result()const{return calibration_.result();}
};
} }
