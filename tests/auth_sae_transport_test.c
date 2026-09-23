// SPDX-License-Identifier: BSD-3-Clause
#include "../src/auth/SaeExchange.h"
#include "../src/auth/SaeSession.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t station[6] = {2,0,0,0,0,1}, ap[6] = {2,0,0,0,0,2};
static const uint8_t stranger[6] = {2,0,0,0,0,3};
static const uint8_t ssid[] = {'t',0,'e','s','t'};
static const uint8_t password[] = "offline-transport-password";
static const uint8_t wrong_password[] = "wrong-offline-password";
struct pair {
    struct r16_sae_exchange *sta;
    struct r16_sae *peer;
    struct r16_sae_auth_body sta_commit, ap_commit, sta_confirm, ap_confirm;
};
static struct r16_sae_exchange *create(int h2e, uint32_t timeout, unsigned retries) {
    struct r16_sae_exchange *x = r16_sae_exchange_create(station, ap, ssid, sizeof(ssid),
        password, sizeof(password)-1, h2e, 100, timeout, retries);
    assert(x);
    return x;
}
static void no_key(struct r16_sae_exchange *x) {
    uint8_t key[32], id[16];
    memset(key, 0xaa, sizeof(key)); memset(id, 0xaa, sizeof(id));
    assert(r16_sae_exchange_export(x, key, id));
    for (size_t i=0;i<sizeof(key);++i) assert(key[i]==0);
    for (size_t i=0;i<sizeof(id);++i) assert(id[i]==0);
}
static void header(struct r16_sae_auth_body *body, unsigned seq, unsigned status) {
    body->bytes[0]=3; body->bytes[1]=0;
    body->bytes[2]=(uint8_t)seq; body->bytes[3]=0;
    body->bytes[4]=(uint8_t)status; body->bytes[5]=0;
}
static int receive(struct pair *p, const struct r16_sae_auth_body *body,
                   uint64_t now, struct r16_sae_auth_body *out) {
    return r16_sae_exchange_receive(p->sta, ap, station, body->bytes, body->length, now, out);
}
static struct pair begin(int h2e, int wrong) {
    struct pair p = {0};
    p.sta=create(h2e, 1000, 3);
    p.peer=r16_sae_create(ap, station, ssid, sizeof(ssid),
        wrong ? wrong_password : password,
        wrong ? sizeof(wrong_password)-1 : sizeof(password)-1, h2e);
    assert(p.peer);
    no_key(p.sta);
    assert(r16_sae_exchange_start(p.sta, 0, &p.sta_commit)==R16_SAE_EXCHANGE_SEND);
    assert(p.sta_commit.length==104 && p.sta_commit.bytes[0]==3 &&
        p.sta_commit.bytes[1]==0 && p.sta_commit.bytes[2]==1 &&
        p.sta_commit.bytes[3]==0 && p.sta_commit.bytes[4]==(h2e ? 126 : 0) &&
        p.sta_commit.bytes[5]==0);
    header(&p.ap_commit, 1, h2e ? 126 : 0);
    size_t n=0;
    assert(!r16_sae_commit(p.peer, p.ap_commit.bytes+6, 98, &n) && n==98);
    p.ap_commit.length=n+6;
    return p;
}
static void peer_commit(struct pair *p, const struct r16_sae_auth_body *body) {
    assert(body->length==104);
    assert(!r16_sae_receive_commit(p->peer, body->bytes+6, body->length-6));
}
static void peer_confirm(struct pair *p, const struct r16_sae_auth_body *body) {
    assert(body->length==40 && body->bytes[2]==2 && body->bytes[4]==0);
    header(&p->ap_confirm, 2, 0);
    size_t n=0;
    assert(!r16_sae_confirm(p->peer, p->ap_confirm.bytes+6, 98, &n) && n==34);
    p->ap_confirm.length=n+6;
    assert(!r16_sae_receive_confirm(p->peer, body->bytes+6, body->length-6));
}
static void finish(struct pair *p) {
    r16_sae_exchange_destroy(p->sta);
    r16_sae_destroy(p->peer);
}
static void same_key(struct pair *p) {
    uint8_t sk[32], ak[32], si[16], ai[16];
    assert(!r16_sae_exchange_export(p->sta, sk, si));
    assert(!r16_sae_export(p->peer, ak, ai));
    assert(!memcmp(sk, ak, sizeof(sk)) && !memcmp(si, ai, sizeof(si)));
}
static void normal(int h2e) {
    struct pair p=begin(h2e, 0);
    struct r16_sae_auth_body out;
    peer_commit(&p, &p.sta_commit);
    assert(receive(&p, &p.ap_commit, 1, &p.sta_confirm)==R16_SAE_EXCHANGE_SEND);
    assert(r16_sae_exchange_state(p.sta)==R16_SAE_WAIT_CONFIRM);
    no_key(p.sta);
    peer_confirm(&p, &p.sta_confirm);
    assert(receive(&p, &p.ap_confirm, 2, &out)==R16_SAE_EXCHANGE_ACCEPTED);
    assert(out.length==0 && r16_sae_exchange_state(p.sta)==R16_SAE_ACCEPTED);
    same_key(&p);
    assert(receive(&p, &p.ap_confirm, 3, &out)==R16_SAE_EXCHANGE_IDLE);
    assert(receive(&p, &p.ap_commit, 4, &out)==R16_SAE_EXCHANGE_IDLE);
    assert(r16_sae_exchange_poll(p.sta, 10000, &out)==R16_SAE_EXCHANGE_IDLE);
    same_key(&p);
    r16_sae_exchange_cancel(p.sta);
    assert(r16_sae_exchange_state(p.sta)==R16_SAE_CANCELLED);
    no_key(p.sta);
    assert(r16_sae_exchange_poll(p.sta, 10001, &out)==R16_SAE_EXCHANGE_ERROR && !out.length);
    finish(&p);
}
static void losses(int h2e) {
    struct pair p=begin(h2e, 0);
    struct r16_sae_auth_body out;
    /* First STA commit lost before reaching the AP. */
    assert(r16_sae_exchange_poll(p.sta, 99, &out)==R16_SAE_EXCHANGE_IDLE && !out.length);
    assert(r16_sae_exchange_poll(p.sta, 100, &out)==R16_SAE_EXCHANGE_SEND);
    assert(out.length==p.sta_commit.length && !memcmp(out.bytes,p.sta_commit.bytes,out.length));
    peer_commit(&p, &out);
    /* First AP commit lost; identical management response is delivered later. */
    assert(r16_sae_exchange_poll(p.sta, 200, &out)==R16_SAE_EXCHANGE_SEND);
    assert(!memcmp(out.bytes,p.sta_commit.bytes,out.length));
    assert(receive(&p, &p.ap_commit, 201, &p.sta_confirm)==R16_SAE_EXCHANGE_SEND);
    assert(receive(&p, &p.ap_commit, 290, &out)==R16_SAE_EXCHANGE_IDLE && !out.length);
    /* Duplicate commit must not postpone the Confirm retry. */
    assert(r16_sae_exchange_poll(p.sta, 301, &out)==R16_SAE_EXCHANGE_SEND);
    assert(out.length==p.sta_confirm.length && out.bytes[6]==2 && out.bytes[7]==0);
    assert(memcmp(out.bytes,p.sta_confirm.bytes,out.length));
    /* Original STA confirm was lost, so AP accepts the freshly signed retry. */
    peer_confirm(&p, &out);
    assert(receive(&p, &p.ap_confirm, 302, &out)==R16_SAE_EXCHANGE_ACCEPTED);
    same_key(&p);
    finish(&p);
}
static void accepted_ap_response_lost(int h2e) {
    struct pair p=begin(h2e,0);
    struct r16_sae_auth_body retry,out;
    peer_commit(&p,&p.sta_commit);
    assert(receive(&p,&p.ap_commit,1,&p.sta_confirm)==R16_SAE_EXCHANGE_SEND);
    peer_confirm(&p,&p.sta_confirm); /* AP accepted, response never reaches STA. */
    no_key(p.sta);
    uint8_t key[32],id[16];
    assert(r16_sae_receive_confirm(p.peer,p.sta_confirm.bytes+6,34)); /* replay */
    assert(!r16_sae_export(p.peer,key,id)); /* replay cannot revoke its key */
    uint8_t invalid[35]={0},after_key[32],after_id[16];
    const size_t invalid_lengths[]={34,34,0,1,33,35};
    for(unsigned i=0;i<sizeof(invalid_lengths)/sizeof(invalid_lengths[0]);++i){
        memcpy(invalid,p.sta_confirm.bytes+6,34);
        /* A forged larger counter must neither revoke the PMK nor advance rc.
         * The correctly signed counter 2 below must still be accepted. */
        invalid[0]=i==1 ? 0 : 2;invalid[1]=0;
        assert(r16_sae_receive_confirm(p.peer,invalid,invalid_lengths[i]));
        assert(!r16_sae_export(p.peer,after_key,after_id));
        assert(!memcmp(key,after_key,sizeof(key)) && !memcmp(id,after_id,sizeof(id)));
    }
    assert(r16_sae_exchange_poll(p.sta,101,&retry)==R16_SAE_EXCHANGE_SEND);
    assert(retry.bytes[6]==2 && retry.bytes[7]==0);
    assert(!r16_sae_receive_confirm(p.peer,retry.bytes+6,34));
    size_t n=0;
    assert(!r16_sae_confirm(p.peer,p.ap_confirm.bytes+6,98,&n) && n==34);
    assert(p.ap_confirm.bytes[6]==0xff && p.ap_confirm.bytes[7]==0xff);
    assert(receive(&p,&p.ap_confirm,102,&out)==R16_SAE_EXCHANGE_ACCEPTED);
    same_key(&p);
    assert(receive(&p,&p.ap_confirm,103,&out)==R16_SAE_EXCHANGE_IDLE);
    assert(r16_sae_receive_confirm(p.peer,retry.bytes+6,34)); /* duplicate counter */
    same_key(&p);
    finish(&p);
}
static void ignore_foreign(int h2e) {
    struct pair p=begin(h2e, 0);
    struct r16_sae_auth_body out;
    uint8_t malformed[1]={0};
    assert(r16_sae_exchange_receive(p.sta,stranger,station,malformed,1,1,&out)==R16_SAE_EXCHANGE_IDLE);
    assert(r16_sae_exchange_receive(p.sta,ap,stranger,malformed,1,2,&out)==R16_SAE_EXCHANGE_IDLE);
    assert(r16_sae_exchange_state(p.sta)==R16_SAE_WAIT_COMMIT);
    peer_commit(&p, &p.sta_commit);
    /* The API intentionally permits body storage to alias its output. */
    out=p.ap_commit;
    assert(r16_sae_exchange_receive(p.sta,ap,station,out.bytes,out.length,3,&out)==R16_SAE_EXCHANGE_SEND);
    assert(out.length==40);
    no_key(p.sta);
    finish(&p);
}
static void malformed(int h2e, unsigned mode) {
    struct pair p=begin(h2e, 0);
    struct r16_sae_auth_body body=p.ap_commit, out;
    enum r16_sae_exchange_error error=R16_SAE_ERROR_PROTOCOL;
    switch(mode) {
    case 0: body.bytes[0]=0; break;                     /* Open-system auth */
    case 1: body.bytes[1]=3; break;                     /* Endianness */
    case 2: body.bytes[2]=3; error=R16_SAE_ERROR_SEQUENCE; break;
    case 3: body.bytes[4]=h2e ? 0 : 126; break;          /* No H2E downgrade */
    case 4: body.bytes[4]=76; break;                    /* Token unsupported */
    case 5: --body.length; break;
    case 6: body.length=5; break;
    case 7: body.bytes[2]=2; body.bytes[4]=0; body.length=40;
            error=R16_SAE_ERROR_SEQUENCE; break;       /* Confirm before commit */
    case 8: memcpy(body.bytes,p.sta_commit.bytes,body.length);
            error=R16_SAE_ERROR_CRYPTO; break;         /* Reflection */
    default: assert(0);
    }
    assert(receive(&p,&body,1,&out)==R16_SAE_EXCHANGE_ERROR && !out.length);
    assert(r16_sae_exchange_state(p.sta)==R16_SAE_FAILED);
    assert(r16_sae_exchange_error(p.sta)==error);
    no_key(p.sta);
    assert(receive(&p,&p.ap_commit,2,&out)==R16_SAE_EXCHANGE_ERROR);
    finish(&p);
}
static void bad_confirm(int h2e, unsigned mode) {
    struct pair p=begin(h2e, mode==0);
    struct r16_sae_auth_body out;
    peer_commit(&p,&p.sta_commit);
    assert(receive(&p,&p.ap_commit,1,&p.sta_confirm)==R16_SAE_EXCHANGE_SEND);
    header(&p.ap_confirm,2,0);
    size_t n=0;
    assert(!r16_sae_confirm(p.peer,p.ap_confirm.bytes+6,98,&n));
    p.ap_confirm.length=n+6;
    if(mode==1)p.ap_confirm.bytes[p.ap_confirm.length-1]^=1;
    if(mode==2)p.ap_confirm.bytes[4]=126;
    if(mode==3)--p.ap_confirm.length;
    if(mode==4)p.ap_confirm.bytes[6]=p.ap_confirm.bytes[7]=0xff; /* Forged accepted counter */
    if(mode==5)p.ap_confirm.bytes[6]=p.ap_confirm.bytes[7]=0;
    assert(receive(&p,&p.ap_confirm,2,&out)==R16_SAE_EXCHANGE_ERROR);
    assert(r16_sae_exchange_error(p.sta)==(mode==2||mode==3 ? R16_SAE_ERROR_PROTOCOL : R16_SAE_ERROR_CRYPTO));
    no_key(p.sta);
    finish(&p);
}
static void clocks_and_cancel(int h2e) {
    struct r16_sae_auth_body out;
    struct r16_sae_exchange *x=create(h2e,1000,2);
    assert(r16_sae_exchange_start(x,1000,&out)==R16_SAE_EXCHANGE_SEND);
    assert(r16_sae_exchange_poll(x,999,&out)==R16_SAE_EXCHANGE_ERROR);
    assert(r16_sae_exchange_error(x)==R16_SAE_ERROR_CLOCK); no_key(x);
    r16_sae_exchange_destroy(x);
    x=create(h2e,1000,2);
    assert(r16_sae_exchange_start(x,UINT64_MAX-10,&out)==R16_SAE_EXCHANGE_ERROR);
    assert(r16_sae_exchange_error(x)==R16_SAE_ERROR_CLOCK); no_key(x);
    r16_sae_exchange_destroy(x);
    x=create(h2e,1000,2);
    assert(r16_sae_exchange_start(x,0,&out)==R16_SAE_EXCHANGE_SEND);
    assert(r16_sae_exchange_poll(x,100,&out)==R16_SAE_EXCHANGE_SEND);
    assert(r16_sae_exchange_poll(x,200,&out)==R16_SAE_EXCHANGE_SEND);
    assert(r16_sae_exchange_poll(x,300,&out)==R16_SAE_EXCHANGE_ERROR);
    assert(r16_sae_exchange_error(x)==R16_SAE_ERROR_RETRIES); no_key(x);
    r16_sae_exchange_destroy(x);
    x=create(h2e,1000,16);
    assert(r16_sae_exchange_start(x,0,&out)==R16_SAE_EXCHANGE_SEND);
    assert(r16_sae_exchange_poll(x,1000,&out)==R16_SAE_EXCHANGE_ERROR);
    assert(r16_sae_exchange_error(x)==R16_SAE_ERROR_TIMEOUT); no_key(x);
    r16_sae_exchange_destroy(x);
    x=create(h2e,1000,0);
    assert(r16_sae_exchange_start(x,0,&out)==R16_SAE_EXCHANGE_SEND);
    assert(r16_sae_exchange_poll(x,100,&out)==R16_SAE_EXCHANGE_ERROR);
    assert(r16_sae_exchange_error(x)==R16_SAE_ERROR_RETRIES); no_key(x);
    r16_sae_exchange_destroy(x);
    for(unsigned started=0;started<2;++started){
        x=create(h2e,1000,2);
        if(started)assert(r16_sae_exchange_start(x,0,&out)==R16_SAE_EXCHANGE_SEND);
        r16_sae_exchange_cancel(x); r16_sae_exchange_cancel(x);
        assert(r16_sae_exchange_state(x)==R16_SAE_CANCELLED);
        assert(r16_sae_exchange_start(x,0,&out)==R16_SAE_EXCHANGE_ERROR);
        no_key(x); r16_sae_exchange_destroy(x);
    }
    /* Commit duplicates while waiting for Confirm cannot extend deadline. */
    struct pair p=begin(h2e,0);
    peer_commit(&p,&p.sta_commit);
    assert(receive(&p,&p.ap_commit,900,&p.sta_confirm)==R16_SAE_EXCHANGE_SEND);
    assert(receive(&p,&p.ap_commit,999,&out)==R16_SAE_EXCHANGE_IDLE);
    assert(receive(&p,&p.ap_commit,1000,&out)==R16_SAE_EXCHANGE_ERROR);
    assert(r16_sae_exchange_error(p.sta)==R16_SAE_ERROR_TIMEOUT); no_key(p.sta);
    finish(&p);
}
int main(void) {
    for(int h2e=0;h2e<2;++h2e){
        normal(h2e); losses(h2e); accepted_ap_response_lost(h2e);
        ignore_foreign(h2e); clocks_and_cancel(h2e);
        for(unsigned mode=0;mode<9;++mode)malformed(h2e,mode);
        for(unsigned mode=0;mode<6;++mode)bad_confirm(h2e,mode);
    }
    puts("SAE body transport: real HnP/H2E peers, wire framing, bounded retries, duplicates, ordering, timeout, cancellation and PMK gating passed");
    return 0;
}
