/*
 * board.h - Waveshare ESP32-S3-LCD-1.3 (SKU 30559 = "-C", case + prism)
 *
 * Pin map read from Waveshare's schematic ESP32S3_1.3inch.pdf (2025-03-19):
 *   https://files.waveshare.com/wiki/ESP32-S3-LCD-1.3/ESP32S3_1.3inch.pdf
 * USB-C goes through a CH343P USB-UART bridge on U0TXD/U0RXD (GPIO43/44),
 * so flashing/monitor use the CH343 COM port, not native USB-JTAG.
 */
#pragma once

#include "driver/gpio.h"

/* ST7789V2 240x240, SPI write-only */
#define BOARD_LCD_HOST      SPI2_HOST
#define BOARD_LCD_MOSI      GPIO_NUM_41
#define BOARD_LCD_SCLK      GPIO_NUM_40
#define BOARD_LCD_CS        GPIO_NUM_39
#define BOARD_LCD_DC        GPIO_NUM_38
#define BOARD_LCD_RST       GPIO_NUM_42
#define BOARD_LCD_BL        GPIO_NUM_20   /* via NPN, PWM-able, active high */
#define BOARD_LCD_W         240
#define BOARD_LCD_H         240

/* QMI8658A 6-axis IMU, I2C (5.1k pull-ups on board) */
#define BOARD_IMU_SDA       GPIO_NUM_47
#define BOARD_IMU_SCL       GPIO_NUM_48
#define BOARD_IMU_INT1      GPIO_NUM_46
#define BOARD_IMU_INT2      GPIO_NUM_45

/* micro-SD in SPI mode */
#define BOARD_SD_MISO       GPIO_NUM_16
#define BOARD_SD_CS         GPIO_NUM_17
#define BOARD_SD_MOSI       GPIO_NUM_18
#define BOARD_SD_CLK        GPIO_NUM_21

/* Misc */
#define BOARD_BTN_BOOT      GPIO_NUM_0    /* BOOT key, usable as a user button after boot */
#define BOARD_RGB_LED       GPIO_NUM_15   /* WS2812B */
#define BOARD_VBAT_ADC      GPIO_NUM_6    /* VBAT through 100k/100k divider (x2) */
#define BOARD_RST_CONTROL   GPIO_NUM_19   /* drives CHIP_EN via NPN: never drive high */

/*
 * Header pins free for the backpack board (H1/H2): GPIO1-5, 7-14.
 * Assignments used by hardware/backpack (see hardware/backpack/README.md):
 */
#define BACKPACK_I2C_SDA    GPIO_NUM_10   /* magnetometer (+ optional fuel gauge) */
#define BACKPACK_I2C_SCL    GPIO_NUM_11
#define BACKPACK_MAG_DRDY   GPIO_NUM_9
#define BACKPACK_GNSS_RX    GPIO_NUM_12   /* ESP RX  <- GNSS TX */
#define BACKPACK_GNSS_TX    GPIO_NUM_13   /* ESP TX  -> GNSS RX */
#define BACKPACK_GNSS_PPS   GPIO_NUM_14
#define BACKPACK_BTN_A      GPIO_NUM_7    /* optional second button / encoder A */
#define BACKPACK_BTN_B      GPIO_NUM_8    /* optional encoder B */
#define BACKPACK_HAPTIC     GPIO_NUM_5    /* optional vibration motor driver */
