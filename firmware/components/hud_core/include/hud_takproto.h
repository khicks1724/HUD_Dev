/*
 * hud_takproto.h - minimal decoder for TAK Protocol Version 1 (protobuf).
 *
 * ATAK can send SA on the multicast mesh as protobuf instead of XML:
 *     0xBF 0x01 0xBF <TakMessage protobuf>
 * (streaming connections use 0xBF <varint length> <TakMessage> after the
 * client negotiates; this HUD keeps streams on XML, so only the mesh
 * framing is needed in practice).
 *
 * Only the fields the HUD uses are decoded (takmessage.proto / cotevent.proto
 * / detail.proto / contact.proto / group.proto / track.proto from the TAK
 * Protocol specification). Unknown fields are skipped by wire type.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hud_cot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* True if the datagram starts with the TAK Protocol v1 mesh header. */
bool hud_takproto_is_mesh(const uint8_t *buf, size_t len);

/* Decode a mesh datagram (with header) into a CoT event. */
bool hud_takproto_decode_mesh(const uint8_t *buf, size_t len, hud_cot_event_t *out);

/* Decode a bare TakMessage protobuf (no header). */
bool hud_takproto_decode_message(const uint8_t *buf, size_t len, hud_cot_event_t *out);

#ifdef __cplusplus
}
#endif
