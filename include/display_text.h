#ifndef DISPLAY_TEXT_H_
#define DISPLAY_TEXT_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include "display_common.h"

#define FONT_W 8
#define FONT_H 16
#define FONT_SCALE_LARGE_NUM 3
#define FONT_SCALE_LARGE_DEN 2

uint8_t get_bpp(enum display_pixel_format fmt);
uint16_t text_center_x(uint16_t screen_w, uint16_t text_w);
void display_text(const struct device *dev, const struct display_capabilities *caps,
		  const char *str, uint16_t x_pos, uint16_t y_pos, uint32_t fg_color,
		  uint32_t bg_color, uint8_t *text_buf, uint8_t bpp,
		  int scale_num, int scale_den);
void fill_display_solid(const struct device *dev,
			const struct display_capabilities *caps, uint32_t color);
const uint8_t *display_glyph_for_char(char ch);

#endif /* DISPLAY_TEXT_H_ */
