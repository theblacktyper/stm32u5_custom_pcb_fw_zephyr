#include "display_screen.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "app_version.h"
#include "build_version.h"
#include "display_text.h"
#if defined(CONFIG_GOLIOTH_OTA)
#include "golioth_ui.h"
#include "ota_download_ui.h"
#endif

#define STANDBY_TEXT_MAX_LEN 24

/* Avoid full-screen gray refresh every tick: paint static updating UI once, then only the bar band. */
#define UPDATING_PCT_SENTINEL 101U

static unsigned s_updating_last_pct = UPDATING_PCT_SENTINEL;
static bool s_updating_painted;
#if defined(CONFIG_GOLIOTH_OTA)
/* Tracks last drawn "Applying Update" vs "Updating Firmware" for partial refresh paths. */
static bool s_updating_title_applying;
#endif

void display_screen_updating_invalidate(void)
{
	s_updating_last_pct = UPDATING_PCT_SENTINEL;
	s_updating_painted = false;
#if defined(CONFIG_GOLIOTH_OTA)
	s_updating_title_applying = false;
#endif
}

/*
 * Erase the full title row before drawing a new title string. Required when the new string is
 * shorter than the old one — display_text only writes the new width; leftover pixels must not
 * remain visible.
 */
static void updating_clear_title_band(const struct device *display_dev,
				      const struct display_capabilities *capabilities,
				      uint16_t screen_w, uint16_t y_title,
				      const struct display_screen_ctx *ctx)
{
	display_solid_rect(display_dev, capabilities, 0, y_title, screen_w, ctx->glyph_h, COLOR_GRAY);
}

#if defined(CONFIG_GOLIOTH_OTA)
/* Gap between bar bottom and centered % text (bar_y unchanged vs. previous layout). */
#define UPDATING_PCT_BELOW_GAP 4U

struct updating_bar_layout {
	uint16_t y_title;
	uint16_t bar_x;
	uint16_t bar_y;
	uint16_t bar_w;
	uint16_t bar_h;
	uint16_t pct_y;
	bool valid;
};

static void updating_compute_bar_layout(uint16_t screen_w, uint16_t screen_h,
					const struct display_screen_ctx *ctx,
					struct updating_bar_layout *out)
{
	const uint16_t bar_h = 12U;
	const uint16_t block_gap = 8U;
	const uint16_t margin = 8U;
	const uint16_t block_h = (uint16_t)(ctx->glyph_h + block_gap + bar_h);

	out->y_title = (screen_h > block_h) ? (uint16_t)((screen_h - block_h) / 2U) : 0U;
	out->bar_h = bar_h;
	out->bar_w = (uint16_t)(screen_w - 2U * margin);
	out->bar_x = (uint16_t)((screen_w > out->bar_w) ? (screen_w - out->bar_w) / 2U : 0U);
	out->bar_y = (uint16_t)(out->y_title + ctx->glyph_h + block_gap);
	out->pct_y = (uint16_t)(out->bar_y + out->bar_h + UPDATING_PCT_BELOW_GAP);
	out->valid = (out->bar_w > 4U) &&
		     ((uint32_t)out->pct_y + FONT_H <= screen_h);
}

static void updating_draw_progress_only(const struct device *display_dev,
					const struct display_capabilities *capabilities,
					const struct display_screen_ctx *ctx,
					const struct updating_bar_layout *layout, unsigned pct)
{
	const uint16_t screen_w = capabilities->x_resolution;
	const uint16_t fill_w = (uint16_t)(((uint32_t)pct * (uint32_t)layout->bar_w) / 100U);
	char pct_str[8];

	snprintk(pct_str, sizeof(pct_str), "%u%%", pct);

	const uint16_t text_w = (uint16_t)(strlen(pct_str) * FONT_W);
	const uint16_t pct_x = text_center_x(screen_w, text_w);

	const uint16_t bar_r = (uint16_t)(layout->bar_x + layout->bar_w);
	const uint16_t text_r = (uint16_t)(pct_x + text_w);
	const uint16_t box_left = (layout->bar_x < pct_x) ? layout->bar_x : pct_x;
	const uint16_t box_right = (bar_r > text_r) ? bar_r : text_r;
	const uint16_t box_w = (uint16_t)(box_right - box_left);
	const uint16_t box_y = layout->bar_y;
	const uint16_t text_bottom = (uint16_t)(layout->pct_y + FONT_H);
	const uint16_t bar_bottom = (uint16_t)(layout->bar_y + layout->bar_h);
	const uint16_t box_bottom = (text_bottom > bar_bottom) ? text_bottom : bar_bottom;
	const uint16_t box_h = (uint16_t)(box_bottom - box_y);

	display_solid_rect(display_dev, capabilities, box_left, box_y, box_w, box_h, COLOR_GRAY);
	display_solid_rect(display_dev, capabilities, layout->bar_x, layout->bar_y, layout->bar_w,
			   layout->bar_h, COLOR_BLACK);
	if (fill_w > 0U) {
		display_solid_rect(display_dev, capabilities, layout->bar_x, layout->bar_y, fill_w,
				   layout->bar_h, COLOR_GREEN);
	}

	display_text(display_dev, capabilities, pct_str, pct_x, layout->pct_y, COLOR_WHITE,
		     COLOR_GRAY, ctx->text_buf, ctx->bpp, 1, 1);
}
#endif /* CONFIG_GOLIOTH_OTA */

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

