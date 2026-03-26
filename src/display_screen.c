#include "display_screen.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "app_version.h"
#include "build_version.h"
#include "display_text.h"

#define STANDBY_TEXT_MAX_LEN 24

static void format_build_time(char *dst, size_t size)
{
	snprintk(dst, size, "Built: %s", BUILD_DATE_TIME);
}

int display_screen_prepare(const struct device *display_dev,
			   struct display_capabilities *capabilities,
			   struct display_screen_ctx *ctx)
{
	size_t rect_w;
	size_t rect_h;
	size_t h_step;
	size_t scale;
	uint8_t *buf;
	struct display_buffer_descriptor buf_desc;
	size_t buf_size = 0;

	display_get_capabilities(display_dev, capabilities);

	if (capabilities->screen_info & SCREEN_INFO_MONO_VTILED) {
		rect_w = 16;
		rect_h = 8;
	} else {
		rect_w = 2;
		rect_h = 1;
	}

	if ((capabilities->x_resolution < 3 * rect_w) ||
	    (capabilities->y_resolution < 3 * rect_h) ||
	    (capabilities->x_resolution < 8 * rect_h)) {
		rect_w = capabilities->x_resolution * 40 / 100;
		rect_h = capabilities->y_resolution * 40 / 100;
		h_step = capabilities->y_resolution * 20 / 100;
		scale = 1;
	} else {
		h_step = rect_h;
		scale = (capabilities->x_resolution / 8) / rect_h;
	}

	rect_w *= scale;
	rect_h *= scale;
	ctx->grey_scale_sleep = (capabilities->screen_info & SCREEN_INFO_EPD) ? 10000 : 100;

	if (capabilities->screen_info & SCREEN_INFO_X_ALIGNMENT_WIDTH) {
		rect_w = capabilities->x_resolution;
	}

	buf_size = rect_w * rect_h;
	if (buf_size < (capabilities->x_resolution * h_step)) {
		buf_size = capabilities->x_resolution * h_step;
	}

	switch (capabilities->current_pixel_format) {
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
		buf_size = DIV_ROUND_UP(DIV_ROUND_UP(buf_size, NUM_BITS(uint8_t)), sizeof(uint8_t));
		break;
	default:
		return -ENOTSUP;
	}

	buf = k_malloc(buf_size);
	if (!buf) {
		return -ENOMEM;
	}
	(void)memset(buf, 0, buf_size);

	buf_desc.buf_size = buf_size;
	buf_desc.pitch = capabilities->x_resolution;
	buf_desc.width = capabilities->x_resolution;
	buf_desc.height = h_step;
	buf_desc.frame_incomplete = true;
	for (int idx = 0; idx < capabilities->y_resolution; idx += h_step) {
		if ((capabilities->y_resolution - idx) < h_step) {
			buf_desc.height = (capabilities->y_resolution - idx);
		}
		display_write(display_dev, 0, idx, &buf_desc, buf);
	}
	buf_desc.frame_incomplete = false;
	display_blanking_off(display_dev);
	k_free(buf);

	ctx->bpp = get_bpp(capabilities->current_pixel_format);
	ctx->scale_num = FONT_SCALE_LARGE_NUM;
	ctx->scale_den = FONT_SCALE_LARGE_DEN;
	ctx->glyph_w = FONT_W * ctx->scale_num / ctx->scale_den;
	ctx->glyph_h = FONT_H * ctx->scale_num / ctx->scale_den;

	uint32_t text_buf_size = STANDBY_TEXT_MAX_LEN * ctx->glyph_w * ctx->glyph_h * ctx->bpp;
	ctx->text_buf = k_malloc(text_buf_size);
	return (ctx->text_buf == NULL) ? -ENOMEM : 0;
}

int display_screen_render_boot(const struct device *display_dev,
			       const struct display_capabilities *capabilities,
			       const struct display_screen_ctx *ctx)
{
	if (!ctx->text_buf) {
		return -ENOMEM;
	}

	uint16_t screen_w = capabilities->x_resolution;
	char version_str[32];
	snprintk(version_str, sizeof(version_str), "App v%s", APP_VERSION);
	int vlen = strlen(version_str);
	if (vlen > STANDBY_TEXT_MAX_LEN) {
		version_str[STANDBY_TEXT_MAX_LEN] = '\0';
		vlen = STANDBY_TEXT_MAX_LEN;
	}
	uint16_t version_w = vlen * ctx->glyph_w;
	display_text(display_dev, capabilities, version_str, text_center_x(screen_w, version_w), 0,
		     COLOR_ORANGE, COLOR_BLACK, ctx->text_buf, ctx->bpp, ctx->scale_num, ctx->scale_den);

	char build_str[64];
	format_build_time(build_str, sizeof(build_str));
	int blen = strlen(build_str);
	if (blen > STANDBY_TEXT_MAX_LEN) {
		build_str[STANDBY_TEXT_MAX_LEN] = '\0';
		blen = STANDBY_TEXT_MAX_LEN;
	}
	uint16_t build_w = blen * FONT_W;
	display_text(display_dev, capabilities, build_str, text_center_x(screen_w, build_w), ctx->glyph_h + 8,
		     COLOR_WHITE, COLOR_BLACK, ctx->text_buf, ctx->bpp, 1, 1);

	const char *prompt_str = "Button2: Start";
	uint16_t prompt_w = strlen(prompt_str) * ctx->glyph_w;
	display_text(display_dev, capabilities, prompt_str,
		     text_center_x(screen_w, prompt_w), (ctx->glyph_h << 1) + 16,
		     COLOR_YELLOW, COLOR_BLUE, ctx->text_buf, ctx->bpp, ctx->scale_num, ctx->scale_den);
	return 0;
}

void display_screen_render_dfu_mode(const struct device *display_dev,
				    const struct display_capabilities *capabilities,
				    const struct display_screen_ctx *ctx)
{
	fill_display_solid(display_dev, capabilities, COLOR_GRAY);
	if (ctx->text_buf) {
		display_text(display_dev, capabilities, "DFU mode",
			     text_center_x(capabilities->x_resolution, strlen("DFU mode") * ctx->glyph_w),
			     capabilities->y_resolution / 2 - ctx->glyph_h / 2,
			     COLOR_RED, COLOR_GRAY, ctx->text_buf, ctx->bpp,
			     ctx->scale_num, ctx->scale_den);
	}
}

void display_screen_render_updating(const struct device *display_dev,
				    const struct display_capabilities *capabilities,
				    const struct display_screen_ctx *ctx)
{
	fill_display_solid(display_dev, capabilities, COLOR_GRAY);
	if (ctx->text_buf) {
		display_text(display_dev, capabilities, "Updating FW...",
			     text_center_x(capabilities->x_resolution, strlen("Updating FW...") * ctx->glyph_w),
			     capabilities->y_resolution / 2 - ctx->glyph_h / 2,
			     COLOR_RED, COLOR_GRAY, ctx->text_buf, ctx->bpp,
			     ctx->scale_num, ctx->scale_den);
	}
}
