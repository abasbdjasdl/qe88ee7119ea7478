// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_runtime_identity.hpp"
#include <libkern/c++/OSObject.h>
#include <libkern/c++/OSData.h>
#include <libkern/version.h>

// The pinned SDK OSKext header requires absent kernel-private headers. This
// callsite-only declaration uses the observed OSObject base and direct methods;
// never construct, size, subclass, or access fields of OSKext through it.
class OSKext : public OSObject {
public:
    static OSKext *lookupKextWithIdentifier(const char *);
    bool isLoaded(); // direct call only, actual implementation returns bool in AL
    OSData *copyTextUUID();
};

extern "C" {
extern void (OSObject::*const r16_identity_release_slot)()const=&OSObject::release;
extern unsigned (OSData::*const r16_identity_length_slot)()const=&OSData::getLength;
extern const void *(OSData::*const r16_identity_bytes_slot)()const=&OSData::getBytesNoCopy;
}

namespace r16_native_identity {
Result inspectLoadedComponents(){
    Result result{};
    if(version_major!=24||version_minor!=4||version_revision!=0)return result;
    for(size_t i=0;i<componentCount;++i){
        result.component=i;
        for(auto &byte:result.observed)byte=0;
        OSKext *kext=OSKext::lookupKextWithIdentifier(components[i].identifier);
        if(!kext){result.status=Status::missing;return result;}
        // Explicit qualification avoids calling OSKext virtual slots through
        // an unverified SDK vtable. The exact symbols are separately audited.
        if(!kext->OSKext::isLoaded()){
            kext->release();result.status=Status::notLoaded;return result;
        }
        // copyUUID can return the aggregate kernel UUID for built-in kexts.
        // copyTextUUID reads that component's own loaded Mach-O LC_UUID.
        OSData *uuid=kext->copyTextUUID();
        kext->release();
        if(!uuid){result.status=Status::noUUID;return result;}
        const auto length=uuid->getLength();
        const auto *bytes=length==16?static_cast<const uint8_t*>(uuid->getBytesNoCopy()):nullptr;
        if(!bytes){uuid->release();result.status=Status::invalidUUID;return result;}
        bool equal=true;
        for(size_t j=0;j<16;++j){result.observed[j]=bytes[j];equal=equal&&(bytes[j]==components[i].uuid[j]);}
        uuid->release();
        if(!equal){result.status=Status::mismatch;return result;}
    }
    result.component=componentCount;result.status=Status::matched;
    for(auto &byte:result.observed)byte=0;
    return result;
}
}
