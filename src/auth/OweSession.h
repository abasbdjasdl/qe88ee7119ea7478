// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>
// Userspace group-19 OWE DH component. No association transport, RSN policy,
// PMF, or four-way handshake. A derived PMK does NOT authorize a data port.
struct r16_owe;
struct r16_owe *r16_owe_create(int access_point);
void r16_owe_destroy(struct r16_owe *s);
// Complete extension IE: 255, 35, 32 (OWE DH), group-19 LE, 32-byte X.
int r16_owe_element(struct r16_owe *s,uint8_t *out,size_t capacity,size_t *written);
int r16_owe_receive(struct r16_owe *s,const uint8_t *ie,size_t length);
// Export only after valid peer DH; the caller must still complete RSN/4-way.
int r16_owe_export(struct r16_owe *s,uint8_t pmk[32],uint8_t pmkid[16]);
