#include "camera_pipeline.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(camera_pipeline, LOG_LEVEL_INF);

const struct device *ov5640 = DEVICE_DT_GET(DT_NODELABEL(ov5640));
const struct device *video_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

int camera_pipeline_configure(void)
{
	/* Init camera sensor + video capture (DCMI) device */
	if (!device_is_ready(ov5640)) {
		LOG_INF("> OV5640 camera sensor not ready");
		return -ENODEV;
	}

	if (!device_is_ready(video_dev)) {
		LOG_INF("> Video capture device (DCMI) not ready");
		return -ENODEV;
	}

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
			if (ret != 0) {
				LOG_WRN("> OV5640 0x4300 override failed: %d (colors may be wrong)", ret);
			}
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
	int want_hflip = 0;
	int want_vflip = 0;

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

	/* Enable ISP scaler before capture starts. */
	camera_enable_scaler();

	LOG_INF("Camera configured: RGB565 %ux%u (hflip=%d vflip=%d)\n", CAMERA_W, CAMERA_H,
		want_hflip, want_vflip);

	return 0;
}

void camera_enable_scaler(void)
{
	const struct device *i2c_dev = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(ov5640)));
	const uint8_t ov5640_addr = DT_REG_ADDR(DT_NODELABEL(ov5640));
	uint8_t addr_buf[2] = { 0x50, 0x01 };
	uint8_t v;

	if (!device_is_ready(i2c_dev)) {
		return;
	}

	if (i2c_write_read(i2c_dev, ov5640_addr, addr_buf, sizeof(addr_buf), &v, 1) != 0) {
		return;
	}

	v |= BIT(5);
	uint8_t buf[3] = { 0x50, 0x01, v };
	(void)i2c_write(i2c_dev, buf, sizeof(buf), ov5640_addr);
}
