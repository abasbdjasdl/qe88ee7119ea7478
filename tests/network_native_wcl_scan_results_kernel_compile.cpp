// SPDX-License-Identifier: GPL-2.0-or-later
// Compile-only, freestanding macOS target. This is not an IO80211 event sender.
#include "../src/network/NativeWclScanResults.hpp"
namespace ns=rtl8852be::network::nativescan;
namespace fg=rtl8852be::network::foregroundscan;
namespace ws=rtl8852be::network::nativewclscan;
namespace wb=rtl8852be::network::nativewclbeacon;
namespace wr=rtl8852be::network::nativewclresults;
extern "C" unsigned native_wcl_scan_results_compile(
    wr::Bridge *bridge,const ws::Request *request,const fg::Status *scan,
    const ns::Store *cache,void *scratch,size_t scratchCapacity,
    void *payload,size_t payloadCapacity,bool verified){
    if(!bridge||!request||!scan||!cache)return 0;
    const auto admission=bridge->begin(wb::TargetProfile::darwin24_4_0_d8b50fc2,
                                       verified,*request,*scan,*cache,scratch,scratchCapacity);
    if(admission!=wr::Status::ready)return unsigned(admission);
    wr::Frame frame{};
    const auto result=bridge->reserve(*scan,*cache,payload,payloadCapacity,frame);
    if(result!=wr::Status::frameReady)return unsigned(result);
    return unsigned(frame.event+frame.bytes);
}
