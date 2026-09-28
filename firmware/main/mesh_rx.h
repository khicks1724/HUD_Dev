/*
 * mesh_rx.h - UDP inputs that do not need a TAK Server:
 *
 *  1. ATAK SA multicast 239.2.3.1:6969 ("mesh" mode, XML or TAK Protocol v1
 *     protobuf): every ATAK device on the same Wi-Fi broadcasts its own position here by default, so the HUD
 *     can show teammates and follow its owner's phone with no server at all.
 *
 *  2. A small text protocol on g_cfg.udp_port (default 4349) for own
 *     position/heading from a phone app, a PC, or tools/sim_server.py:
 *        POS,<lat>,<lon>,<hae_m>
 *        HDG,<true_heading_deg>[,<gain 0..1>]
 *     A full CoT <event> sent to this port is also accepted.
 */
#pragma once

void mesh_rx_start(void);
