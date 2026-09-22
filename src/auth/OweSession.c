// SPDX-License-Identifier: BSD-3-Clause
// RFC 8110 section 4.4; crypto operations use pinned hostap/OpenSSL routines.
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "crypto/crypto.h"
#include "crypto/sha256.h"
#include "OweSession.h"
struct r16_owe {
    struct crypto_ecdh *dh;
    uint8_t public_x[32],pmk[32],pmkid[16];
    int ap,sent,derived,failed;
};
static int fail(struct r16_owe *s){
    if(s){s->failed=1;s->derived=0;forced_memzero(s->pmk,32);forced_memzero(s->pmkid,16);
        if(s->dh){crypto_ecdh_deinit(s->dh);s->dh=NULL;}}
    return -1;
}
void r16_owe_destroy(struct r16_owe *s){
    if(s){if(s->dh)crypto_ecdh_deinit(s->dh);bin_clear_free(s,sizeof(*s));}
}
struct r16_owe *r16_owe_create(int ap){
    if(ap!=0&&ap!=1)return NULL;
    struct r16_owe *s=os_zalloc(sizeof(*s));if(!s)return NULL;
    s->ap=ap;s->dh=crypto_ecdh_init(19);if(!s->dh)goto error;
    struct wpabuf *pub=wpabuf_zeropad(crypto_ecdh_get_pubkey(s->dh,0),32);
    if(!pub)goto error;
    if(wpabuf_len(pub)!=32){wpabuf_free(pub);goto error;}
    os_memcpy(s->public_x,wpabuf_head(pub),32);wpabuf_free(pub);return s;
error:r16_owe_destroy(s);return NULL;
}
int r16_owe_element(struct r16_owe *s,uint8_t *out,size_t capacity,size_t *written){
    if(written)*written=0;
    if(!s||s->failed||!out||!written||capacity<37)return -1;
    out[0]=255;out[1]=35;out[2]=32;out[3]=19;out[4]=0;
    os_memcpy(out+5,s->public_x,32);*written=37;s->sent=1;return 0;
}
int r16_owe_receive(struct r16_owe *s,const uint8_t *ie,size_t length){
    if(!s||s->failed||s->derived||!s->sent)return -1;
    if(!ie||length!=37||ie[0]!=255||ie[1]!=35||ie[2]!=32||WPA_GET_LE16(ie+3)!=19)
        return fail(s);
    if(!os_memcmp(s->public_x,ie+5,32))return fail(s);
    struct wpabuf *secret=wpabuf_zeropad(crypto_ecdh_set_peerkey(s->dh,0,ie+5,32),32);
    if(!secret)return fail(s);
    uint8_t salt[66],prk[32]={0},digest[32]={0};
    os_memcpy(salt,s->ap?ie+5:s->public_x,32);
    os_memcpy(salt+32,s->ap?s->public_x:ie+5,32);WPA_PUT_LE16(salt+64,19);
    const uint8_t *parts[]={salt};size_t lengths[]={64};
    const uint8_t info[]="OWE Key Generation";
    int error=wpabuf_len(secret)!=32||sha256_vector(1,parts,lengths,digest)||
        hmac_sha256(salt,sizeof(salt),wpabuf_head(secret),wpabuf_len(secret),prk)||
        hmac_sha256_kdf(prk,32,NULL,info,sizeof(info)-1,s->pmk,32);
    wpabuf_clear_free(secret);forced_memzero(prk,sizeof(prk));
    crypto_ecdh_deinit(s->dh);s->dh=NULL;
    if(!error){os_memcpy(s->pmkid,digest,16);s->derived=1;}
    forced_memzero(digest,sizeof(digest));
    return error?fail(s):0;
}
int r16_owe_export(struct r16_owe *s,uint8_t pmk[32],uint8_t pmkid[16]){
    if(pmk)forced_memzero(pmk,32);if(pmkid)forced_memzero(pmkid,16);
    if(!s||s->failed||!s->derived||!pmk||!pmkid)return -1;
    os_memcpy(pmk,s->pmk,32);os_memcpy(pmkid,s->pmkid,16);return 0;
}
