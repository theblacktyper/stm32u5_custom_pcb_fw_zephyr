# Application CMake config: run before Zephyr module discovery so the
# out-of-tree STM32U5 DCMI module is always found (required for st,stm32u5-dcmi).
set(_stm32u5_dcmi "${APPLICATION_SOURCE_DIR}/modules/stm32u5_dcmi")

# So the module is built and linked
if(ZEPHYR_EXTRA_MODULES)
  list(FIND ZEPHYR_EXTRA_MODULES "${_stm32u5_dcmi}" _idx)
  if(_idx LESS 0)
    list(APPEND ZEPHYR_EXTRA_MODULES "${_stm32u5_dcmi}")
    set(ZEPHYR_EXTRA_MODULES "${ZEPHYR_EXTRA_MODULES}" CACHE STRING "Extra Zephyr modules" FORCE)
  endif()
else()
  set(ZEPHYR_EXTRA_MODULES "${_stm32u5_dcmi}" CACHE STRING "Extra Zephyr modules" FORCE)
endif()

# So devicetree finds st,stm32u5-dcmi binding (DT_HAS_ST_STM32U5_DCMI_ENABLED=y)
list(APPEND DTS_ROOT "${_stm32u5_dcmi}")
