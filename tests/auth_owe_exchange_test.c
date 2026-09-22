// SPDX-License-Identifier: BSD-3-Clause
#include "../src/auth/OweSession.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
int main(void){
    for(unsigned mode=0;mode<6;++mode){
        struct r16_owe *c=r16_owe_create(0),*a=r16_owe_create(1);assert(c&&a);
        uint8_t ce[37],ae[37],cp[32],ap[32],ci[16],ai[16];size_t cn,an;
        memset(cp,0xaa,32);assert(r16_owe_export(c,cp,ci));
        for(unsigned i=0;i<32;++i)assert(cp[i]==0);
        assert(!r16_owe_element(c,ce,37,&cn)&&!r16_owe_element(a,ae,37,&an));
        assert(cn==37&&an==37);
        if(mode==1)ae[3]=20; // Unnegotiated group
        if(mode==2)an=36; // Truncated point
        if(mode==3)memset(ae+5,255,32); // X outside field
        if(mode==4)memcpy(ae,ce,37); // Reflected point
        if(mode==5)ae[2]=31; // Wrong extension
        int cr=r16_owe_receive(c,ae,an);
        if(mode){assert(cr);assert(r16_owe_export(c,cp,ci));
            for(unsigned i=0;i<32;++i)assert(cp[i]==0);}
        else{
            assert(!cr&&!r16_owe_receive(a,ce,cn));
            assert(!r16_owe_export(c,cp,ci)&&!r16_owe_export(a,ap,ai));
            assert(!memcmp(cp,ap,32)&&!memcmp(ci,ai,16));
            uint8_t public_pair[64],digest[32];
            memcpy(public_pair,ce+5,32);memcpy(public_pair+32,ae+5,32);
            SHA256(public_pair,64,digest);assert(!memcmp(ci,digest,16));
            assert(r16_owe_receive(c,ae,an));
        }
        r16_owe_destroy(c);r16_owe_destroy(a);
    }
    puts("OWE offline: group19 agreement, PMKID, malformed/group/point/reflection rejection passed");
    return 0;
}
