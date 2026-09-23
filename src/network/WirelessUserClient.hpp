// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLocks.h>
class R16NetworkController;
class R16WirelessUserClient : public IOUserClient {
    OSDeclareDefaultStructors(R16WirelessUserClient)
    R16NetworkController *owner_{};
    IOLock *lock_{};
    bool closed_{};
    void closeControl();
public:
    bool initWithTask(task_t,void*,UInt32,OSDictionary*) override;
    bool start(IOService*) override;
    void stop(IOService*) override;
    void free() override;
    IOReturn clientClose() override;
    IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*) override;
};
