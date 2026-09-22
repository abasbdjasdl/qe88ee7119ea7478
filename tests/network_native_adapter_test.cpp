// SPDX-License-Identifier: GPL-2.0-or-later
// Compiled with the actual pinned Apple declarations; called by a host test runner.
#include "../src/network/NativeWirelessData.hpp"
#include "../src/network/NativeWirelessRequests.hpp"
#include "../src/network/NativeWirelessDispatch.hpp"
using namespace rtl8852be::network;
#define CHECK(x) do { if(!(x))return __LINE__; } while(0)
struct AdapterTestBackend {
    unsigned submitted{},disconnected{};
    unsigned snapshots{};
    IOReturn snapshotResult{kIOReturnSuccess};
    wireless::Snapshot observed;
    IOReturn copyWirelessStatus(wireless::Snapshot &s){++snapshots;s=observed;return snapshotResult;}
    IOReturn selectWirelessNetwork(const selection::Join &r){
        if(!selection::valid(r))return kIOReturnBadArgument;
        ++submitted;return kIOReturnBusy;
    }
    IOReturn disconnectWirelessNetwork(){++disconnected;return kIOReturnSuccess;}
};
extern "C" int R16NativeAdapterTest(){
    apple80211_assoc_data a{};selection::Join out;
    CHECK(nativewifi::decodeAssociation(nullptr,out)==kIOReturnBadArgument);
    a.version=APPLE80211_VERSION;a.ad_mode=APPLE80211_AP_MODE_INFRA;
    a.ad_auth_lower=APPLE80211_AUTHTYPE_OPEN;a.ad_ssid_len=32;a.ad_ssid[31]=255;
    CHECK(nativewifi::decodeAssociation(&a,out)==kIOReturnSuccess);
    CHECK(out.ssidLength==32&&out.ssid[0]==0&&out.ssid[31]==255&&!out.specificBssid);
    a.ad_auth_upper=APPLE80211_AUTHTYPE_WPA3_SAE;
    CHECK(nativewifi::decodeAssociation(&a,out)==kIOReturnUnsupported);
    CHECK(out.ssidLength==0&&out.pmkLength==0);
    a.ad_auth_upper=APPLE80211_AUTHTYPE_WPA2_PSK;a.ad_key.key_cipher_type=APPLE80211_CIPHER_PMK;
    a.ad_key.key_len=31;CHECK(nativewifi::decodeAssociation(&a,out)==kIOReturnUnsupported);
    a.ad_key.key_len=32;a.ad_key.key[31]=0xaa;
    CHECK(nativewifi::decodeAssociation(&a,out)==kIOReturnSuccess&&out.pmk[31]==0xaa);
    a.ad_bssid.octet[0]=1;
    CHECK(nativewifi::decodeAssociation(&a,out)==kIOReturnBadArgument&&out.pmk[31]==0);
    a.ad_bssid.octet[0]=2;AdapterTestBackend backend;
    CHECK(nativewifi::associate(backend,&a)==kIOReturnBusy&&backend.submitted==1);
    a.ad_auth_upper=APPLE80211_AUTHTYPE_WPA2;
    CHECK(nativewifi::associate(backend,&a)==kIOReturnUnsupported&&backend.submitted==1);
    CHECK(nativewifi::disassociate(backend)==kIOReturnSuccess&&backend.disconnected==1);
    a.ad_auth_upper=APPLE80211_AUTHTYPE_WPA_PSK;
    CHECK(nativewifi::decodeAssociation(&a,out)==kIOReturnSuccess&&out.security==selection::Security::wpaPsk&&
        out.pairwise==selection::Cipher::tkip&&out.group==selection::Cipher::tkip);
    a.ad_auth_upper=APPLE80211_AUTHTYPE_WPA3_SAE;
    CHECK(nativewifi::decodeAssociation(&a,out)==kIOReturnUnsupported&&out.pmkLength==0);
    wireless::Snapshot s;apple80211_ssid_data ssid;memset(&ssid,0xaa,sizeof(ssid));
    CHECK(nativewifi::ssid(s,&ssid)==kIOReturnNotReady&&ssid.ssid_len==0&&ssid.ssid_bytes[0]==0);
    s.currentValid=true;s.link=wireless::Link::connected;s.current.ssidLength=32;s.current.ssid[31]=255;
    CHECK(nativewifi::ssid(s,&ssid)==kIOReturnSuccess&&ssid.ssid_len==32&&ssid.ssid_bytes[31]==255);
    s.selectionPending=true;CHECK(nativewifi::ssid(s,&ssid)==kIOReturnNotReady&&ssid.ssid_len==0);
    s.selectionPending=false;s.current.signalPercent=67;apple80211_rssi_data rssi;
    CHECK(nativewifi::rssi(s,&rssi)==kIOReturnSuccess&&rssi.version==APPLE80211_VERSION&&
        rssi.rssi_unit==APPLE80211_UNIT_PERCENT&&rssi.aggregate_rssi==67);
    CHECK(nativewifi::ssid(s,nullptr)==kIOReturnBadArgument);
    backend.observed=s;wireless::Snapshot scratch;
    CHECK(nativewifi::getRequest(backend,scratch,APPLE80211_IOC_SSID,&ssid,sizeof(ssid)-1)==kIOReturnBadArgument);
    CHECK(backend.snapshots==0);
    CHECK(nativewifi::getRequest(backend,scratch,APPLE80211_IOC_SSID,&ssid,sizeof(ssid))==kIOReturnSuccess);
    CHECK(backend.snapshots==1&&ssid.ssid_bytes[31]==255);
    backend.snapshotResult=kIOReturnNotReady;
    CHECK(nativewifi::getRequest(backend,scratch,APPLE80211_IOC_SSID,&ssid,sizeof(ssid))==kIOReturnNotReady);
    CHECK(ssid.ssid_len==0&&ssid.ssid_bytes[31]==0);
    CHECK(nativewifi::getRequest(backend,scratch,0xffffffff,&ssid,sizeof(ssid))==kIOReturnUnsupported);
    CHECK(backend.snapshots==2);
    a.ad_auth_upper=APPLE80211_AUTHTYPE_WPA2_PSK;
    CHECK(nativewifi::setRequest(backend,APPLE80211_IOC_ASSOCIATE,&a,sizeof(a)-1)==kIOReturnBadArgument);
    CHECK(backend.submitted==1);
    CHECK(nativewifi::setRequest(backend,APPLE80211_IOC_ASSOCIATE,&a,sizeof(a))==kIOReturnBusy);
    CHECK(backend.submitted==2);
    CHECK(nativewifi::setRequest(backend,APPLE80211_IOC_DISASSOCIATE,nullptr,0)==kIOReturnSuccess);
    CHECK(backend.disconnected==2);
    selection::wipe(&a,sizeof(a));selection::wipe(&out,sizeof(out));return 0;
}
