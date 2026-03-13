# RAM Study: Double-Buffering for Video Capture

## Parameters

| Parameter | Value |
|-----------|-------|
| Frame size | 320 × 240 × 2 = **153,600 bytes** (~150 KiB) |
| SRAM total | **768 KiB** |

---

## Current Allocation (Single Buffer)

| Component | Size | Location |
|-----------|------|----------|
| Camera frame buffer | 150 KiB | Static (`.bss`) |
| TFLM tensor arena | 240 KiB | Static (`.bss`) |
| Display buffer | ~63 KiB | Heap (`k_malloc`) |
| Heap pool | 192 KiB | `CONFIG_HEAP_MEM_POOL_SIZE` |
| Thread stacks | ~14 KiB | Static |
| Kernel + app BSS | ~50–80 KiB | Static |

**Estimated static (camera + tensor):** 390 KiB  
**Heap:** 192 KiB  
**Rough total:** ~600 KiB (leaves ~168 KiB margin)

---

## Double-Buffer Requirement

| Change | Extra RAM |
|--------|-----------|
| Second camera buffer | **150 KiB** |

**Total camera buffers:** 2 × 150 KiB = **300 KiB**

---

## Feasibility Analysis

### Option A: Add Second Static Buffer (No Other Changes)

- **New static:** 390 + 150 = **540 KiB**
- **Heap:** 192 KiB (unchanged)
- **Estimated total:** 540 + 192 + ~70 = **~802 KiB**

**Verdict:** Exceeds 768 KiB; not feasible without other changes.

---

### Option B: Add Second Buffer + Reduce Tensor Arena

The official 96×96 model is smaller than the 128×128 model. TFLM examples often use ~120 KiB for person_detect.

- **Tensor arena:** 240 → **120 KiB** (saves 120 KiB)
- **Camera buffers:** 300 KiB
- **New static:** 300 + 120 = **420 KiB**
- **Heap:** 192 KiB
- **Estimated total:** 420 + 192 + ~70 = **~682 KiB**

**Verdict:** Fits within 768 KiB.

**Risk:** 120 KiB arena may be too small; needs verification with the 96×96 model.

---

### Option C: Add Second Buffer + Reduce Heap

- **Heap:** 192 → **64 KiB** (saves 128 KiB)
- **Camera buffers:** 300 KiB
- **New static:** 300 + 240 = **540 KiB**
- **Estimated total:** 540 + 64 + ~70 = **~674 KiB**

**Verdict:** Fits within 768 KiB.

**Risk:** 64 KiB heap must cover `disp_buf` (~63 KiB) and any other allocations; very little margin.

---

### Option D: Add Second Buffer + Reduce Both

- **Tensor arena:** 240 → **140 KiB** (saves 100 KiB)
- **Heap:** 192 → **100 KiB** (saves 92 KiB)
- **Camera buffers:** 300 KiB
- **New static:** 300 + 140 = **440 KiB**
- **Estimated total:** 440 + 100 + ~70 = **~610 KiB**

**Verdict:** Fits with comfortable margin.

---

## Recommendation

**Option B** is the most straightforward: reduce the tensor arena and add a second static camera buffer.

1. **Verify arena size:** Build with `kTensorArenaSize = 120 * 1024` and run; if `AllocateTensors()` fails, increase (e.g. 140 KiB).
2. **Implement double buffer:** Add a second static buffer and set `CONFIG_VIDEO_BUFFER_POOL_NUM_MAX=2`, or keep using static buffers with `NUM_MAX=2` and two static `video_buffer` structs.
3. **Update DCMI driver:** Ensure it supports two-buffer operation (it already does when using the pool).

**Next step:** Try Option B with 120 KiB arena. If allocation fails, increase to 140 KiB and re-test.

---

## Implementation Status (Done)

- **Tensor arena:** Reduced to 120 KiB in `main_functions.cpp`
- **Double-buffer:** Added `camera_frame_buf1` and `camera_static_vbuf1`; static buffers used for both `NUM_MAX=1` and `NUM_MAX=2`
- **prj.conf:** `CONFIG_VIDEO_BUFFER_POOL_NUM_MAX=2`
- **Camera thread:** With 2 buffers, no semaphore wait before capture; pipelining allows next capture to start while inference processes the previous frame
- **DCMI driver:** Already supports two-buffer operation (copy from DMA buffer to second buffer, deliver to app)
