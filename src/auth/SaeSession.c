// SPDX-License-Identifier: BSD-3-Clause
#include "utils/includes.h"
#include "utils/common.h"
#include "common/defs.h"
#include "common/wpa_common.h"
#include "common/sae.h"
#include "SaeSession.h"
struct r16_sae { struct sae_data sae; int h2e,commit_sent,peer_commit,confirm_sent,accepted,failed; };
static int fail(struct r16_sae *s){
    if(s){sae_clear_data(&s->sae);s->failed=1;s->accepted=0;}return -1;
}
static int address(const uint8_t *a){
    unsigned any=0;if(!a||a[0]&1)return 0;for(unsigned i=0;i<6;++i)any|=a[i];return any!=0;
}
struct r16_sae *r16_sae_create(const uint8_t own[6],const uint8_t peer[6],
    const uint8_t *ssid,size_t ssid_len,const uint8_t *password,size_t password_len,int h2e){
    if(!address(own)||!address(peer)||!os_memcmp(own,peer,6)||!ssid||!ssid_len||ssid_len>32||
       !password||!password_len||password_len>63||(h2e!=0&&h2e!=1))return NULL;
    struct r16_sae *s=os_zalloc(sizeof(*s));if(!s)return NULL;
    s->h2e=h2e;
    if(sae_set_group(&s->sae,19))goto error;
    s->sae.akmp=WPA_KEY_MGMT_SAE;
    // The optional commit AKM selector is for SAE extended-key modes only.
    // Ordinary SAE commits omit it; setting it here rejects a valid peer's
    // absent selector (zero) before key derivation.
    if(h2e){
        const int groups[]={19,0};
        struct sae_pt *pt=sae_derive_pt(groups,ssid,ssid_len,password,password_len,NULL,0);
        if(!pt)goto error;
        int status=sae_prepare_commit_pt(&s->sae,pt,own,peer,NULL,NULL);
        sae_deinit_pt(pt);if(status)goto error;
    }else if(sae_prepare_commit(own,peer,password,password_len,&s->sae))goto error;
    return s;
error:r16_sae_destroy(s);return NULL;
}
void r16_sae_destroy(struct r16_sae *s){if(s){sae_clear_data(&s->sae);bin_clear_free(s,sizeof(*s));}}
static int output(struct r16_sae *s,uint8_t *out,size_t capacity,size_t *written,int confirm){
    if(written)*written=0;
    if(!s||s->failed||!out||!written||s->accepted)return -1;
    if(confirm?(!s->peer_commit||s->confirm_sent):s->commit_sent)return -1;
    struct wpabuf *b=wpabuf_alloc(confirm?SAE_CONFIRM_MAX_LEN:SAE_COMMIT_MAX_LEN);
    if(!b)return -1;
    int error=confirm?sae_write_confirm(&s->sae,b):sae_write_commit(&s->sae,b,NULL,NULL,0);
    if(error||wpabuf_len(b)>capacity){wpabuf_free(b);return fail(s);}
    *written=wpabuf_len(b);os_memcpy(out,wpabuf_head(b),*written);wpabuf_free(b);
    if(confirm){s->confirm_sent=1;s->sae.state=SAE_CONFIRMED;}
    else{s->commit_sent=1;s->sae.state=SAE_COMMITTED;}
    return 0;
}
int r16_sae_commit(struct r16_sae *s,uint8_t *out,size_t capacity,size_t *written){return output(s,out,capacity,written,0);}
int r16_sae_confirm(struct r16_sae *s,uint8_t *out,size_t capacity,size_t *written){return output(s,out,capacity,written,1);}
int r16_sae_receive_commit(struct r16_sae *s,const uint8_t *data,size_t length){
    if(!s||s->failed||!s->commit_sent||s->peer_commit||!data||length>SAE_COMMIT_MAX_LEN)return -1;
    // This component negotiates group 19 without token/password-id/rejected-
    // group/extended-key elements. Reject unhandled trailing data explicitly.
    if(length!=98||WPA_GET_LE16(data)!=19)return fail(s);
    int groups[]={19,0};const u8 *token=NULL;size_t token_len=0;
    if(sae_parse_commit(&s->sae,data,length,&token,&token_len,groups,s->h2e,NULL)||
       token_len||sae_process_commit(&s->sae))return fail(s);
    s->peer_commit=1;return 0;
}
int r16_sae_receive_confirm(struct r16_sae *s,const uint8_t *data,size_t length){
    if(!s||s->failed||!s->peer_commit||!s->confirm_sent||s->accepted||!data)return -1;
    // Group19/SHA256 initial exchange only; no extra unhandled confirmation IEs.
    if(length!=34||WPA_GET_LE16(data)==0||WPA_GET_LE16(data)==0xffff||
       sae_check_confirm(&s->sae,data,length,NULL)||s->sae.pmk_len!=32)return fail(s);
    s->accepted=1;s->sae.state=SAE_ACCEPTED;return 0;
}
int r16_sae_export(struct r16_sae *s,uint8_t pmk[32],uint8_t pmkid[16]){
    if(pmk)forced_memzero(pmk,32);if(pmkid)forced_memzero(pmkid,16);
    if(!s||!s->accepted||s->failed||!pmk||!pmkid)return -1;
    os_memcpy(pmk,s->sae.pmk,32);os_memcpy(pmkid,s->sae.pmkid,16);return 0;
}
