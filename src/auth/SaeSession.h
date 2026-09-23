// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Userspace authentication component. Not a kernel crypto implementation.
// Initial scope: one group-19 exchange, H2E or hunting-and-pecking; no transport.
struct r16_sae;
struct r16_sae *r16_sae_create(const uint8_t own[6],const uint8_t peer[6],
    const uint8_t *ssid,size_t ssid_len,const uint8_t *password,size_t password_len,int h2e);
void r16_sae_destroy(struct r16_sae *s);
int r16_sae_commit(struct r16_sae *s,uint8_t *out,size_t capacity,size_t *written);
int r16_sae_receive_commit(struct r16_sae *s,const uint8_t *data,size_t length);
// Each call computes a fresh Confirm with an incremented send-confirm counter.
// After acceptance the response counter is 0xffff, as in hostap's SAE AP FSM.
int r16_sae_confirm(struct r16_sae *s,uint8_t *out,size_t capacity,size_t *written);
// First valid confirmation may carry 0xffff (accepted AP replying to a retry).
// After acceptance, duplicate/older/0xffff counters and invalid confirmations
// are rejected without revoking the established key or advancing peer counter;
// a newer valid Confirm is cryptographically checked before updating counter.
int r16_sae_receive_confirm(struct r16_sae *s,const uint8_t *data,size_t length);
// Only succeeds after peer confirmation. Failures clear both caller outputs.
int r16_sae_export(struct r16_sae *s,uint8_t pmk[32],uint8_t pmkid[16]);
#ifdef __cplusplus
}
#endif
