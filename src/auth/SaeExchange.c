// SPDX-License-Identifier: BSD-3-Clause
#include "SaeExchange.h"
#include "SaeSession.h"
#include <stdlib.h>
#include <string.h>

/* Wire/state reference: hostap 24c033de87759e3f8818507a60d873899658a7cf,
 * wpa_supplicant/sme.c, sme_external_auth_build_buf(),
 * sme_external_auth_send_sae_commit(), sme_external_auth_send_sae_confirm(),
 * sme_sae_auth(). In particular, status 126 is Commit-only, not Confirm.
 */
struct r16_sae_exchange {
    struct r16_sae *session;
    enum r16_sae_exchange_state state;
    enum r16_sae_exchange_error error;
    uint8_t own[6], peer[6];
    int h2e;
    uint32_t retry_ms, timeout_ms;
    unsigned retries, retries_used;
    uint64_t last_ms, deadline_ms, retry_at_ms;
    struct r16_sae_auth_body commit, confirm;
};
static void erase(void *p, size_t length) {
    volatile uint8_t *b = (volatile uint8_t *)p;
    while (length--) *b++ = 0;
}
static void clear_material(struct r16_sae_exchange *x) {
    r16_sae_destroy(x->session);
    x->session = NULL;
    erase(&x->commit, sizeof(x->commit));
    erase(&x->confirm, sizeof(x->confirm));
    erase(x->own, sizeof(x->own));
    erase(x->peer, sizeof(x->peer));
}
static int fail(struct r16_sae_exchange *x, enum r16_sae_exchange_error why) {
    if (x) {
        clear_material(x);
        x->error = why;
        x->state = R16_SAE_FAILED;
    }
    return R16_SAE_EXCHANGE_ERROR;
}
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static void set_le16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static int check_time(struct r16_sae_exchange *x, uint64_t now) {
    if (!x || x->state == R16_SAE_FAILED || x->state == R16_SAE_CANCELLED)
        return R16_SAE_EXCHANGE_ERROR;
    if (x->state == R16_SAE_READY) return fail(x, R16_SAE_ERROR_SEQUENCE);
    if (now < x->last_ms) return fail(x, R16_SAE_ERROR_CLOCK);
    x->last_ms = now;
    if (x->state != R16_SAE_ACCEPTED && now >= x->deadline_ms)
        return fail(x, R16_SAE_ERROR_TIMEOUT);
    return R16_SAE_EXCHANGE_IDLE;
}
static void next_retry(struct r16_sae_exchange *x, uint64_t now) {
    uint64_t remaining = x->deadline_ms - now;
    x->retry_at_ms = now + (remaining < x->retry_ms ? remaining : x->retry_ms);
}
static int make_body(struct r16_sae_exchange *x, int confirm) {
    struct r16_sae_auth_body *body = confirm ? &x->confirm : &x->commit;
    size_t count = 0;
    set_le16(body->bytes, 3);
    set_le16(body->bytes + 2, confirm ? 2 : 1);
    set_le16(body->bytes + 4, !confirm && x->h2e ? 126 : 0);
    int error = confirm ?
        r16_sae_confirm(x->session, body->bytes + 6, sizeof(body->bytes) - 6, &count) :
        r16_sae_commit(x->session, body->bytes + 6, sizeof(body->bytes) - 6, &count);
    if (error || count != (confirm ? 34u : 98u))
        return fail(x, R16_SAE_ERROR_CRYPTO);
    body->length = count + 6;
    return R16_SAE_EXCHANGE_IDLE;
}
struct r16_sae_exchange *r16_sae_exchange_create(
    const uint8_t own[6], const uint8_t peer[6], const uint8_t *ssid,
    size_t ssid_len, const uint8_t *password, size_t password_len, int h2e,
    uint32_t retry_ms, uint32_t timeout_ms, unsigned retries) {
    if (!retry_ms || retry_ms > 10000 || timeout_ms < retry_ms ||
        timeout_ms > 120000 || retries > 16) return NULL;
    struct r16_sae *session = r16_sae_create(own, peer, ssid, ssid_len,
                                           password, password_len, h2e);
    if (!session) return NULL;
    struct r16_sae_exchange *x = calloc(1, sizeof(*x));
    if (!x) { r16_sae_destroy(session); return NULL; }
    x->session = session;
    x->h2e = h2e;
    memcpy(x->own, own, 6);
    memcpy(x->peer, peer, 6);
    x->retry_ms = retry_ms;
    x->timeout_ms = timeout_ms;
    x->retries = retries;
    return x;
}
void r16_sae_exchange_destroy(struct r16_sae_exchange *x) {
    if (!x) return;
    clear_material(x);
    erase(x, sizeof(*x));
    free(x);
}
int r16_sae_exchange_start(struct r16_sae_exchange *x, uint64_t now,
                          struct r16_sae_auth_body *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!x || !out) return fail(x, R16_SAE_ERROR_ARGUMENT);
    if (x->state != R16_SAE_READY) return R16_SAE_EXCHANGE_ERROR;
    if (now > UINT64_MAX - x->timeout_ms) return fail(x, R16_SAE_ERROR_CLOCK);
    x->last_ms = now;
    x->deadline_ms = now + x->timeout_ms;
    if (make_body(x, 0)) return R16_SAE_EXCHANGE_ERROR;
    x->state = R16_SAE_WAIT_COMMIT;
    next_retry(x, now);
    *out = x->commit;
    return R16_SAE_EXCHANGE_SEND;
}
int r16_sae_exchange_receive(struct r16_sae_exchange *x,
    const uint8_t source[6], const uint8_t destination[6], const uint8_t *body,
    size_t length, uint64_t now, struct r16_sae_auth_body *out) {
    /* Permit in-place receive/output without erasing the input packet first. */
    uint8_t input[R16_SAE_AUTH_BODY_MAX];
    if (body && length <= sizeof(input)) memcpy(input, body, length);
    if (out) memset(out, 0, sizeof(*out));
    if (!x || !out || !source || !destination || !body)
        return fail(x, R16_SAE_ERROR_ARGUMENT);
    if (check_time(x, now)) return R16_SAE_EXCHANGE_ERROR;
    if (x->state == R16_SAE_ACCEPTED) return R16_SAE_EXCHANGE_IDLE;
    if (memcmp(source, x->peer, 6) || memcmp(destination, x->own, 6))
        return R16_SAE_EXCHANGE_IDLE;
    if (length < 6 || length > sizeof(input) || le16(input) != 3)
        return fail(x, R16_SAE_ERROR_PROTOCOL);
    uint16_t transaction = le16(input + 2), status = le16(input + 4);
    if (transaction == 1) {
        if (status != (x->h2e ? 126 : 0) || length != 104)
            return fail(x, R16_SAE_ERROR_PROTOCOL);
        /* Do not reprocess a commit or reset the retry/deadline on duplicates. */
        if (x->state == R16_SAE_WAIT_CONFIRM) return R16_SAE_EXCHANGE_IDLE;
        if (r16_sae_receive_commit(x->session, input + 6, length - 6))
            return fail(x, R16_SAE_ERROR_CRYPTO);
        if (make_body(x, 1)) return R16_SAE_EXCHANGE_ERROR;
        x->state = R16_SAE_WAIT_CONFIRM;
        x->retries_used = 0;
        next_retry(x, now);
        *out = x->confirm;
        return R16_SAE_EXCHANGE_SEND;
    }
    if (transaction != 2) return fail(x, R16_SAE_ERROR_SEQUENCE);
    if (status != 0 || length != 40) return fail(x, R16_SAE_ERROR_PROTOCOL);
    if (x->state != R16_SAE_WAIT_CONFIRM) return fail(x, R16_SAE_ERROR_SEQUENCE);
    if (r16_sae_receive_confirm(x->session, input + 6, length - 6))
        return fail(x, R16_SAE_ERROR_CRYPTO);
    x->state = R16_SAE_ACCEPTED;
    erase(&x->commit, sizeof(x->commit));
    erase(&x->confirm, sizeof(x->confirm));
    return R16_SAE_EXCHANGE_ACCEPTED;
}
int r16_sae_exchange_poll(struct r16_sae_exchange *x, uint64_t now,
                         struct r16_sae_auth_body *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!x || !out) return fail(x, R16_SAE_ERROR_ARGUMENT);
    if (check_time(x, now)) return R16_SAE_EXCHANGE_ERROR;
    if (x->state == R16_SAE_ACCEPTED || now < x->retry_at_ms)
        return R16_SAE_EXCHANGE_IDLE;
    if (x->retries_used == x->retries) return fail(x, R16_SAE_ERROR_RETRIES);
    ++x->retries_used;
    /* Reusing a Confirm after AP acceptance is a replay there. Recalculate
     * with an incremented send-confirm as hostap's retransmit FSM does. */
    if (x->state == R16_SAE_WAIT_CONFIRM && make_body(x, 1))
        return R16_SAE_EXCHANGE_ERROR;
    next_retry(x, now);
    *out = x->state == R16_SAE_WAIT_COMMIT ? x->commit : x->confirm;
    return R16_SAE_EXCHANGE_SEND;
}
void r16_sae_exchange_cancel(struct r16_sae_exchange *x) {
    if (!x) return;
    clear_material(x);
    x->state = R16_SAE_CANCELLED;
    x->error = R16_SAE_ERROR_CANCELLED;
}
enum r16_sae_exchange_state r16_sae_exchange_state(const struct r16_sae_exchange *x) {
    return x ? x->state : R16_SAE_FAILED;
}
enum r16_sae_exchange_error r16_sae_exchange_error(const struct r16_sae_exchange *x) {
    return x ? x->error : R16_SAE_ERROR_ARGUMENT;
}
int r16_sae_exchange_export(struct r16_sae_exchange *x, uint8_t pmk[32], uint8_t pmkid[16]) {
    if (pmk) erase(pmk, 32);
    if (pmkid) erase(pmkid, 16);
    if (!x || x->state != R16_SAE_ACCEPTED || !pmk || !pmkid) return -1;
    return r16_sae_export(x->session, pmk, pmkid);
}
