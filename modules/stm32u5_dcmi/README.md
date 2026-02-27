# STM32U5 DCMI (GPDMA) out-of-tree driver

This module provides an STM32U5 DCMI driver that uses GPDMA. It lets you keep the in-tree `drivers/video/video_stm32_dcmi.c` in the Zephyr repo **unmodified** while still using the U5-specific DMA init and behavior.

---

## Walkthrough: use this driver instead of the in-tree one

### 1. Register the module so Zephyr builds it

You must tell the build about this module.

**Option A – Extra module (no `west.yml` in app)**  
Build with:

```bash
west build -d build -b kkong/edgeaiproto -- -DZEPHYR_EXTRA_MODULES=/path/to/kk_edge_ai_tflm_hello/modules/stm32u5_dcmi
```

Use the real path to your app (e.g. `c:/zephyr_workspace/kk_edge_ai_tflm_hello` on Windows).

**Option B – `west.yml` in the app (if the app repo is the west “project”)**

If you have (or add) a `west.yml` in the application root and the app is the main project, add the module there:

```yaml
manifest:
  version: "0.7"
  self:
    path: .
    modules:
      - modules/stm32u5_dcmi
  projects: []   # or your existing projects (e.g. zephyr)
```

Then run `west update` and build as usual; the module will be picked up automatically.

---

### 2. Overlay: use the new compatible (in Zephyr repo)

In your **board overlay** (e.g. `C:\repos\zephyr\zephyr\boards\kkong\edgeaiproto\edgeaiproto.overlay`), change the DCMI node so this driver binds instead of the in-tree one.

Find the `dcmi` node and set its compatible to `st,stm32u5-dcmi`:

**Before:**

```dts
dcmi: dcmi@4202c000 {
    compatible = "st,stm32-dcmi";
    ...
};
```

**After:**

```dts
dcmi: dcmi@4202c000 {
    compatible = "st,stm32u5-dcmi";
    ...
};
```

Leave all other properties (reg, interrupts, clocks, dmas, pinctrl, port, etc.) unchanged.

---

### 3. Board Kconfig: disable the in-tree DCMI driver (in Zephyr repo)

So that the in-tree `video_stm32_dcmi.c` is not compiled (and you don’t get duplicate or wrong driver), disable it for this board.

In the **board** directory (e.g. `C:\repos\zephyr\zephyr\boards\kkong\edgeaiproto\`), add or edit `Kconfig.defconfig` and set:

```
CONFIG_VIDEO_STM32_DCMI=n
```

If the file doesn’t exist, create it with that line. If it exists, append the line (and keep any existing `CONFIG_*` entries).

---

### 4. Revert the in-tree driver (in Zephyr repo)

Restore `C:\repos\zephyr\zephyr\drivers\video\video_stm32_dcmi.c` to the **upstream** version (remove your local GPDMA / `CONFIG_DMA_STM32U5` changes). This driver is no longer used for your board; the out-of-tree one is.

---

### 5. Build and test

- Build the app for your board (with the overlay and, if used, `ZEPHYR_EXTRA_MODULES` or `west.yml` as above).
- Confirm that DCMI/camera still works. The out-of-tree driver provides the same API; only the compatible and DMA init differ.

---

## Summary

| Where | What |
|-------|------|
| **App** | Module under `modules/stm32u5_dcmi/` (already added). Register via `ZEPHYR_EXTRA_MODULES` or `west.yml` `modules`. |
| **Zephyr – overlay** | In the DCMI node, set `compatible = "st,stm32u5-dcmi";`. |
| **Zephyr – board** | In `boards/kkong/edgeaiproto/Kconfig.defconfig`, set `CONFIG_VIDEO_STM32_DCMI=n`. |
| **Zephyr – driver** | Revert `drivers/video/video_stm32_dcmi.c` to upstream. |

After this, the only place that contains the U5/GPDMA logic is this module; the Zephyr repo stays clean.

---

## Zephyr DMA driver: `hal_override` in `dma_stm32u5.c`

### Is it still required?

**Yes.** This driver uses **HAL-managed GPDMA**: it sets `dma_cfg.linked_channel = STM32_DMA_HAL_OVERRIDE` and programs the DMA via the STM32 HAL (e.g. `HAL_DCMI_Start_DMA`). The in-tree STM32U5 DMA driver (`C:\repos\zephyr\zephyr\drivers\dma\dma_stm32u5.c`) must support that mode:

- **Lines 383–392** – When `linked_channel == STM32_DMA_HAL_OVERRIDE`, the DMA driver only registers the callback and sets `stream->hal_override = true`; HAL does the actual channel programming.
- **Lines 286–288** – In the IRQ handler, when neither TC nor HT is active but `hal_override` is true, the handler still calls the callback with `DMA_STATUS_COMPLETE` (HAL may have already cleared the flags).
- **Lines 674–677** – On channel disable, HAL-overridden streams are just marked not busy; the driver does not touch LL registers.

Without this logic, the DCMI completion callback would never run and captures would hang.

### Can we externalize it like DCMI?

**No, not in a practical way.**

- The `hal_override` behavior is **inside** the in-tree STM32U5 DMA driver (IRQ handler, configure, disable). It is not a separate driver.
- Externalizing would mean either:
  - **Duplicating** the whole `dma_stm32u5.c` (800+ lines) in this module and maintaining a fork, plus using a different devicetree compatible for GPDMA so the board uses “our” DMA driver. That would be high maintenance and brittle.
  - **Patching** the in-tree driver at build time, which Zephyr does not support in a standard way.
- The same pattern already exists in mainline Zephyr for other STM32 DMA drivers (`dma_stm32.c`, `dma_stm32_bdma.c`) and is used by in-tree drivers (DCMI, I2S, flash, SDMMC). So this is the intended way to support HAL-managed DMA.

### Recommendation

- **Keep** the `hal_override` handling in the Zephyr repo’s `drivers/dma/dma_stm32u5.c`.
- If that code is not yet upstream, **submit it** to the Zephyr project so it becomes part of mainline and you don’t carry a local patch.
- **Document** for your team that this app (and this DCMI module) depend on that DMA driver behavior; no need to change or move it.
