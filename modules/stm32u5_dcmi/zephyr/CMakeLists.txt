# Build the STM32U5 DCMI driver when enabled (board uses st,stm32u5-dcmi and
# CONFIG_VIDEO_STM32_DCMI=n).
if(CONFIG_VIDEO_STM32U5_DCMI)
  zephyr_library()
  # video_device.h is a private Zephyr header in drivers/video
  zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/video)
  zephyr_library_sources(video_stm32u5_dcmi.c)
endif()
