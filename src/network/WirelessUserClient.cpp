// SPDX-License-Identifier: GPL-2.0-or-later
#include "WirelessUserClient.hpp"
#include "WirelessControl.hpp"
#include "MacNetworkController.hpp"
#include <IOKit/IOLib.h>
#include <kern/task.h>
using namespace rtl8852be::network;
OSDefineMetaClassAndStructors(R16WirelessUserClient,IOUserClient)
bool R16WirelessUserClient::initWithTask(task_t task,void *token,UInt32 type,OSDictionary *properties){
    if(type!=control::connectionType||clientHasPrivilege(task,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)
        return false;
    if(!IOUserClient::initWithTask(task,token,type,properties))return false;
    lock_=IOLockAlloc();return lock_!=nullptr;
}
bool R16WirelessUserClient::start(IOService *provider){
    auto *owner=OSDynamicCast(R16NetworkController,provider);
    if(!owner||!IOUserClient::start(provider))return false;
    owner_=owner;owner_->retain();return true;
}
void R16WirelessUserClient::closeControl(){
    if(lock_){IOLockLock(lock_);closed_=true;
        if(owner_)owner_->authenticationEvents(this,control::captureEnd);
        IOLockUnlock(lock_);}
}
void R16WirelessUserClient::stop(IOService *provider){
    closeControl();
    IOUserClient::stop(provider);
}
IOReturn R16WirelessUserClient::clientClose(){
    closeControl();
    if(!isInactive())terminate();return kIOReturnSuccess;
}
void R16WirelessUserClient::free(){
    closeControl();
    if(owner_){owner_->release();owner_=nullptr;}
    if(lock_){IOLockFree(lock_);lock_=nullptr;}IOUserClient::free();
}
IOReturn R16WirelessUserClient::externalMethod(uint32_t selector,IOExternalMethodArguments *a,
    IOExternalMethodDispatch*,OSObject*,void*){
    // Re-check the invoking task: an inherited/transferred port is not authority.
    if(clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
    if(!a||!lock_)return kIOReturnBadArgument;
    if(a->scalarInputCount||a->scalarOutputCount||a->asyncReferenceCount||
       a->structureInputDescriptor||a->structureOutputDescriptor)return kIOReturnBadArgument;
    if(selector>control::captureRead)return kIOReturnUnsupported;
    if(selector==control::status||selector==control::captureRead){
        const size_t outputSize=selector==control::status?sizeof(control::Status):sizeof(authevents::Event);
        if(a->structureInputSize||!a->structureOutput||a->structureOutputSize!=outputSize)return kIOReturnBadArgument;
        bzero(a->structureOutput,outputSize);
    }else if(a->structureOutputSize||(selector==control::join?
        (!a->structureInput||a->structureInputSize!=sizeof(control::Join)):a->structureInputSize))return kIOReturnBadArgument;
    IOLockLock(lock_);
    IOReturn result=kIOReturnNotReady;
    if(!closed_&&owner_&&!isInactive()){
        if(selector==control::status){
            auto *snapshot=static_cast<wireless::Snapshot*>(IOMalloc(sizeof(wireless::Snapshot)));
            if(!snapshot)result=kIOReturnNoMemory;
            else{result=owner_->copyWirelessStatus(*snapshot);
                if(result==kIOReturnSuccess)control::encode(*snapshot,*static_cast<control::Status*>(a->structureOutput));
                IOFree(snapshot,sizeof(wireless::Snapshot));}
        }else if(selector==control::join){
            control::Join input;selection::Join request;
            memcpy(&input,a->structureInput,sizeof(input));
            result=control::decode(input,request)?owner_->selectWirelessNetwork(request):kIOReturnBadArgument;
            selection::wipe(&input,sizeof(input));selection::wipe(&request,sizeof(request));
        }else if(selector==control::disconnect)result=owner_->disconnectWirelessNetwork();
        else result=owner_->authenticationEvents(this,selector,
            selector==control::captureRead?static_cast<authevents::Event*>(a->structureOutput):nullptr);
    }
    IOLockUnlock(lock_);return result;
}
IOReturn R16NetworkController::newUserClient(task_t task,void *token,UInt32 type,
    OSDictionary *properties,IOUserClient **handler){
    if(!handler)return kIOReturnBadArgument;*handler=nullptr;
    if(type!=control::connectionType)return IOEthernetController::newUserClient(task,token,type,properties,handler);
    if(IOUserClient::clientHasPrivilege(task,kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
    if(isInactive())return kIOReturnNotReady;
    auto *client=new R16WirelessUserClient;if(!client)return kIOReturnNoMemory;
    if(!client->initWithTask(task,token,type,properties)){client->release();return kIOReturnError;}
    if(!client->attach(this)){client->release();return kIOReturnError;}
    if(!client->start(this)){client->detach(this);client->release();return kIOReturnError;}
    *handler=client;return kIOReturnSuccess;
}
