#include "display_overlay.h"

#include <stdio.h>
#include <string.h>

#include "display_text.h"

#if defined(CONFIG_WIFI_AUTOCONNECT)
#include "wifi_link.h"
#endif
#if defined(CONFIG_GOLIOTH_OTA)
#include "golioth_ui.h"
#endif

static void set_display_pixel_rgb565(uint8_t *dst, uint16_t dx, uint16_t dy,
				     uint32_t color)
{
	if (dx >= DISPLAY_W || dy >= DISPLAY_H) {
		return;
	}

	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = color & 0xFF;
	uint16_t c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
	uint32_t pixel_index = (uint32_t)dy * DISPLAY_W + dx;
	uint32_t byte_index = pixel_index * 2U;

	dst[byte_index] = (uint8_t)((c565 >> 8) & 0xFF);
	dst[byte_index + 1] = (uint8_t)(c565 & 0xFF);
}

static void draw_string_on_rgb565(uint8_t *dst, uint16_t x0, uint16_t y0,
				  const char *str, uint32_t color)
{
	for (int c = 0; str[c] != '\0'; c++) {
		const uint8_t *glyph = display_glyph_for_char(str[c]);
		if (!glyph) {
			continue;
		}
		for (int row = 0; row < FONT_H; row++) {
			uint8_t bits = glyph[row];
			for (int col = 0; col < FONT_W; col++) {
				if (bits & (0x80U >> col)) {
					uint16_t px = (uint16_t)(x0 + c * FONT_W + col);
					uint16_t py = (uint16_t)(y0 + row);
					if (px < DISPLAY_W && py < DISPLAY_H) {
						set_display_pixel_rgb565(dst, px, py, color);
					}
				}
			}
		}
	}
}

static void fill_rect_rgb565(uint8_t *dst, uint16_t x0, uint16_t y0, uint16_t w, uint16_t h,
			     uint32_t color)
{
	for (uint16_t row = 0; row < h; row++) {
		for (uint16_t col = 0; col < w; col++) {
			set_display_pixel_rgb565(dst, x0 + col, y0 + row, color);
		}
	}
}

#if defined(CONFIG_WIFI_AUTOCONNECT)
/* Signal-style bars, top-right of the scaled preview region (RGB565 buffer coordinates). */
#define WIFI_BAR_W      3U
#define WIFI_BAR_GAP    2U
#define WIFI_BAR_COUNT  4U
#define WIFI_BAR_BASE_H 4U
#define WIFI_BAR_STEP   3U
#define WIFI_BAR_MARGIN 4U

static void draw_wifi_link_bars_rgb565(uint8_t *dst)
{
	if (!wifi_link_preview_connected()) {
		return;
	}

	uint32_t fg = COLOR_GREEN;
#if defined(CONFIG_GOLIOTH_OTA)
	if (!golioth_ui_cloud_connected()) {
		fg = COLOR_WHITE;
	}
#endif
	const uint16_t total_w =
		WIFI_BAR_COUNT * WIFI_BAR_W + (WIFI_BAR_COUNT - 1U) * WIFI_BAR_GAP;
	const uint16_t max_h = WIFI_BAR_BASE_H + (WIFI_BAR_COUNT - 1U) * WIFI_BAR_STEP;

	const uint16_t x_left =
		(uint16_t)(FRAME_X_OFFSET + FRAME_DISP_W - WIFI_BAR_MARGIN - total_w);
	const uint16_t y_top = (uint16_t)(FRAME_Y_OFFSET + WIFI_BAR_MARGIN);
	const uint16_t y_bottom = (uint16_t)(y_top + max_h - 1U);

	for (unsigned i = 0; i < WIFI_BAR_COUNT; i++) {
		uint16_t h = (uint16_t)(WIFI_BAR_BASE_H + i * WIFI_BAR_STEP);
		uint16_t x = (uint16_t)(x_left + i * (WIFI_BAR_W + WIFI_BAR_GAP));
		uint16_t y = (uint16_t)(y_bottom + 1U - h);

		fill_rect_rgb565(dst, x, y, WIFI_BAR_W, h, fg);
	}
}
#endif

static int clamp_pct(int val)
{
	if (val < 0) {
		return 0;
	}
	if (val > 100) {
		return 100;
	}
	return val;
}

