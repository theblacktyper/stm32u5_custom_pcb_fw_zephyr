# CPU Optimization Proposal: Reduce Workload and Increase Efficiency

Prioritized improvements to reduce CPU load and improve FPS.

---

## 1. Remove DCMI Double-Buffer memcpy (High Impact) ✅ DONE

**Current:** With 2 buffers, the DCMI driver copies 153,600 bytes per frame:
```c
memcpy(vbuf->buffer, dev_data->vbuf->buffer, vbuf->bytesused);
```

**Change:** Deliver the DMA buffer directly when a second buffer is available in `fifo_in`. The pipeline still works: we deliver buffer A to the app; buffer B remains in `fifo_in` for the next capture. No copy needed.

**File:** `modules/stm32u5_dcmi/zephyr/video_stm32u5_dcmi.c` – `dcmi_frame_done()`

**Effect:** Saves ~1–2 ms per frame (150 KiB memcpy on STM32U5).

**Applied:** Deliver DMA buffer directly; return the unused buffer from fifo_in back to fifo_in for the next capture. No memcpy.

---

## 2. Optimize copy_frame_to_display Scaling (High Impact)

**Current:** 320×240 → 180×135 nearest-neighbor. ~43,200 output pixels, each with:
- `sy = dy * roi_h / FRAME_DISP_H` (division per row)
- `sx = dx * roi_w / FRAME_DISP_W` (division per pixel)
- Per-pixel read and write

**Improvements:**

### 2a. Integer-step scaling (no division in inner loop)
Precompute `step_x = (roi_w << 16) / FRAME_DISP_W` and use fixed-point: `sx = (dx * step_x) >> 16`. Or precompute a row stride table.

### 2b. Use 2:1 pixel binning for a faster path
When scaling down by ~2×, use 2×2 averaging: each output pixel = average of 4 input pixels. One pass, no division in inner loop.

### 2c. Simpler scaling: 160×120 display region ✅ DONE
If acceptable: crop center 160×120 from 320×240 and display 1:1 (no scaling). 160×120 fits on 240×135. Saves all scaling work; just a memcpy of 38,400 bytes.

**Applied:** `USE_160x120_CROP=1` – center crop (80,60) from 320×240, 120 rows × memcpy(320 bytes). Set `USE_160x120_CROP` to 0 to revert to scaled 180×135.

**File:** `src/main.c` – `copy_frame_to_display()`

---

## 3. Optimize Person Overlay (Medium Impact)

**Current:** Every frame draws:
- 3-pixel border (4 sides)
- 3–4 character string (e.g. "75%")

**Improvements:**

### 3a. Only redraw overlay when score changes
Track `last_drawn_person_pct`; skip `draw_person_overlay` when unchanged. Border + text are static for many frames.

### 3b. Simplify border drawing
Use `memset` for horizontal lines and a tight loop for verticals instead of per-pixel `set_display_pixel_rgb565`.

### 3c. Reduce overlay update rate
Update overlay every 2nd or 4th frame; reuse previous frame’s overlay for display.

**File:** `src/main.c` – `draw_person_overlay()`, `copy_frame_to_display()`

---

## 4. Optimize TFLM Preprocessing (Medium Impact)

**Current:** `rgb565_to_model_input` does 96×96 = 9,216 pixels, each with:
- `rgb565_to_gray_int8`: 3 divisions (R, G, B), multiply-add for luminance

**Improvements:**

### 4a. LUT for RGB565 → grayscale
Precompute a 65536-entry LUT (or 32×64×32 for R,G,B) mapping RGB565 → int8. One lookup per pixel instead of 3 divisions + multiply.

### 4b. Shift-based luminance (no division)
Approximate: `gray ≈ ((px >> 11)*77 + ((px >> 5)&0x3f)*150 + (px&0x1f)*29) >> 8`. Use shifts: `(R << 2) + (R >> 1)` for R×5, etc. Avoid `* 255/31` division.

### 4c. Process in larger blocks
Ensure inner loop is cache-friendly; process 8 or 16 pixels at a time for potential SIMD later.

**File:** `src/tflm_hello_world/main_functions.cpp` – `rgb565_to_gray_int8()`, `rgb565_to_model_input()`

---

## 5. Display Partial Update (Medium Impact, if supported)

**Current:** `display_write(disp, 0, 0, &desc, disp_buf)` sends full 240×135×2 = 64,800 bytes.

**Change:** If the MIPI DBI / display driver supports partial updates, only write the frame region (180×135 or 160×120). Reduces SPI transfer by ~25–40%.

**Caveat:** Requires driver support for `display_write` with non-zero (x,y) and smaller width/height. May need to verify Zephyr MIPI DBI behavior.

---

## 6. Reduce Display Update Frequency (Low Impact, Trade-off)

**Option:** Update display every 2nd frame. Camera still captures every frame; we only `display_write` on alternating frames. Cuts display transfer in half but halves visible FPS.

**Use case:** Only if the goal is to maximize capture/inference rate rather than smooth display.

---

## 7. Fix Stale Comment in main_functions.cpp (Cosmetic)

**Current:** Comment says "take 128x128 from center" but model is 96×96.

**Change:** Update to "take 96×96 from center".

---

## Summary: Recommended Order

| Priority | Change                          | Est. savings | Effort |
|----------|----------------------------------|--------------|--------|
| 1        | Remove DCMI memcpy              | ~1–2 ms/frame| Low    |
| 2        | Optimize copy_frame_to_display  | ~1–3 ms/frame| Medium |
| 3        | Overlay: redraw only on change  | ~0.2–0.5 ms  | Low    |
| 4        | TFLM preprocessing (LUT/shifts) | ~0.5–1 ms    | Medium |
| 5        | Display partial update         | Variable     | Medium |

Implementing 1 and 2 first should give the largest FPS gain with reasonable effort.
