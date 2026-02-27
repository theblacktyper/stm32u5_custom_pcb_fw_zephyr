/*
 * Copyright (c) 2019 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * Based on ST7789V sample:
 * Copyright (c) 2019 Marc Reilly
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(kk_edge_ai, LOG_LEVEL_INF);

#include "main_functions.h"  /* TFLM: overlay get/ready (draw); setup/fill (inference thread) */

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <version.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* size of stack area used by threads */
#define DEFAULT_STACKSIZE    1024
#define INFERENCE_STACKSIZE  2048
#define CAMERA_STACKSIZE     4096

/* scheduling priority: lower number = higher priority in Zephyr */
#define EQUAL_PRIORITY    7
#define PRIORITY_LED      EQUAL_PRIORITY//5   /* LEDs preempt camera/display for crisp toggling */
#define PRIORITY_CAMERA   EQUAL_PRIORITY//7   /* camera + display */

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

#if !DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS_OKAY(LED1_NODE)
#error "Unsupported board: led1 devicetree alias is not defined"
#endif

#define SW0_NODE DT_ALIAS(sw0)

#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

/* Configure button (sw0) as capture trigger */
const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

/* CAMERA: sensor for readiness check, DCMI controller for all video API ops */
const struct device *ov5640 = DEVICE_DT_GET(DT_NODELABEL(ov5640));
const struct device *video_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

/*
 * STM32U5 GPDMA block-transfer limit is 65 535 bytes (16-bit BNDT register).
 * 320x240 RGB565 = 153 600 bytes — exceeds the limit, HAL rejects the DMA.
 * 160x120 RGB565 =  38 400 bytes — fits comfortably.
 */
#define CAMERA_W          160
#define CAMERA_H          120
#define DISPLAY_W         240
#define DISPLAY_H         135
#define FRAME_X_OFFSET    ((DISPLAY_W - CAMERA_W) / 2)   /* center 160 on 240 */
#define FRAME_Y_OFFSET    ((DISPLAY_H - CAMERA_H) / 2)   /* center 120 on 135 */

#define STANDBY_TEXT_MAX_LEN 24

/*
 * Capture mode: 0 = SNAPSHOT, 1 = CONTINUOUS.
 * Note: The DCMI driver captures one frame per video_stream_start; it does not
 * produce a continuous stream. Both modes use start/stop per frame.
 */
#define CAMERA_CAPTURE_MODE_CONTINUOUS  0  // TBD: need implement REAL continuous mode

static atomic_t show_camera_frame = ATOMIC_INIT(0);
static K_SEM_DEFINE(capture_sem, 0, 1);

/* FPS measurement */
static uint32_t frame_count;
static int64_t fps_start_ms;
static float fps_current;
static float fps_last_logged = -1.0f;

struct led {
	struct gpio_dt_spec spec;
	uint8_t num;
};

static const struct led led0 = {
	.spec = GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0}),
	.num = 0,
};

static const struct led led1 = {
	.spec = GPIO_DT_SPEC_GET_OR(LED1_NODE, gpios, {0}),
	.num = 1,
};

static struct gpio_callback button_cb_data;

/* ---- 8x16 bitmap font for printable ASCII 0x20-0x7E (VGA style) -------- */
#define FONT_W  8
#define FONT_H  16
#define FONT_SCALE_LARGE_NUM  3   /* 1.5x scaling: 3/2 yields 12x24 glyphs */
#define FONT_SCALE_LARGE_DEN  2
#define FONT_FIRST_CHAR  0x20
#define FONT_LAST_CHAR   0x7E
#define DISPLAY_TEXT_MAX_LEN 32

/* Convenience colour constants (0x00RRGGBB) */
#define COLOR_BLACK   0x00000000u
#define COLOR_WHITE   0x00FFFFFFu
#define COLOR_RED     0x00FF0000u
#define COLOR_GREEN   0x0000FF00u
#define COLOR_BLUE    0x000000FFu
#define COLOR_YELLOW  0x00FFFF00u
#define COLOR_CYAN    0x0000FFFFu
#define COLOR_MAGENTA 0x00FF00FFu
#define COLOR_ORANGE  0x00FFA500u

