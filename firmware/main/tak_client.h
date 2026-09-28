/*
 * tak_client.h - TAK Server streaming client.
 *
 *   TAK_TCP : plain TCP, TAK Server "stcp" input (default 8087) or
 *             tools/sim_server.py
 *   TAK_TLS : mutual TLS (default 8089) with the device certificate and key
 *             provisioned into the hudcfg partition by tools/provision.py
 *
 * The server streams CoT XML <event>s; everything is fed through
 * hud_cot_stream and into app_ingest_cot(). The client sends a t-x-c-t ping
 * every 15 s and, when it has its own GNSS fix and send_sa is set, its own SA.
 */
#pragma once

void tak_client_start(void);
