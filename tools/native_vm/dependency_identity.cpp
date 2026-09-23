// SPDX-License-Identifier: GPL-2.0-or-later
// VM validation of a replacement for the unexported OSKext identity calls.
#ifndef R16_VM_LIFECYCLE_EXPERIMENT
#error "Not yet promoted to a physical-driver identity provider"
#endif
#include "native_runtime_identity.hpp"
#include "native_macho_uuid.hpp"
#include <libkern/version.h>
#include <libkern/sysctl.h>
#include <mach/kmod.h>
#include <IOKit/IOLib.h>

extern "C" kmod_info_t kmod_info; // This bundle's definition in VMModule.c.

namespace r16_native_identity {
static bool sameName(const char *fixed,const char *expected) {
    for(size_t i=0;i<KMOD_MAX_NAME;++i){
        if(fixed[i]!=expected[i])return false;
        if(!fixed[i])return true;
    }
    return false;
}
Result inspectLoadedComponents(){
    Result result{};
    if(version_major!=24||version_minor!=4||version_revision!=0)return result;
    char kernelUUID[37]{};size_t length=sizeof(kernelUUID);
    if(sysctlbyname("kern.uuid",kernelUUID,&length,nullptr,0)!=0){result.status=Status::noUUID;return result;}
    if(!readUUIDString(kernelUUID,length,result.observed)){result.status=Status::invalidUUID;return result;}
    for(size_t j=0;j<16;++j)if(result.observed[j]!=components[0].uuid[j]){
        result.status=Status::mismatch;return result;
    }
    // The loader owns this fixed dependency list for our loaded lifetime.
    // Never walk the mutable global kmod list, call OSKext private methods,
    // scan memory for a header, or use addresses supplied by user space.
    for(size_t i=1;i<componentCount;++i){
        result.component=i;for(auto &byte:result.observed)byte=0;
        if(kmod_info.info_version!=KMOD_INFO_VERSION||kmod_info.id==UINT32_MAX){
            result.status=Status::notLoaded;return result;
        }
        const kmod_info_t *found=nullptr;unsigned count=0;
        for(auto *ref=kmod_info.reference_list;ref;ref=ref->next){
            if(++count>128||!ref->info){result.status=Status::invalidUUID;return result;}
            if(sameName(ref->info->name,components[i].identifier)){
                if(found){result.status=Status::invalidUUID;return result;}
                found=ref->info;
            }
        }
        if(!found){result.status=Status::missing;return result;}
        if(found->info_version!=KMOD_INFO_VERSION||!found->address||found->size<32||
           found->size>UINTPTR_MAX-found->address){result.status=Status::invalidUUID;return result;}
        if(!readMachOUUID(reinterpret_cast<const uint8_t*>(found->address),found->size,result.observed)){
            result.status=Status::invalidUUID;return result;
        }
        for(size_t j=0;j<16;++j)if(result.observed[j]!=components[i].uuid[j]){
            result.status=Status::mismatch;return result;
        }
        IOLog("R16VM retained dependency UUID matched: %s\n",components[i].identifier);
    }
    result.component=componentCount;result.status=Status::matched;
    for(auto &byte:result.observed)byte=0;
    return result;
}
}