static const uint8_t font_ascii[95][FONT_H] = {
	/* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x21 '!' */ {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
	/* 0x22 '"' */ {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x23 '#' */ {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
	/* 0x24 '$' */ {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
	/* 0x25 '%' */ {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00},
	/* 0x26 '&' */ {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	/* 0x27 ''' */ {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x28 '(' */ {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
	/* 0x29 ')' */ {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00},
	/* 0x2A '*' */ {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x2B '+' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x2C ',' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00},
	/* 0x2D '-' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x2E '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
	/* 0x2F '/' */ {0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00},
	/* 0x30 '0' */ {0x00,0x00,0x3C,0x66,0x66,0x76,0x6E,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
	/* 0x31 '1' */ {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
	/* 0x32 '2' */ {0x00,0x00,0x3C,0x66,0x06,0x0C,0x18,0x30,0x60,0x66,0x7E,0x00,0x00,0x00,0x00,0x00},
	/* 0x33 '3' */ {0x00,0x00,0x3C,0x66,0x06,0x1C,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
	/* 0x34 '4' */ {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00},
	/* 0x35 '5' */ {0x00,0x00,0x7E,0x60,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
	/* 0x36 '6' */ {0x00,0x00,0x1C,0x30,0x60,0x7C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
	/* 0x37 '7' */ {0x00,0x00,0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
	/* 0x38 '8' */ {0x00,0x00,0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
	/* 0x39 '9' */ {0x00,0x00,0x3C,0x66,0x66,0x3E,0x06,0x06,0x06,0x0C,0x38,0x00,0x00,0x00,0x00,0x00},
	/* 0x3A ':' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
	/* 0x3B ';' */ {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
	/* 0x3C '<' */ {0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00},
	/* 0x3D '=' */ {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x3E '>' */ {0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
	/* 0x3F '?' */ {0x00,0x00,0x3C,0x66,0x66,0x06,0x0C,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
	/* 0x40 '@' */ {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00},
	/* 0x41 'A' */ {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	/* 0x42 'B' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
	/* 0x43 'C' */ {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00},
	/* 0x44 'D' */ {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
	/* 0x45 'E' */ {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
	/* 0x46 'F' */ {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
	/* 0x47 'G' */ {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00},
	/* 0x48 'H' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	/* 0x49 'I' */ {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	/* 0x4A 'J' */ {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
	/* 0x4B 'K' */ {0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
	/* 0x4C 'L' */ {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
	/* 0x4D 'M' */ {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	/* 0x4E 'N' */ {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	/* 0x4F 'O' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	/* 0x50 'P' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
	/* 0x51 'Q' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00},
	/* 0x52 'R' */ {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
	/* 0x53 'S' */ {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	/* 0x54 'T' */ {0x00,0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	/* 0x55 'U' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	/* 0x56 'V' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
	/* 0x57 'W' */ {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00},
	/* 0x58 'X' */ {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00},
	/* 0x59 'Y' */ {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
	/* 0x5A 'Z' */ {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00},
	/* 0x5B '[' */ {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},
	/* 0x5C '\' */ {0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00},
	/* 0x5D ']' */ {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},
	/* 0x5E '^' */ {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x5F '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00},
	/* 0x60 '`' */ {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	/* 0x61 'a' */ {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	/* 0x62 'b' */ {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},
	/* 0x63 'c' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
	/* 0x64 'd' */ {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	/* 0x65 'e' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
	/* 0x66 'f' */ {0x00,0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
	/* 0x67 'g' */ {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00},
	/* 0x68 'h' */ {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
	/* 0x69 'i' */ {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	/* 0x6A 'j' */ {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00},
	/* 0x6B 'k' */ {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
	/* 0x6C 'l' */ {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	/* 0x6D 'm' */ {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00},
	/* 0x6E 'n' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
	/* 0x6F 'o' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	/* 0x70 'p' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
	/* 0x71 'q' */ {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00},
	/* 0x72 'r' */ {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
	/* 0x73 's' */ {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00},
	/* 0x74 't' */ {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00},
	/* 0x75 'u' */ {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	/* 0x76 'v' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00},
	/* 0x77 'w' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00},
	/* 0x78 'x' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
	/* 0x79 'y' */ {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00},
	/* 0x7A 'z' */ {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00},
	/* 0x7B '{' */ {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},
	/* 0x7C '|' */ {0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
	/* 0x7D '}' */ {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00},
	/* 0x7E '~' */ {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* ----- Pixel helpers ---------------------------------------------------- */

static uint8_t get_bpp(enum display_pixel_format fmt)
{
	switch (fmt) {
	case PIXEL_FORMAT_ARGB_8888: return 4;
	case PIXEL_FORMAT_RGB_888:   return 3;
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565:   return 2;
	default:                     return 1;
	}
}

/**
 * Write one pixel in a given colour (0x00RRGGBB) into the buffer.
 */
static inline void set_pixel_color(uint8_t *buf, uint32_t offset,
				   enum display_pixel_format fmt,
				   uint32_t color)
{
	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >>  8) & 0xFF;
	uint8_t b =  color        & 0xFF;

	switch (fmt) {
	case PIXEL_FORMAT_RGB_565: {
		uint16_t c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
		buf[offset]     = (c565 >> 8) & 0xFF;
		buf[offset + 1] =  c565       & 0xFF;
		break;
	}
	case PIXEL_FORMAT_BGR_565: {
		uint16_t c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
		*(uint16_t *)(buf + offset) = c565;
		break;
	}
	case PIXEL_FORMAT_RGB_888:
		buf[offset]     = r;
		buf[offset + 1] = g;
		buf[offset + 2] = b;
		break;
	case PIXEL_FORMAT_ARGB_8888:
		*(uint32_t *)(buf + offset) = 0xFF000000u | color;
		break;
	default:
		buf[offset] = (uint8_t)(((uint16_t)r + g + b) / 3);
		break;
	}
}

/**
 * Format __DATE__ and __TIME__ into "Built: YYYY-MM-DD HH:MM" (24h).
 * __DATE__ is "Mmm dd yyyy", __TIME__ is "HH:MM:SS".
 */
static void format_build_time(char *dst, size_t size)
{
	static const char *const months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	int month = 1;
	for (int i = 0; i < 12; i++) {
		if (memcmp(__DATE__, months[i], 3) == 0) {
			month = i + 1;
			break;
		}
	}
	int day = atoi(__DATE__ + 4);
	int year = atoi(__DATE__ + 7);
	int h = atoi(__TIME__);
	int m = atoi(__TIME__ + 3);

	/* Clamp to valid ranges (satisfies -Wformat-truncation) */
	if (month < 1) month = 1; else if (month > 12) month = 12;
	if (day < 1) day = 1; else if (day > 31) day = 31;
	if (year < 1900) year = 1900; else if (year > 9999) year = 9999;
	if (h < 0) h = 0; else if (h > 23) h = 23;
	if (m < 0) m = 0; else if (m > 59) m = 59;

	snprintk(dst, size, "Built: %04d-%02d-%02d %02d:%02d",
		 year, month, day, h, m);
}

/* Return x position to center text of given pixel width on display. */
static uint16_t text_center_x(uint16_t screen_w, uint16_t text_w)
{
	return (screen_w > text_w) ? (screen_w - text_w) / 2 : 0;
}

/* ----- General-purpose text rendering ----------------------------------- */

/**
 * Return the glyph bitmap for any printable ASCII character (0x20-0x7E).
 * Returns NULL for unsupported characters.
 */
static const uint8_t *glyph_for_char(char ch)
{
	if (ch >= FONT_FIRST_CHAR && ch <= FONT_LAST_CHAR) {
		return font_ascii[ch - FONT_FIRST_CHAR];
	}
	return NULL;
}

/**
 * Render a string at (x_pos, y_pos) with configurable colours.
 *
 * @param dev        Display device
 * @param caps       Display capabilities
 * @param str        Null-terminated string (max DISPLAY_TEXT_MAX_LEN chars)
 * @param x_pos      X pixel coordinate (top-left of the text area)
 * @param y_pos      Y pixel coordinate (top-left of the text area)
 * @param fg_color   Foreground colour  (0x00RRGGBB)
 * @param bg_color   Background colour  (0x00RRGGBB)
 * @param text_buf   Working buffer (>= len * out_w * out_h * bpp bytes)
 * @param bpp        Bytes per pixel for the current pixel format
 * @param scale_num  Scale numerator (e.g. 3 for 1.5x)
 * @param scale_den  Scale denominator (e.g. 2 for 1.5x); 1 = 8x16, 3/2 = 12x24
 */
static void display_text(const struct device *dev,
			 const struct display_capabilities *caps,
			 const char *str,
			 uint16_t x_pos, uint16_t y_pos,
			 uint32_t fg_color, uint32_t bg_color,
			 uint8_t *text_buf, uint8_t bpp,
			 int scale_num, int scale_den)
{
	int len = strlen(str);

	if (len > DISPLAY_TEXT_MAX_LEN) {
		len = DISPLAY_TEXT_MAX_LEN;
	}
	if (scale_num < 1) scale_num = 1;
	if (scale_den < 1) scale_den = 1;
	if (scale_num > FONT_SCALE_LARGE_NUM) scale_num = FONT_SCALE_LARGE_NUM;
	if (scale_den > FONT_SCALE_LARGE_DEN) scale_den = FONT_SCALE_LARGE_DEN;

	uint16_t glyph_w = FONT_W * scale_num / scale_den;
	uint16_t glyph_h = FONT_H * scale_num / scale_den;
	if (glyph_w < 1) glyph_w = 1;
	if (glyph_h < 1) glyph_h = 1;

	uint16_t text_w = len * glyph_w;
	uint16_t text_h = glyph_h;
	uint32_t total_px = (uint32_t)text_w * text_h;

	/* Fill background */
	if (bg_color == COLOR_BLACK) {
		(void)memset(text_buf, 0x00, total_px * bpp);
	} else {
		for (uint32_t px = 0; px < total_px; px++) {
			set_pixel_color(text_buf, px * bpp,
					caps->current_pixel_format,
					bg_color);
		}
	}

	/* Render each character */
	for (int c = 0; c < len; c++) {
		const uint8_t *glyph = glyph_for_char(str[c]);

		if (!glyph) {
			continue; /* unsupported char – leave background */
		}
		if (scale_num == 1 && scale_den == 1) {
			/* 1:1 – direct copy */
			for (int row = 0; row < FONT_H; row++) {
				uint8_t bits = glyph[row];
				for (int col = 0; col < FONT_W; col++) {
					if (bits & (0x80 >> col)) {
						uint32_t px = row * text_w + c * FONT_W + col;
						set_pixel_color(text_buf, px * bpp,
								caps->current_pixel_format,
								fg_color);
					}
				}
			}
		} else {
			/* Scaled: nearest-neighbor sample from 8x16 to glyph_w x glyph_h */
			for (int dy = 0; dy < glyph_h; dy++) {
				int src_row = dy * FONT_H / glyph_h;
				uint8_t bits = glyph[src_row];
				for (int dx = 0; dx < glyph_w; dx++) {
					int src_col = dx * FONT_W / glyph_w;
					if (bits & (0x80 >> src_col)) {
						uint32_t px = dy * text_w + (c * glyph_w + dx);
						set_pixel_color(text_buf, px * bpp,
								caps->current_pixel_format,
								fg_color);
					}
				}
			}
		}
	}

	struct display_buffer_descriptor desc = {
		.buf_size = total_px * bpp,
		.width  = text_w,
		.height = text_h,
		.pitch  = text_w,
	};

	display_write(dev, x_pos, y_pos, &desc, text_buf);
}

/* Button 2 (sw0) */
static void button_pressed_cb(const struct device *dev,
			      struct gpio_callback *cb,
			      uint32_t pins)
{
	k_sem_give(&capture_sem);
}

/* ---- Camera ---------------------------------------------------- */
/*
 * Set one pixel in the display buffer (RGB565, DISPLAY_W x DISPLAY_H).
 * (dx, dy) are display coordinates; color is 0x00RRGGBB.
 */
static void set_display_pixel_rgb565(uint8_t *dst, uint16_t dx, uint16_t dy,
                                     uint32_t color)
{
    if (dx >= DISPLAY_W || dy >= DISPLAY_H) {
        return;
    }

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >>  8) & 0xFF;
    uint8_t b =  color        & 0xFF;
    uint16_t c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    /* Write as [high byte][low byte] to match display expectations */
    uint32_t pixel_index = (uint32_t)dy * DISPLAY_W + dx;
    uint32_t byte_index  = pixel_index * 2U;

    dst[byte_index]     = (uint8_t)((c565 >> 8) & 0xFF);
    dst[byte_index + 1] = (uint8_t)(c565 & 0xFF);
}

/*
 * Draw a line segment in the display buffer (RGB565) using Bresenham.
 */
static void draw_line_rgb565(uint8_t *dst, int x0, int y0, int x1, int y1,
			     uint32_t color)
{
	int dx = x1 - x0;
	int dy = y1 - y0;
	int ax = (dx < 0) ? -dx : dx;
	int ay = (dy < 0) ? -dy : dy;
	int steps = (ax > ay) ? ax : ay;
	if (steps <= 0) {
		set_display_pixel_rgb565(dst, (uint16_t)x0, (uint16_t)y0, color);
		return;
	}
	for (int i = 0; i <= steps; i++) {
		int t = (steps == 0) ? 0 : (i * 65536 / steps);
		int x = x0 + (dx * t) / 65536;
		int y = y0 + (dy * t) / 65536;
		set_display_pixel_rgb565(dst, (uint16_t)x, (uint16_t)y, color);
	}
}

/*
 * Draw sine overlay from precomputed buffer (filled by inference thread).
 * No TFLM inference in this thread; read-only for person-detection-ready design.
 */
static void draw_sine_overlay(uint8_t *dst)
{
	const int center_y = FRAME_Y_OFFSET + CAMERA_H / 2;
	const int amplitude = (CAMERA_H / 2) - 4;
	if (amplitude <= 0) {
		return;
	}
	if (!tflm_sine_overlay_is_ready()) {
		return;
	}

	const float *y_values;
	int num_points;
	tflm_sine_overlay_get(&y_values, &num_points);
	if (num_points <= 0) {
		return;
	}

	int prev_px = -1;
	int prev_py = -1;
	for (int i = 0; i < num_points; i++) {
		float y = y_values[i];
		int px = FRAME_X_OFFSET + (int)((float)i * (float)(CAMERA_W - 1) /
						(float)(num_points > 1 ? num_points - 1 : 1));
		if (px >= FRAME_X_OFFSET + (int)CAMERA_W) {
			px = FRAME_X_OFFSET + CAMERA_W - 1;
		}
		int py = center_y - (int)(y * (float)amplitude);

		if (py < (int)FRAME_Y_OFFSET) {
			py = FRAME_Y_OFFSET;
		}
		if (py >= FRAME_Y_OFFSET + (int)CAMERA_H) {
			py = FRAME_Y_OFFSET + CAMERA_H - 1;
		}

		set_display_pixel_rgb565(dst, (uint16_t)px, (uint16_t)py, COLOR_GREEN);
		if (prev_px >= 0) {
			draw_line_rgb565(dst, prev_px, prev_py, px, py, COLOR_GREEN);
		}
		prev_px = px;
		prev_py = py;
	}
}

/**
 * Copy 160x120 frame 1:1 (no scaling), centred on 240x135 display.
 * Black borders fill the margins.
 * Then draw the Zephyr hello_world-style sine wave overlay on the frame.
 */
static void copy_frame_to_display(const uint8_t *src, uint8_t *dst)
{
	/* One-time clear of borders; camera region is fully overwritten each frame. */
	static int borders_cleared;

	const uint16_t *s = (const uint16_t *)src;
	uint16_t *d = (uint16_t *)dst;

	if (!borders_cleared) {
		memset(dst, 0x00, DISPLAY_W * DISPLAY_H * sizeof(uint16_t));
		borders_cleared = 1;
	}

	for (int y = 0; y < CAMERA_H; y++) {
		const uint16_t *src_row = s + y * CAMERA_W;
		uint16_t *dst_row = d + (FRAME_Y_OFFSET + y) * DISPLAY_W + FRAME_X_OFFSET;

		memcpy(dst_row, src_row, CAMERA_W * sizeof(uint16_t));
	}

	draw_sine_overlay(dst);
}

void camera_thread(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button device not ready");
		return;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure button: %d", ret);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure button interrupt: %d", ret);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed_cb, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

    LOG_INF("===== Camera Config Info =====");

	/* Get display device for writing the captured frame */
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(disp)) {
		LOG_ERR("> Display device not ready in camera thread");
		return;
	}

	/* Allocate video buffers from the video buffer pool */
	size_t frame_size = CAMERA_W * CAMERA_H * sizeof(uint16_t);
	struct video_buffer *vbufs[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX];

	for (int i = 0; i < ARRAY_SIZE(vbufs); i++) {
		vbufs[i] = video_buffer_aligned_alloc(
				frame_size,
				CONFIG_VIDEO_BUFFER_POOL_ALIGN,
				K_FOREVER);
		if (vbufs[i] == NULL) {
			LOG_ERR("> Failed to allocate video buffer %d", i);
			return;
		}
		vbufs[i]->type = VIDEO_BUF_TYPE_OUTPUT;
	}

	/* Allocate output buffer: full display width so borders are included */
	uint8_t *disp_buf = k_malloc(DISPLAY_W * DISPLAY_H * sizeof(uint16_t));

	if (disp_buf == NULL) {
		LOG_ERR("> Failed to allocate camera display buffer");
		return;
	}

	/* Log the format the DCMI driver actually stored */
	struct video_format active_fmt = { .type = VIDEO_BUF_TYPE_OUTPUT };

	video_get_format(video_dev, &active_fmt);
	LOG_INF("DCMI fmt: %ux%u pitch=%u pixfmt=0x%08x",
		active_fmt.width, active_fmt.height,
		active_fmt.pitch, active_fmt.pixelformat);
	LOG_INF("Vbuf size=%u, buf[0]=%p",
		(uint32_t)frame_size, (void *)vbufs[0]->buffer);

	// LOG_INF("Streaming in %s mode\n",
	// 	CAMERA_CAPTURE_MODE_CONTINUOUS ? "continuous" : "snapshot");
	LOG_INF("\n");

	/* Wait for SW0 press before starting capture; then run until power cycle */
	k_sem_take(&capture_sem, K_FOREVER);

	LOG_INF("Capture started");

	/* Enqueue all buffers before starting */
	for (int i = 0; i < ARRAY_SIZE(vbufs); i++) {
		ret = video_enqueue(video_dev, vbufs[i]);
		if (ret < 0) {
			LOG_ERR("> video_enqueue[%d] failed: %d", i, ret);
			return;
		}
	}

	frame_count = 0;
	fps_start_ms = k_uptime_get();

	while (1) {
		struct video_buffer *vbuf;

		/* DCMI driver captures one frame per start; both modes need start per frame */
		ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
		if (ret < 0) {
			LOG_ERR("> Failed to start video stream: %d", ret);
			video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT);
			{
				struct video_buffer *tmp;
				while (video_dequeue(video_dev, &tmp, K_NO_WAIT) == 0) {
					video_enqueue(video_dev, tmp);
				}
			}
			k_msleep(100);
			continue;
		}

		ret = video_dequeue(video_dev, &vbuf, K_MSEC(100));
		if (ret < 0) {
			video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT);
			{
				struct video_buffer *tmp;
				while (video_dequeue(video_dev, &tmp, K_NO_WAIT) == 0) {
					video_enqueue(video_dev, tmp);
				}
			}
			k_msleep(1);
			continue;
		}

		video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT);

		copy_frame_to_display(vbuf->buffer, disp_buf);

		atomic_set(&show_camera_frame, 1);

		struct display_buffer_descriptor desc = {
			.buf_size = DISPLAY_W * DISPLAY_H * sizeof(uint16_t),
			.width  = DISPLAY_W,
			.height = DISPLAY_H,
			.pitch  = DISPLAY_W,
		};

		display_write(disp, 0, 0, &desc, disp_buf);

		/* FPS measurement: log once per second, only when value changed */
		frame_count++;
		{
			int64_t elapsed = k_uptime_get() - fps_start_ms;
			if (elapsed >= 1000) {
				fps_current = (float)frame_count * 1000.0f / (float)elapsed;
				frame_count = 0;
				fps_start_ms = k_uptime_get();
				if (fps_last_logged < 0 ||
				    fabsf(fps_current - fps_last_logged) >= 0.05f) {
					LOG_INF("FPS: %.1f", (double)fps_current);
					fps_last_logged = fps_current;
				}
			}
		}

		/* Re-enqueue for next capture */
		video_enqueue(video_dev, vbuf);

		/* Drain any extra buffers */
		{
			struct video_buffer *tmp;
			while (video_dequeue(video_dev, &tmp, K_NO_WAIT) == 0) {
				video_enqueue(video_dev, tmp);
			}
		}
	}
}


void display_thread(void)
{
	size_t rect_w;
	size_t rect_h;
	size_t h_step;
	size_t scale;
	uint8_t *buf;
	int32_t grey_scale_sleep;
	const struct device *display_dev;
	struct display_capabilities capabilities;
	struct display_buffer_descriptor buf_desc;
	size_t buf_size = 0;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found. Aborting sample.",
			display_dev->name);
		return;
	}

	// LOG_INF("Display %s Thread Starts", display_dev->name);
	display_get_capabilities(display_dev, &capabilities);

	if (capabilities.screen_info & SCREEN_INFO_MONO_VTILED) {
		rect_w = 16;
		rect_h = 8;
	} else {
		rect_w = 2;
		rect_h = 1;
	}

	if ((capabilities.x_resolution < 3 * rect_w) ||
	    (capabilities.y_resolution < 3 * rect_h) ||
	    (capabilities.x_resolution < 8 * rect_h)) {
		rect_w = capabilities.x_resolution * 40 / 100;
		rect_h = capabilities.y_resolution * 40 / 100;
		h_step = capabilities.y_resolution * 20 / 100;
		scale = 1;
	} else {
		h_step = rect_h;
		scale = (capabilities.x_resolution / 8) / rect_h;
	}

	rect_w *= scale;
	rect_h *= scale;

	if (capabilities.screen_info & SCREEN_INFO_EPD) {
		grey_scale_sleep = 10000;
	} else {
		grey_scale_sleep = 100;
	}

	if (capabilities.screen_info & SCREEN_INFO_X_ALIGNMENT_WIDTH) {
		rect_w = capabilities.x_resolution;
	}

	buf_size = rect_w * rect_h;

	if (buf_size < (capabilities.x_resolution * h_step)) {
		buf_size = capabilities.x_resolution * h_step;
	}

	switch (capabilities.current_pixel_format) {
	case PIXEL_FORMAT_ARGB_8888:
		buf_size *= 4;
		break;
	case PIXEL_FORMAT_RGB_888:
		buf_size *= 3;
		break;
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565:
		buf_size *= 2;
		break;
	case PIXEL_FORMAT_L_8:
		break;
	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10:
		buf_size = DIV_ROUND_UP(DIV_ROUND_UP(
			buf_size, NUM_BITS(uint8_t)), sizeof(uint8_t));
		break;
	default:
		LOG_ERR("Unsupported pixel format. Aborting sample.");
 		return;
	}

	buf = k_malloc(buf_size);

	if (buf == NULL) {
		LOG_ERR("Could not allocate memory. Aborting sample.");
 		return;
	}

	/* Black background fill */
	(void)memset(buf, 0, buf_size);

	buf_desc.buf_size = buf_size;
	buf_desc.pitch = capabilities.x_resolution;
	buf_desc.width = capabilities.x_resolution;
	buf_desc.height = h_step;
	buf_desc.frame_incomplete = true;

	for (int idx = 0; idx < capabilities.y_resolution; idx += h_step) {
		if ((capabilities.y_resolution - idx) < h_step) {
			buf_desc.height = (capabilities.y_resolution - idx);
		}
		display_write(display_dev, 0, idx, &buf_desc, buf);
	}

	buf_desc.frame_incomplete = false;
	display_blanking_off(display_dev);

	k_free(buf);
	buf = NULL;

	/* Text rendering: Zephyr version (top center, orange) + build date/time (below, white) */
	uint8_t bpp = get_bpp(capabilities.current_pixel_format);
	/* 1.5x scale: 12x24 glyphs (was 2x = 16x32) */
	int scale_num = FONT_SCALE_LARGE_NUM;
	int scale_den = FONT_SCALE_LARGE_DEN;
	uint16_t glyph_w = FONT_W * scale_num / scale_den;
	uint16_t glyph_h = FONT_H * scale_num / scale_den;
	/* Buffer sized for longest string we draw (version ~20 chars, build ~24 chars) */
	uint32_t text_buf_size = STANDBY_TEXT_MAX_LEN * glyph_w * glyph_h * bpp;
	uint8_t *text_buf = k_malloc(text_buf_size);

	if (text_buf) {
		uint16_t screen_w = capabilities.x_resolution;

		char version_str[32];
		snprintk(version_str, sizeof(version_str), "Zephyr %s", KERNEL_VERSION_STRING);

		int vlen = strlen(version_str);
		if (vlen > STANDBY_TEXT_MAX_LEN) {
			version_str[STANDBY_TEXT_MAX_LEN] = '\0';
			vlen = STANDBY_TEXT_MAX_LEN;
		}
		uint16_t version_w = vlen * glyph_w;
		display_text(display_dev, &capabilities, version_str,
			     text_center_x(screen_w, version_w), 0, COLOR_ORANGE, COLOR_BLACK,
			     text_buf, bpp, scale_num, scale_den);

		/* Build date/time: "Built: YYYY-MM-DD HH:MM" (24h) */
		char build_str[64];
		format_build_time(build_str, sizeof(build_str));
		int blen = strlen(build_str);
		if (blen > STANDBY_TEXT_MAX_LEN) {
			build_str[STANDBY_TEXT_MAX_LEN] = '\0';
			blen = STANDBY_TEXT_MAX_LEN;
		}
		uint16_t build_w = blen * FONT_W;  /* scale 1 for build line */
		display_text(display_dev, &capabilities, build_str,
			     text_center_x(screen_w, build_w), glyph_h + 8,
			     COLOR_WHITE, COLOR_BLACK, text_buf, bpp, 1, 1);

		/* Prompt user to press "BTN 2" */
		const char *prompt_str = "BUTTON 2: Start";
		uint16_t prompt_w = strlen(prompt_str) * glyph_w;
		display_text(display_dev, &capabilities, prompt_str,
			     text_center_x(screen_w, prompt_w), (glyph_h << 1)+16,
			     COLOR_YELLOW, COLOR_BLUE, text_buf, bpp, scale_num, scale_den);
	} else {
		LOG_WRN("Could not allocate text buffer");
	}

	while (1) {
		if (atomic_get(&show_camera_frame)) {
			k_msleep(200);
			continue;
		}

		k_msleep(grey_scale_sleep);
	}
}


void blink(const struct led *led, uint32_t sleep_ms, uint32_t id)
{
	const struct gpio_dt_spec *spec = &led->spec;
	static uint8_t init = 0;
	int cnt[2];
	int ret;

	if (!device_is_ready(spec->port)) {
		LOG_INF("Error: %s device is not ready", spec->port->name);
		return;
	}

	ret = gpio_pin_configure_dt(spec, GPIO_OUTPUT);
	if (ret != 0) {
		LOG_INF("Error %d: failed to configure pin %d (LED '%d')",
			ret, spec->pin, led->num);
		return;
	}

	if (!init) {
		cnt[0] = 0;
		cnt[1] = 1;
		gpio_pin_set(led0.spec.port, led0.spec.pin, cnt[0]);
		gpio_pin_set(led1.spec.port, led1.spec.pin, cnt[1]);
		k_msleep(sleep_ms);
		init++;
	}

	while (1) {
		gpio_pin_set(spec->port, spec->pin, cnt[id] % 2);

		k_msleep(sleep_ms);
		cnt[id]++;
	}
}

void blink0(void)
{   /* GRN */
	// static uint8_t init = 0;
	// if (!init) {
	// 	LOG_INF("> Led0 loop");
	// 	init++;
	// }
	blink(&led0, 125, 0);
}

void blink1(void)
{   /* RED */
	// static uint8_t init = 0;
	// if (!init) {
	// 	LOG_INF("> Led1 loop");
	// 	init++;
	// }
	blink(&led1, 125, 1);
}

int main(void)
{
	LOG_INF("===== System Information =====");
	// LOG_INF("Board: %s", CONFIG_BOARD);
	// LOG_INF("Zephyr: %s", KERNEL_VERSION_STRING);
	LOG_INF("Clock: %.2f MHz", 0.000001 * sys_clock_hw_cycles_per_sec());
	LOG_INF("Kernel ticks/sec: %u", CONFIG_SYS_CLOCK_TICKS_PER_SEC);
	LOG_INF("Build: " __DATE__ " " __TIME__);
	LOG_INF("==============================\n");

	/* Init camera sensor + video capture (DCMI) device */
	if (!device_is_ready(ov5640)) {
		LOG_INF("> OV5640 camera sensor not ready");
		return -ENODEV;
	}

	if (!device_is_ready(video_dev)) {
		LOG_INF("> Video capture device (DCMI) not ready");
		return -ENODEV;
	}

	// LOG_INF("Camera + DCMI ready");

	struct video_format fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_RGB565,
		.width = CAMERA_W,
		.height = CAMERA_H,
		.pitch = CAMERA_W * sizeof(uint16_t),
	};

	if (video_set_format(video_dev, &fmt)) {
		LOG_ERR("> Failed to set camera format");
		return -EIO;
	}

	/*
	 * Override OV5640 FORMAT CONTROL 00 (0x4300) to 0x61 (RGB565 2X8 BE) so
	 * the sensor output matches the display byte order and no per-pixel
	 * swap is needed. Done here so we don't have to patch the Zephyr driver.
	 */
	{
		const struct device *i2c_dev = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(ov5640)));
		uint8_t ov5640_addr = DT_REG_ADDR(DT_NODELABEL(ov5640));
		uint8_t fmt_ctrl[] = { 0x43, 0x00, 0x61 }; /* 0x4300 = 0x61 */

		if (device_is_ready(i2c_dev)) {
			int ret = i2c_write(i2c_dev, fmt_ctrl, sizeof(fmt_ctrl), ov5640_addr);
			if (ret != 0) 
			{
				LOG_WRN("> OV5640 0x4300 override failed: %d (colors may be wrong)", ret);
			}
			// else 
			// {
			// 	LOG_INF("> OV5640 0x4300 set to 0x61 (RGB565 BE)");
			// }
		} else {
			LOG_WRN("> OV5640 I2C bus not ready, skip 0x4300 override");
		}
	}

	/*
	 * Flip controls: video_set_ctrl skips the driver when new value equals
	 * current (default 0). Resolution params set 0x3820/0x3821, so we must
	 * force a write by setting the opposite value first.
	 */
	struct video_control ctrl;
	int want_hflip = 0, want_vflip = 0;

	ctrl.id = VIDEO_CID_HFLIP;
	ctrl.val = 1;
	video_set_ctrl(ov5640, &ctrl);
	ctrl.val = want_hflip;
	if (video_set_ctrl(ov5640, &ctrl)) {
		LOG_ERR("> Failed to set HFLIP");
	}

	ctrl.id = VIDEO_CID_VFLIP;
	ctrl.val = 1;
	video_set_ctrl(ov5640, &ctrl);
	ctrl.val = want_vflip;
	if (video_set_ctrl(ov5640, &ctrl)) {
		LOG_ERR("> Failed to set VFLIP");
	}

	LOG_INF("Camera configured: RGB565 %ux%u (hflip=%d vflip=%d)\n", CAMERA_W, CAMERA_H,
		want_hflip, want_vflip);

	/* TFLM overlay is filled by inference thread; no setup here */

	// LOG_INF("====== Threads STARTING ======");
	LOG_INF("Press Button 2 to start capture...\n");

	return 0;
}

/* Dedicated inference thread: runs TFLM once to fill overlay buffer, then exits. */
static void inference_thread(void)
{
	tflm_sine_setup();
	// while (1)
	// {
		tflm_sine_fill_overlay_buffer();
	// 	k_msleep(1000);
	// }
}

K_THREAD_DEFINE(inference_id, INFERENCE_STACKSIZE, inference_thread, NULL, NULL, NULL,
		PRIORITY_CAMERA, 0, 0);
K_THREAD_DEFINE(display_id, DEFAULT_STACKSIZE, display_thread, NULL, NULL, NULL,
		PRIORITY_CAMERA, 0, 0);
K_THREAD_DEFINE(blink0_id, DEFAULT_STACKSIZE, blink0, NULL, NULL, NULL,
		PRIORITY_LED, 0, 0);
K_THREAD_DEFINE(blink1_id, DEFAULT_STACKSIZE, blink1, NULL, NULL, NULL,
		PRIORITY_LED, 0, 0);
K_THREAD_DEFINE(camera_id, CAMERA_STACKSIZE, camera_thread, NULL, NULL, NULL,
		PRIORITY_CAMERA, 0, 0);
