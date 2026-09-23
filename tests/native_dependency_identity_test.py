#!/usr/bin/env python3
"""Exercise the actual VM dependency collector using loader-owned list fakes."""
from pathlib import Path
import argparse
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#define R16_NATIVE_ABI_AUDIT_ONLY 1
#include "tools/native_abi/native_runtime_identity.hpp"
#include "tools/native_abi/native_macho_uuid.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#define KMOD_MAX_NAME 64
#define KMOD_INFO_VERSION 1
struct kmod_reference_t;
struct kmod_info_t {int info_version=1;unsigned id=1;char name[64]{};kmod_reference_t *reference_list{};uintptr_t address{};size_t size{};};
struct kmod_reference_t {kmod_reference_t *next{};kmod_info_t *info{};};
kmod_info_t kmod_info;
int version_major=24,version_minor=4,version_revision=0;
char kernelText[37]="E6326809-88F4-3ECC-93BB-D2CBDF235588";
int sysctlError=0;size_t sysctlLength=37;unsigned calls=0;
int sysctlbyname(const char *name,void *out,size_t *length,void *write,size_t writeLength){
 ++calls;assert(!std::strcmp(name,"kern.uuid")&&!write&&!writeLength&&*length==37);
 std::memcpy(out,kernelText,37);*length=sysctlLength;return sysctlError;
}
void IOLog(const char *,...){}
'''
POST=r'''
int main(){
 using namespace r16_native_identity;
 kmod_info_t deps[3]{};kmod_reference_t refs[3]{};uint8_t images[3][80]{};
 auto put=[](uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));};
 auto reset=[&](){
  kmod_info={};sysctlError=0;sysctlLength=37;calls=0;
  std::strcpy(kernelText,"E6326809-88F4-3ECC-93BB-D2CBDF235588");
  for(unsigned i=0;i<3;++i){
   deps[i]={};refs[i]={};std::memset(images[i],0,80);
   std::strcpy(deps[i].name,components[i+1].identifier);
   deps[i].address=reinterpret_cast<uintptr_t>(images[i]);deps[i].size=56;
   refs[i].info=&deps[i];refs[i].next=i<2?&refs[i+1]:nullptr;
   put(images[i],0xfeedfacf);put(images[i]+4,0x01000007);put(images[i]+16,1);put(images[i]+20,24);
   put(images[i]+32,0x1b);put(images[i]+36,24);std::memcpy(images[i]+40,components[i+1].uuid,16);
  }
  kmod_info.reference_list=refs;
 };
 reset();assert(inspectLoadedComponents().status==Status::matched);
 for(unsigned part=0;part<3;++part){
  reset();int *v=part==0?&version_major:part==1?&version_minor:&version_revision;
  ++*v;assert(inspectLoadedComponents().status==Status::unsupportedKernel);--*v;assert(!calls);
 }
 reset();sysctlError=1;assert(inspectLoadedComponents().status==Status::noUUID);
 for(size_t length=0;length<40;++length){
  if(length==37)continue;reset();sysctlLength=length;assert(inspectLoadedComponents().status==Status::invalidUUID);
 }
 reset();kernelText[8]='z';assert(inspectLoadedComponents().status==Status::invalidUUID);
 reset();kernelText[0]='F';assert(inspectLoadedComponents().status==Status::mismatch);
 reset();kmod_info.id=UINT32_MAX;assert(inspectLoadedComponents().status==Status::notLoaded);
 reset();refs[2].next=refs;assert(inspectLoadedComponents().status==Status::invalidUUID);
 reset();refs[1].info=nullptr;assert(inspectLoadedComponents().status==Status::invalidUUID);
 reset();refs[1].info=&deps[0];assert(inspectLoadedComponents().status==Status::invalidUUID);
 for(unsigned i=0;i<3;++i){
  reset();deps[i].name[0]='x';auto r=inspectLoadedComponents();assert(r.status==Status::missing&&r.component==i+1);
  for(unsigned byte=0;byte<16;++byte){reset();images[i][40+byte]^=1;r=inspectLoadedComponents();assert(r.status==Status::mismatch&&r.component==i+1);}
  for(unsigned length=0;length<56;++length){reset();deps[i].size=length;assert(inspectLoadedComponents().status==Status::invalidUUID);}
  const unsigned offsets[]={0,4,16,20,32,36};
  for(auto offset:offsets){reset();put(images[i]+offset,UINT32_MAX);assert(inspectLoadedComponents().status==Status::invalidUUID);}
  reset();put(images[i]+16,2);put(images[i]+20,48);deps[i].size=80;
  std::memcpy(images[i]+56,images[i]+32,24);assert(inspectLoadedComponents().status==Status::invalidUUID);
 }
 reset();auto r=inspectLoadedComponents();assert(r.status==Status::matched&&r.component==componentCount);
 for(auto b:r.observed)assert(!b);
 puts("Dependency identity actual-body tests passed: versions, UUIDs, bounds, malformed headers, duplicate/cyclic dependencies");
}
'''
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
source=(ROOT/'tools/native_abi/native_runtime_identity.cpp').read_text()
body=source[source.index('namespace r16_native_identity {'):]
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(PRE+body+POST)
