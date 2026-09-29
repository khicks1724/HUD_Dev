#pragma once

/*
 * usb_link.h - the USB-C serial port (CH343, UART0 at 2 Mbit/s).
 *
 * 2 Mbit/s once the app is up (the boot log before that is at 115200).
 * One byte stream carries two things:
 *   - text lines: console commands (help, trk, fix, show, ...), same as typing
 *     in a terminal; output comes back as text (and "@HUD {json}" lines)
 *   - binary packets from a host app (the WinTAK plugin), each
 *       A5 5A | type u8 | flags u8 | w u16 | h u16 | hfov_cdeg u16 | len u32 |
 *       payload[len] | sum16 u16          (all little-endian)
 *     type 1: thermal frame, w*h bytes of 8-bit intensity (0 = cold/black),
 *             hfov_cdeg = camera horizontal FOV in 1/100 deg (e.g. 3200)
 *     type 2: thermal palette, 256 x RGB888 (768 bytes), index = intensity
 *     type 3: thermal source off (drop the USB frame, go back to greyscale)
 *   0xA5 never starts a text line, so a terminal and the plugin can share it.
 */
#include <stdint.h>

void usb_link_start(void);

/* Binary packets received OK / with a bad checksum / abandoned mid-way. */
void usb_link_stats(uint32_t *ok, uint32_t *bad, uint32_t *resync);
