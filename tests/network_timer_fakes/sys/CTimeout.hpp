// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/IOWorkLoop.h>
struct CTimeout:OSObject {
    static bool failAllocation;static int live;
    static void *operator new(size_t bytes)noexcept{return failAllocation?nullptr:std::malloc(bytes);}
    static void operator delete(void *p){std::free(p);}
    CTimeout(){++live;}~CTimeout(){--live;}
    void release(){delete this;}
    static void timeoutOccurred(OSObject *,IOTimerEventSource *);
    static IOReturn timeout_set(OSObject *,void *,void *,void *,void *);
    static IOReturn timeout_add_msec(OSObject *,void *,void *,void *,void *);
    static IOReturn timeout_del(OSObject *,void *,void *,void *,void *);
    static IOReturn timeout_free(OSObject *,void *,void *,void *,void *);
    static IOReturn timeout_pending(OSObject *,void *,void *,void *,void *);
    IOTimerEventSource *tm;
    void (*to_func)(void *);
    void *to_arg;
    bool isPending;
};
