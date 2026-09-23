// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Userspace infrastructure-STA SAE authentication-body transport. The caller
 * supplies/validates the 802.11 management header and serializes all calls.
 * Body: little-endian algorithm (3), transaction (1/2), status, SAE payload.
 * Scope: group 19, HnP/H2E, no token, password ID, rejected groups, SAE-PK,
 * extended-key SAE or reauthentication. This is not association/4-way/PMF.
 * Commit retries reuse the cached scalar/element. Confirm retries increment
 * send-confirm and recompute its authenticator via hostap. Each SEND is a new
 * protocol transmission; the management layer assigns sequence control and
 * handles any byte-identical 802.11 MAC retries separately.
 */
#define R16_SAE_AUTH_BODY_MAX 104
struct r16_sae_auth_body {
    size_t length;
    uint8_t bytes[R16_SAE_AUTH_BODY_MAX];
};
enum r16_sae_exchange_state {
    R16_SAE_READY, R16_SAE_WAIT_COMMIT, R16_SAE_WAIT_CONFIRM,
    R16_SAE_ACCEPTED, R16_SAE_FAILED, R16_SAE_CANCELLED
};
enum r16_sae_exchange_error {
    R16_SAE_ERROR_NONE, R16_SAE_ERROR_ARGUMENT, R16_SAE_ERROR_SEQUENCE,
    R16_SAE_ERROR_PROTOCOL, R16_SAE_ERROR_CRYPTO, R16_SAE_ERROR_TIMEOUT,
    R16_SAE_ERROR_RETRIES, R16_SAE_ERROR_CLOCK, R16_SAE_ERROR_CANCELLED
};
enum r16_sae_exchange_result {
    R16_SAE_EXCHANGE_ERROR = -1, R16_SAE_EXCHANGE_IDLE = 0,
    R16_SAE_EXCHANGE_SEND = 1, R16_SAE_EXCHANGE_ACCEPTED = 2
};
struct r16_sae_exchange;
/* retry_ms: 1..10000, timeout_ms: retry_ms..120000, retries: 0..16 per
 * commit/confirm phase. Retries never extend the overall exchange deadline.
 * Times passed below are monotonic milliseconds from one clock, not UTC.
 */
struct r16_sae_exchange *r16_sae_exchange_create(
    const uint8_t own[6], const uint8_t peer[6], const uint8_t *ssid,
    size_t ssid_len, const uint8_t *password, size_t password_len, int h2e,
    uint32_t retry_ms, uint32_t timeout_ms, unsigned retries);
void r16_sae_exchange_destroy(struct r16_sae_exchange *exchange);
int r16_sae_exchange_start(struct r16_sae_exchange *exchange, uint64_t now_ms,
                          struct r16_sae_auth_body *out);
/* Foreign source/destination addresses are ignored. Out may alias body.
 * Unsupported or invalid messages from the selected peer fail closed;
 * while waiting for Confirm, repeated Commit messages are ignored as in
 * hostap's station SME. Accepted sessions ignore subsequent RX messages.
 */
int r16_sae_exchange_receive(struct r16_sae_exchange *exchange,
    const uint8_t source[6], const uint8_t destination[6], const uint8_t *body,
    size_t length, uint64_t now_ms, struct r16_sae_auth_body *out);
int r16_sae_exchange_poll(struct r16_sae_exchange *exchange, uint64_t now_ms,
                         struct r16_sae_auth_body *out);
/* Cancellation also revokes an already accepted session and clears material. */
void r16_sae_exchange_cancel(struct r16_sae_exchange *exchange);
enum r16_sae_exchange_state r16_sae_exchange_state(const struct r16_sae_exchange *exchange);
enum r16_sae_exchange_error r16_sae_exchange_error(const struct r16_sae_exchange *exchange);
/* Only after a cryptographically valid peer Confirm; zeroes outputs on error. */
int r16_sae_exchange_export(struct r16_sae_exchange *exchange,
                           uint8_t pmk[32], uint8_t pmkid[16]);
#ifdef __cplusplus
}
#endif
