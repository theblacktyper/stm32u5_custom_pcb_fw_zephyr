# kk_edge_ai_tflm_hello

Camera + display app with a **TensorFlow Lite Micro (TFLM) hello_world** sine model overlay on the camera frame.

## TFLM sine overlay

The overlay is drawn using the Zephyr **hello_world** tflite-micro model: for each x in `[0, 2π]`, the app runs TFLM inference and draws the predicted y (sine) on the camera image in green.

## Building (with TFLM)

The app requires the **tflite-micro** Zephyr module. From your Zephyr workspace (parent of this app), run:

```bash
west config manifest.project-filter -- +tflite-micro
west update
```

Then build from this directory or pass it to `west build`:

```bash
west build -b <your_board> -- -DBOARD_ROOT=$(pwd)
# or, if this app is under your workspace:
west build -b <your_board> kk_edge_ai_tflm_hello
```

## Board requirements

- Display (chosen via `zephyr,display`)
- Camera (OV5640 + DCMI, chosen via `zephyr,camera`)
- Button (sw0) to start capture

Press Button 2 to start camera capture; the TFLM sine wave is overlaid on each frame.

## Logging (dedicated thread)

Logging runs in **deferred mode**: callers (e.g. `LOG_INF`) only enqueue messages; a dedicated low-priority logging thread does formatting and output. This keeps log I/O out of time-critical paths (camera, display, inference). Configured via `CONFIG_LOG_MODE_DEFERRED=y` and `CONFIG_LOG_BUFFER_SIZE=2048` in `prj.conf`.

## UART console with DMA (optional)

To offload UART TX (and RX) to DMA and reduce CPU load:

1. Ensure `CONFIG_DMA=y` and `CONFIG_UART_STM32_DMA=y` in `prj.conf` (already set).
2. Build with the optional overlay so the console UART has DMA channels in devicetree:
   ```bash
   west build -b <board> -- -DOVERLAY_FILE=uart_dma.overlay
   ```
3. The provided `uart_dma.overlay` targets **USART1** (e.g. B-U585I-IOT02A). If your board uses another UART for console (e.g. `lpuart1`), edit the overlay: change the node (e.g. `&lpuart1`) and the DMA request slots to match your SoC (see the STM32U5 Reference Manual for GPDMA request mapping).