#if defined(CONFIG_GOLIOTH_OTA)
	struct golioth_ui_standby_line line;

	golioth_ui_standby_get_line(&line);
#else
	struct {
		const char *text;
		uint32_t fg;
		uint32_t bg;
	} line = { "Firmware up-to-date", COLOR_YELLOW, COLOR_BLACK };
#endif
	uint16_t prompt_w = strlen(line.text) * ctx->glyph_w;
	display_text(display_dev, capabilities, line.text,
		     text_center_x(screen_w, prompt_w), (ctx->glyph_h << 1) + 16,
		     line.fg, line.bg, ctx->text_buf, ctx->bpp, ctx->scale_num, ctx->scale_den);
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
	const uint16_t screen_w = capabilities->x_resolution;
	const uint16_t screen_h = capabilities->y_resolution;

	if (!ctx->text_buf) {
		return;
	}

	uint16_t y_title;

#if defined(CONFIG_GOLIOTH_OTA)
	const char *title = ota_download_ui_is_applying() ? "Applying Update" : "Updating Firmware";
	const uint16_t title_w = (uint16_t)(strlen(title) * ctx->glyph_w);
	struct updating_bar_layout layout;
	bool has_bar = false;

	if (ota_download_ui_has_total()) {
		updating_compute_bar_layout(screen_w, screen_h, ctx, &layout);
		has_bar = layout.valid;
		if (has_bar) {
			y_title = layout.y_title;
		} else {
			y_title = (uint16_t)(screen_h / 2U - ctx->glyph_h / 2U);
		}
	} else {
		y_title = (uint16_t)(screen_h / 2U - ctx->glyph_h / 2U);
	}

	const unsigned pct = has_bar ? ota_download_ui_percent() : 0U;

	/* Title-only (e.g. MCUmgr): one full paint, then idle. */
	if (!has_bar) {
		if (s_updating_painted) {
			return;
		}
		fill_display_solid(display_dev, capabilities, COLOR_GRAY);
		updating_clear_title_band(display_dev, capabilities, screen_w, y_title, ctx);
		display_text(display_dev, capabilities, title,
			     text_center_x(screen_w, title_w), y_title, COLOR_RED, COLOR_GRAY,
			     ctx->text_buf, ctx->bpp, ctx->scale_num, ctx->scale_den);
		s_updating_painted = true;
		s_updating_last_pct = UPDATING_PCT_SENTINEL;
		s_updating_title_applying = ota_download_ui_is_applying();
		return;
	}

	/* New download or first paint with bar: full gray + static + bar. */
	if (!s_updating_painted || pct < s_updating_last_pct) {
		fill_display_solid(display_dev, capabilities, COLOR_GRAY);
		updating_clear_title_band(display_dev, capabilities, screen_w, y_title, ctx);
		display_text(display_dev, capabilities, title,
			     text_center_x(screen_w, title_w), y_title, COLOR_RED, COLOR_GRAY,
			     ctx->text_buf, ctx->bpp, ctx->scale_num, ctx->scale_den);
		updating_draw_progress_only(display_dev, capabilities, ctx, &layout, pct);
		s_updating_painted = true;
		s_updating_last_pct = pct;
		s_updating_title_applying = ota_download_ui_is_applying();
		return;
	}

	/* Same % as last frame; skip redraw unless title text changed (e.g. shorter string). */
	if (pct == s_updating_last_pct) {
		if (ota_download_ui_is_applying() == s_updating_title_applying) {
			return;
		}
		updating_clear_title_band(display_dev, capabilities, screen_w, y_title, ctx);
		display_text(display_dev, capabilities, title,
			     text_center_x(screen_w, title_w), y_title, COLOR_RED, COLOR_GRAY,
			     ctx->text_buf, ctx->bpp, ctx->scale_num, ctx->scale_den);
		s_updating_title_applying = ota_download_ui_is_applying();
		return;
	}

	updating_draw_progress_only(display_dev, capabilities, ctx, &layout, pct);
	s_updating_last_pct = pct;
#else
	const uint16_t title_w = (uint16_t)(strlen("Updating Firmware") * ctx->glyph_w);

	y_title = (uint16_t)(screen_h / 2U - ctx->glyph_h / 2U);

	if (s_updating_painted) {
		return;
	}
	fill_display_solid(display_dev, capabilities, COLOR_GRAY);
	updating_clear_title_band(display_dev, capabilities, screen_w, y_title, ctx);
	display_text(display_dev, capabilities, "Updating Firmware",
		     text_center_x(screen_w, title_w), y_title, COLOR_RED, COLOR_GRAY, ctx->text_buf,
		     ctx->bpp, ctx->scale_num, ctx->scale_den);
	s_updating_painted = true;
#endif
}
