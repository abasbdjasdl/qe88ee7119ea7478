// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
namespace rtl8852be { namespace network {
// Upstream itlwm's timeout and if_input helpers use global workloop/gate
// pointers. Enforce a single owner instead of silently replacing a live host.
class Net80211Runtime {
    IOWorkLoop *workloop_{};
    IOCommandGate *gate_{};
public:
    Net80211Runtime()=default;
    Net80211Runtime(const Net80211Runtime &)=delete;
    Net80211Runtime &operator=(const Net80211Runtime &)=delete;
    IOReturn bind(IOWorkLoop *workloop,IOCommandGate *gate);
    // Call under this gate only after ifdetach/node cleanup, timeout_free for
    // every protocol timer and task queue drain. Caller retains its own gate
    // and workloop references until this action has returned.
    IOReturn unbindAfterProtocolDetached();
    bool inGate()const{return workloop_&&workloop_->inGate();}
};
} }
