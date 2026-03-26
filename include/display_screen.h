#ifndef DISPLAY_SCREEN_H_
#define DISPLAY_SCREEN_H_

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

struct display_screen_ctx {
	uint8_t *text_buf;
	uint8_t bpp;
	uint16_t glyph_w;
	uint16_t glyph_h;
	int scale_num;
	int scale_den;
	int32_t grey_scale_sleep;
};

int display_screen_prepare(const struct device *display_dev,
			   struct display_capabilities *capabilities,
			   struct display_screen_ctx *ctx);
int display_screen_render_boot(const struct device *display_dev,
			       const struct display_capabilities *capabilities,
			       const struct display_screen_ctx *ctx);
void display_screen_render_dfu_mode(const struct device *display_dev,
				    const struct display_capabilities *capabilities,
				    const struct display_screen_ctx *ctx);
void display_screen_render_updating(const struct device *display_dev,
				    const struct display_capabilities *capabilities,
				    const struct display_screen_ctx *ctx);

#endif /* DISPLAY_SCREEN_H_ */
