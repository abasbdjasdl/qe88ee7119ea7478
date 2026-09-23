// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "Target support options remain offline until startup/teardown is verified"
#endif
#include <stdint.h>
#include <stddef.h>

// These are factory argument records, NOT the private C++ object layouts.
// Unknown fields retain opaque names. No initialization policy, callback ABI,
// concrete-class cast, service lifecycle or runtime validity is implied.
struct CCPipeOptions {
    uint32_t pipeKind,logType,logDataType,logPolicy;
    uint64_t capacity,notificationAmount;
    uint32_t notificationThreshold;
    char fileName[0x100];
    char name[0x100];
    uint32_t unknown224,unknown228,policyFlags;
    void *callbackContext;
    void *callbackFunction; // Opaque: do not invoke through this declaration.
    uint32_t unknown240;
    char directoryName[0x100];
    uint8_t unknown344[8];
    uint32_t unknown34c;
};
struct CCStreamOptions {
    uint32_t streamKind,unknown004;
    int32_t logLevel,consoleLevel;
    uint8_t unknown010[8];
    uint64_t flags018,flags020;
    uint8_t unknown028[8];
    void *callbackContext;
    void *callback038;
    void *callback040;
    uint8_t unknown048[0x10];
    char name[0xf0];
    class OSData *optionalData;
    uint32_t unknown150;
    uint8_t unknown154[4];
    uint8_t copiedTail[0x200];
};

#define R16_SUPPORT_OFFSET(type, member, at) \
    static_assert(offsetof(type, member)==at, #type "::" #member " offset changed")
static_assert(sizeof(void*)==8 && sizeof(uint32_t)==4 && sizeof(uint64_t)==8, "scalar ABI changed");
static_assert(sizeof(CCPipeOptions)==0x350 && alignof(CCPipeOptions)==8, "full pipe options required");
static_assert(sizeof(CCStreamOptions)==0x358 && alignof(CCStreamOptions)==8, "full stream options required");
R16_SUPPORT_OFFSET(CCPipeOptions,logType,0x04);
R16_SUPPORT_OFFSET(CCPipeOptions,logDataType,0x08);
R16_SUPPORT_OFFSET(CCPipeOptions,logPolicy,0x0c);
R16_SUPPORT_OFFSET(CCPipeOptions,capacity,0x10);
R16_SUPPORT_OFFSET(CCPipeOptions,notificationAmount,0x18);
R16_SUPPORT_OFFSET(CCPipeOptions,notificationThreshold,0x20);
R16_SUPPORT_OFFSET(CCPipeOptions,fileName,0x24);
R16_SUPPORT_OFFSET(CCPipeOptions,name,0x124);
R16_SUPPORT_OFFSET(CCPipeOptions,unknown224,0x224);
R16_SUPPORT_OFFSET(CCPipeOptions,unknown228,0x228);
R16_SUPPORT_OFFSET(CCPipeOptions,policyFlags,0x22c);
R16_SUPPORT_OFFSET(CCPipeOptions,callbackContext,0x230);
R16_SUPPORT_OFFSET(CCPipeOptions,callbackFunction,0x238);
R16_SUPPORT_OFFSET(CCPipeOptions,directoryName,0x244);
R16_SUPPORT_OFFSET(CCPipeOptions,unknown34c,0x34c);
R16_SUPPORT_OFFSET(CCStreamOptions,logLevel,0x08);
R16_SUPPORT_OFFSET(CCStreamOptions,consoleLevel,0x0c);
R16_SUPPORT_OFFSET(CCStreamOptions,flags018,0x18);
R16_SUPPORT_OFFSET(CCStreamOptions,flags020,0x20);
R16_SUPPORT_OFFSET(CCStreamOptions,callbackContext,0x30);
R16_SUPPORT_OFFSET(CCStreamOptions,callback038,0x38);
R16_SUPPORT_OFFSET(CCStreamOptions,callback040,0x40);
R16_SUPPORT_OFFSET(CCStreamOptions,name,0x58);
R16_SUPPORT_OFFSET(CCStreamOptions,optionalData,0x148);
R16_SUPPORT_OFFSET(CCStreamOptions,unknown150,0x150);
R16_SUPPORT_OFFSET(CCStreamOptions,copiedTail,0x158);
#undef R16_SUPPORT_OFFSET