static void draw_person_overlay(uint8_t *dst, int8_t person_score, int8_t no_person_score,
				bool inference_enabled)
{
	if (!inference_enabled) {
		(void)dst;
		(void)person_score;
		(void)no_person_score;
		return;
	}

	int person_pct = clamp_pct((int)(person_score + 128) * 100 / 256);
	int person_at_least_half = (person_pct >= PERSON_DET_THRES_PCT);
	uint32_t bar_color = person_at_least_half ? COLOR_GREEN : COLOR_RED;

	int x0 = FRAME_X_OFFSET;
	int y0 = FRAME_Y_OFFSET;
	int x1 = FRAME_X_OFFSET + FRAME_DISP_W - 1;
	int y1 = FRAME_Y_OFFSET + FRAME_DISP_H - 1;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= DISPLAY_W) x1 = DISPLAY_W - 1;
	if (y1 >= DISPLAY_H) y1 = DISPLAY_H - 1;

	const int border_thickness = 3;

	for (int t = 0; t < border_thickness; t++) {
		int lx = x0 + t;
		int rx = x1 - t;
		if (lx > x1 || rx < x0) {
			break;
		}
		for (int y = y0; y <= y1; y++) {
			set_display_pixel_rgb565(dst, (uint16_t)lx, (uint16_t)y, bar_color);
			set_display_pixel_rgb565(dst, (uint16_t)rx, (uint16_t)y, bar_color);
		}
	}

	for (int t = 0; t < border_thickness; t++) {
		int ty = y0 + t;
		int by = y1 - t;
		if (ty > y1 || by < y0) {
			break;
		}
		for (int x = x0; x <= x1; x++) {
			set_display_pixel_rgb565(dst, (uint16_t)x, (uint16_t)ty, bar_color);
			set_display_pixel_rgb565(dst, (uint16_t)x, (uint16_t)by, bar_color);
		}
	}

	char buf[8];
	snprintf(buf, sizeof(buf), "%d%%", person_pct);
	int text_y = FRAME_Y_OFFSET - (FONT_H + 4);
	if (text_y < 0) {
		text_y = 0;
	}
	draw_string_on_rgb565(dst, (uint16_t)FRAME_X_OFFSET + 5, (uint16_t)text_y + 10,
			      buf, COLOR_YELLOW);
	(void)no_person_score;
}

void copy_frame_to_display(const uint8_t *src, uint8_t *dst,
			   int8_t person_score, int8_t no_person_score,
			   bool inference_enabled)
{
	static int borders_cleared;

	const uint16_t *s = (const uint16_t *)src;
	uint16_t *d = (uint16_t *)dst;

	if (!borders_cleared) {
		memset(dst, 0x00, DISPLAY_W * DISPLAY_H * sizeof(uint16_t));
		borders_cleared = 1;
	}

#if defined(USE_160x120_CROP) && USE_160x120_CROP && defined(CAMERA_RES_320x240)
	for (int y = 0; y < FRAME_DISP_H; y++) {
		const uint16_t *src_row = s + (FRAME_CROP_Y0 + y) * CAMERA_W + FRAME_CROP_X0;
		uint16_t *dst_row = d + (FRAME_Y_OFFSET + y) * DISPLAY_W + FRAME_X_OFFSET;
		memcpy(dst_row, src_row, FRAME_DISP_W * sizeof(uint16_t));
	}
#else
#if (CAMERA_W <= DISPLAY_W && CAMERA_H <= DISPLAY_H)
	for (int y = 0; y < CAMERA_H; y++) {
		const uint16_t *src_row = s + y * CAMERA_W;
		uint16_t *dst_row = d + (FRAME_Y_OFFSET + y) * DISPLAY_W + FRAME_X_OFFSET;
		memcpy(dst_row, src_row, CAMERA_W * sizeof(uint16_t));
	}
#else
	for (int dy = 0; dy < FRAME_DISP_H; dy++) {
		int sy = dy * CAMERA_H / FRAME_DISP_H;
		const uint16_t *src_row = s + sy * CAMERA_W;
		uint16_t *dst_row = d + (FRAME_Y_OFFSET + dy) * DISPLAY_W + FRAME_X_OFFSET;
		for (int dx = 0; dx < FRAME_DISP_W; dx++) {
			int sx = dx * CAMERA_W / FRAME_DISP_W;
			dst_row[dx] = src_row[sx];
		}
	}
#endif
#endif

	draw_person_overlay(dst, person_score, no_person_score, inference_enabled);

#if defined(CONFIG_WIFI_AUTOCONNECT)
	draw_wifi_link_bars_rgb565(dst);
#endif
}
