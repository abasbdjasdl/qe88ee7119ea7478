// SPDX-License-Identifier: GPL-2.0-or-later
// Compile-only factory calls for the fixed Darwin 24.4 native startup objects.
// These declarations deliberately provide no instance layout or virtual calls.
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "Support factory declarations are only for the offline ABI audit"
#endif
#ifndef __x86_64__
#error "This support-object evidence is pinned to x86_64"
#endif
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include "support_options.hpp"

class CCPipe {
public:
    static CCPipe *withOwnerNameCapacity(
        IOService *, const char *, const char *, const CCPipeOptions *);
};
class CCStream {
public:
    static CCStream *withPipeAndName(CCPipe *, const char *, const CCStreamOptions *);
};
class CCDataStream;
class CCFaultReporter {
public:
    static CCFaultReporter *withStreamWorkloop(CCDataStream *, IOWorkLoop *);
};
class IO80211FaultReporter {
public:
    static IO80211FaultReporter *allocWithParams(CCFaultReporter *);
};

using PipeFactory = CCPipe *(*)(IOService *, const char *, const char *, const CCPipeOptions *);
using StreamFactory = CCStream *(*)(CCPipe *, const char *, const CCStreamOptions *);
using ReporterFactory = CCFaultReporter *(*)(CCDataStream *, IOWorkLoop *);
using WrapperFactory = IO80211FaultReporter *(*)(CCFaultReporter *);
static_assert(__is_same(decltype(&CCPipe::withOwnerNameCapacity), PipeFactory), "pipe signature");
static_assert(__is_same(decltype(&CCStream::withPipeAndName), StreamFactory), "stream signature");
static_assert(__is_same(decltype(&CCFaultReporter::withStreamWorkloop), ReporterFactory), "reporter signature");
static_assert(__is_same(decltype(&IO80211FaultReporter::allocWithParams), WrapperFactory), "wrapper signature");

extern "C" CCPipe *r16_support_pipe(IOService *owner, const char *ownerName,
                                   const char *name, const CCPipeOptions *options) {
    return CCPipe::withOwnerNameCapacity(owner, ownerName, name, options);
}
extern "C" CCStream *r16_support_stream(CCPipe *pipe, const char *name,
                                       const CCStreamOptions *options) {
    return CCStream::withPipeAndName(pipe, name, options);
}
extern "C" CCFaultReporter *r16_support_reporter(CCDataStream *stream, IOWorkLoop *loop) {
    return CCFaultReporter::withStreamWorkloop(stream, loop);
}
extern "C" IO80211FaultReporter *r16_support_wrapper(CCFaultReporter *reporter) {
    return IO80211FaultReporter::allocWithParams(reporter);
}
