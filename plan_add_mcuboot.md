# Plan: Add MCUBoot Bootloader to kk_edge_ai_tflm_hello

## Context

The project is a Zephyr RTOS TFLite Micro application running on STM32U585 (custom "edgeaiproto" board). Flash partitions for MCUBoot are already defined in the device tree (boot_partition, slot0, slot1, storage), and `zephyr,code-partition` already points to slot0. However, no MCUBoot build infrastructure exists — no sysbuild config, no signing, no bootloader-aware app config. This plan adds MCUBoot capability using Zephyr's sysbuild approach.

## Key Decisions

- **Sysbuild** (not child_image) — modern Zephyr 4.x approach, auto-builds MCUBoot alongside the app
- **Overwrite-only mode** — simplest to bring up; no scratch partition needed (none defined). Can upgrade to swap-using-move later for rollback support
- **ECDSA-P256 signing** — compact signatures, good Cortex-M33 performance, MCUBoot ships with a dev key
- **No DTS changes needed** — existing partitions are already MCUBoot-compatible (64KB boot, 448KB x2 slots, 64KB storage)

## Files to Create

### 1. `sysbuild.conf` (project root)
```ini
SB_CONFIG_BOOTLOADER_MCUBOOT=y
SB_CONFIG_MCUBOOT_MODE_OVERWRITE_ONLY=y
SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y
```

### 2. `sysbuild/mcuboot.conf`
MCUBoot-specific Kconfig overrides:
```ini
CONFIG_MCUBOOT_LOG_LEVEL_WRN=y
CONFIG_MCUBOOT_DOWNGRADE_PREVENTION=y
CONFIG_BOOT_MAX_IMG_SECTORS=56
CONFIG_FLASH=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```
- 56 sectors = 448KB / 8KB (STM32U585 erase page size)

### 3. `.gitignore` (project root)
```
keys/*.pem
```

## Files to Modify

### 4. `prj.conf` — append MCUBoot app-side configs
```ini
# MCUBoot bootloader support
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_STREAM_FLASH=y
CONFIG_IMG_MANAGER=y
```
- `CONFIG_BOOTLOADER_MCUBOOT=y` adds the 0x200 image header offset to the linker
- Flash/image manager configs enable future DFU support

## No Changes Needed
- `CMakeLists.txt` / `ZephyrAppConfig.cmake` — DCMI module registration only affects app image, not MCUBoot
- `edgeaiproto.dts` — partitions already correct
- `edgeaiproto_defconfig` / `board.cmake` — no bootloader-specific board changes needed

## Build & Flash

**Build command** (adds `--sysbuild`):
```bash
west build --sysbuild -b edgeaiproto C:\zephyr_workspace\kk_edge_ai_tflm_hello
```

**Outputs:**
- `build/mcuboot/zephyr/zephyr.bin` — bootloader (~30-40KB, must be < 64KB)
- `build/kk_edge_ai_tflm_hello/zephyr/zephyr.signed.bin` — signed app (must be < 448KB)
- `build/merged.hex` — combined image for initial programming

**Flash (initial, both images):**
```bash
west flash --runner stm32cubeprogrammer
```

**Flash (manual, if west doesn't pick merged.hex):**
```bash
STM32_Programmer_CLI -c port=SWD reset=HW -e all -d build/merged.hex -v -hardRst
```

## Verification

1. **Check image sizes** — MCUBoot < 64KB, signed app < 448KB
2. **Serial console (115200)** — should show MCUBoot banner then Zephyr boot:
   ```
   *** Booting MCUboot vX.Y.Z ***
   [INF] MCUBoot: primary slot: version=0.0.0+0
   *** Booting Zephyr OS ***
   ```
3. **App functionality** — camera, display, TFLM inference should all work identically
4. **Signing verification** — flashing an unsigned image to slot0 should cause MCUBoot to reject it

## Risk: Image Size

The TFLite model alone is ~294KB. If the signed app exceeds 448KB, expand the partition layout in `edgeaiproto.dts`:
```
boot:  64KB @ 0x00000000
slot0: 896KB @ 0x00010000
slot1: 896KB @ 0x000F0000
storage: 64KB @ 0x001D0000
```

## Future Enhancements (out of scope)
- **UART DFU via MCUmgr** — upload signed images over UART to slot1
- **Swap-using-move mode** — enables rollback on failed updates
- **Custom signing key** — generate with `imgtool keygen -k keys/edgeaiproto-ec-p256.pem -t ecdsa-p256`
