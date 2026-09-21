// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/Net80211Timers.cpp"
#include <cassert>
#include <cstdio>
bool IOTimerEventSource::failAllocation=false;int IOTimerEventSource::armResult=0,IOTimerEventSource::live=0;
bool CTimeout::failAllocation=false;int CTimeout::live=0;
static void callback(void *count){++*static_cast<int *>(count);}
int main(){
    CTimeout *timer=nullptr;IOWorkLoop loop;int calls=0,milliseconds=10;
    auto set=[&](){return CTimeout::timeout_set(nullptr,&timer,reinterpret_cast<void *>(&callback),&calls,nullptr);};
    auto arm=[&](){return CTimeout::timeout_add_msec(nullptr,&timer,&loop,&milliseconds,nullptr);};
    auto free=[&](){return CTimeout::timeout_free(nullptr,&timer,&loop,nullptr,nullptr);};
    CTimeout::failAllocation=true;assert(set()==kIOReturnNoMemory&&!timer);CTimeout::failAllocation=false;
    assert(set()==0&&timer&&!timer->tm&&!timer->isPending);
    IOTimerEventSource::failAllocation=true;assert(arm()==kIOReturnNoMemory&&!timer->tm);IOTimerEventSource::failAllocation=false;
    loop.addResult=42;assert(arm()==42&&!timer->tm&&IOTimerEventSource::live==0);loop.addResult=0;
    IOTimerEventSource::armResult=43;assert(arm()==43&&!timer->isPending&&loop.sources==1);IOTimerEventSource::armResult=0;
    assert(arm()==0&&timer->isPending&&timer->tm->armed);
    CTimeout::timeoutOccurred(timer,timer->tm);assert(calls==1&&!timer->isPending);
    CTimeout::timeoutOccurred(timer,timer->tm);assert(calls==1); // stale callback
    assert(arm()==0);assert(CTimeout::timeout_del(nullptr,&timer,nullptr,nullptr,nullptr)==0);
    CTimeout::timeoutOccurred(timer,timer->tm);assert(calls==1&&!timer->isPending);
    assert(arm()==0);assert(set()==0&&!timer->isPending&&!timer->tm->armed);
    milliseconds=-1;assert(arm()==kIOReturnBadArgument&&!timer->isPending);milliseconds=10;
    IOWorkLoop wrongLoop;
    assert(CTimeout::timeout_free(nullptr,&timer,&wrongLoop,nullptr,nullptr)==kIOReturnBadArgument&&timer);
    assert(arm()==0);assert(free()==0&&!timer&&!loop.sources&&!CTimeout::live&&!IOTimerEventSource::live);
    assert(free()==0);
    puts("PASS: timer allocation/add/arm failures, stale callbacks, cancel/reset, wrong-owner and pending-timer teardown (IOKit model)");
}
