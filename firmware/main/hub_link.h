#pragma once

/* V2H hub backpack link: UART1 lines + SPI3-slave thermal frames.
 * No-op unless CONFIG_HUD_HUB. See hardware/hub_backpack/README.md. */
void hub_link_start(void);
