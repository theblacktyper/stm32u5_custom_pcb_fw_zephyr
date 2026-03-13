# SMP/MCUmgr UART Firmware Upgrade

## Reliable Procedure (camera shares UART with MCUmgr)

**Upgrade works when the camera has NOT been started.** Console, shell, and MCUmgr share USART1; when the camera runs, UART contention causes timeouts.

1. Power cycle the device.
2. **Do NOT press Button 2.**
3. Run upgrade immediately (smpmgr or mcumgr; both trigger "Updating Firmware" display):
   ```bash
   smpmgr --timeout 60 --port COM53 --baudrate 921600 upgrade path/to/zephyr.signed.bin
   ```
   For mcumgr, see the "Use mcumgr" section below (upload + image test + reset).
4. After upgrade completes, press Button 2 to start the camera.

## Quick Reference

```bash
smpmgr --timeout 60 --port COM53 --baudrate 921600 upgrade path/to/zephyr.signed.bin
```

## Common Failure: Timeout (2.5s)

**Symptom:** `Timeout (2.5s) waiting for request` on image upload.

**Cause:** smpmgr uses smpclient, which has a **per-request** timeout of 2.5 seconds (default). The `--timeout 60` you pass only affects connection and some other operations; **upload chunk requests** still use the smpclient default. Flash erase on the first chunk can take 5–10+ seconds on some MCUs.

**Workarounds:**

1. **Device-side (implemented):**
   - USART1 DMA removed (interrupt-driven UART for SMP)
   - Camera and inference pause during DFU (SMP command hooks)

2. **PC-side:** If timeouts persist, use **mcumgr** instead of smpmgr; it supports `-t 60` for per-request timeout:
   ```bash
   mcumgr -t 60 --conntype=serial --connstring="dev=COM53,baud=921600" image upload zephyr.signed.bin
   ```

3. **Or** patch smpmgr/smpclient to pass `timeout_s` to `SMPClient` constructor (smpclient accepts `timeout_s`; smpmgr's `get_smpclient` does not pass it).

## ENOTSUP Warning

`Error reading MCUMgr parameters: rc=<MGMT_ERR.ENOTSUP: 8>` (group 0, command 6) is a **warning**. It is smpmgr probing an optional OS management feature. It does not block the upgrade.

## Configuration (prj.conf)

- `CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS=y` – Pause camera/inference when any SMP command is received (smpmgr, mcumgr, etc.)
- `CONFIG_SYSTEM_WORKQUEUE_PRIORITY=0` – MCUmgr workqueue preempts camera/inference
- `CONFIG_LOG_DEFAULT_LEVEL=0` – No log output to UART (avoids corrupting SMP on shared port)
- `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096` – MCUmgr image handling
- `CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE=1024` – SMP packet size

## Overlay

- USART1: DMA removed for SMP compatibility (interrupt-driven mode)

## Verifying the Upgrade

The "Built: YYYY-MM-DD HH:MM" string in the UART log comes from a header generated at build time. It updates on every build. After an upgrade and power cycle, you should see the new build date.

**If the build date stays stale after upgrade:** MCUboot only copies slot1→slot0 when the new image has a *higher* version than the current one. Bump the version in `include/app_version.h` (format: `x.x.x`) before each release; it is passed to imgtool at build time so MCUboot installs OTA updates correctly.

**After changing mcuboot.conf:** Do a full rebuild and reflash both bootloader and app (e.g. `west build -p` then `west flash`), since the bootloader config changed.

## Troubleshooting: Timeout on First Chunk (off=0)

If the device times out even with 40s on the **first** upload chunk:

1. **Build from the correct source** – Ensure you build from the repo that has our changes (overlay, prj.conf, main.c). Your build dir path must match your source.

2. **Try upgrade before camera starts** – Power cycle, do **not** press Button 2, run upgrade immediately. This removes camera/inference as a variable.

3. **CONFIG_IMG_ERASE_PROGRESSIVELY** – Enabled in prj.conf so the device erases progressively and responds sooner instead of erasing the full slot first.

4. **Use mcumgr** – If smpmgr still fails, try:
   ```bash
   mcumgr -t 120 --conntype=serial --connstring="dev=COM53,baud=921600" image upload zephyr.signed.bin
   ```

5. **CONFIG_LOG_DEFAULT_LEVEL=0** – Log output to UART corrupts SMP responses when console and MCUmgr share the port. This is set in prj.conf. Raise to 1 or 2 for debugging when camera is off.
