// SPDX-License-Identifier: BSD-3-Clause
#include "../src/auth/SaeSession.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void exchange(int h2e,int wrong_password,int corrupt_confirm){
    fprintf(stderr,"SAE exchange h2e=%d wrong-password=%d tamper=%d\n",h2e,wrong_password,corrupt_confirm);
    const uint8_t a[6]={2,0,0,0,0,1},b[6]={2,0,0,0,0,2};
    const uint8_t ssid[]={'t',0,'s','t'},password[]="offline-test-password",wrong[]="wrong-test-password";
    struct r16_sae *x=r16_sae_create(a,b,ssid,sizeof(ssid),password,sizeof(password)-1,h2e);
    struct r16_sae *y=r16_sae_create(b,a,ssid,sizeof(ssid),wrong_password?wrong:password,
        wrong_password?sizeof(wrong)-1:sizeof(password)-1,h2e);
    assert(x&&y);uint8_t xc[2048],yc[2048],xf[128],yf[128],xp[32],yp[32],xi[16],yi[16];
    size_t xn,yn,xfn,yfn;memset(xp,0xaa,sizeof(xp));
    assert(r16_sae_export(x,xp,xi)!=0);for(unsigned i=0;i<32;++i)assert(xp[i]==0);
    assert(!r16_sae_commit(x,xc,sizeof(xc),&xn)&&!r16_sae_commit(y,yc,sizeof(yc),&yn));
    assert(!r16_sae_receive_commit(x,yc,yn)&&!r16_sae_receive_commit(y,xc,xn));
    assert(r16_sae_export(x,xp,xi)!=0);
    assert(!r16_sae_confirm(x,xf,sizeof(xf),&xfn)&&!r16_sae_confirm(y,yf,sizeof(yf),&yfn));
    if(corrupt_confirm)yf[yfn-1]^=1;
    int xr=r16_sae_receive_confirm(x,yf,yfn),yr=r16_sae_receive_confirm(y,xf,xfn);
    if(wrong_password){assert(xr&&yr);assert(r16_sae_export(x,xp,xi)&&r16_sae_export(y,yp,yi));}
    else if(corrupt_confirm){assert(xr);assert(r16_sae_export(x,xp,xi));}
    else{assert(!xr&&!yr);assert(!r16_sae_export(x,xp,xi)&&!r16_sae_export(y,yp,yi));
        assert(!memcmp(xp,yp,32)&&!memcmp(xi,yi,16));assert(r16_sae_receive_confirm(x,yf,yfn)!=0);}
    r16_sae_destroy(x);r16_sae_destroy(y);
}
static void reject_commit(int h2e,unsigned mode){
    const uint8_t a[6]={2,0,0,0,0,1},b[6]={2,0,0,0,0,2};
    const uint8_t ssid[]="test",password[]="offline-test-password";
    struct r16_sae *x=r16_sae_create(a,b,ssid,4,password,sizeof(password)-1,h2e);
    struct r16_sae *y=r16_sae_create(b,a,ssid,4,password,sizeof(password)-1,h2e);
    assert(x&&y);uint8_t xc[128],yc[128],pmk[32],id[16];size_t xn,yn;
    assert(!r16_sae_commit(x,xc,sizeof(xc),&xn)&&!r16_sae_commit(y,yc,sizeof(yc),&yn));
    assert(xn==98&&yn==98); // Ordinary SAE has no extended-key selector.
    if(mode==0)memcpy(yc,xc,xn);
    if(mode==1)yc[0]=20;
    if(mode==2)--yn;
    if(mode==3)yc[yn++]=0;
    if(mode==4)memset(yc+2,0,32); // Invalid scalar zero
    assert(r16_sae_receive_commit(x,yc,yn));
    memset(pmk,0xaa,sizeof(pmk));assert(r16_sae_export(x,pmk,id));
    for(unsigned i=0;i<32;++i)assert(pmk[i]==0);
    r16_sae_destroy(x);r16_sae_destroy(y);
}
int main(void){
    for(int h2e=0;h2e<2;++h2e){exchange(h2e,0,0);exchange(h2e,1,0);exchange(h2e,0,1);}
    for(int h2e=0;h2e<2;++h2e)for(unsigned mode=0;mode<5;++mode)reject_commit(h2e,mode);
    puts("SAE offline: H2E/HnP peer exchange, PMK gating, wrong password and tampering passed");return 0;
}
