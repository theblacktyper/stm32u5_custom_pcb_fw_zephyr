# Golioth cloud OTA firmware updates

This application integrates the [Golioth Firmware SDK](https://github.com/golioth/golioth-firmware-sdk) (v0.22.0) for **over-the-air** firmware updates while keeping **UART MCUmgr** (SMP) as a separate wired recovery path.

## Obtain the SDK

The build expects the SDK at:

`modules/golioth-firmware-sdk/`

**Option A — Git clone (matches `west.yml` in this repo):**

```bash
git clone --depth 1 --branch v0.22.0 https://github.com/golioth/golioth-firmware-sdk.git modules/golioth-firmware-sdk
```

**Option B — West:** merge the `projects` entry from [west.yml](../west.yml) into your Zephyr workspace `west.yml`, then `west update`.

`CMakeLists.txt` adds the SDK via `ZEPHYR_EXTRA_MODULES` when `modules/golioth-firmware-sdk/zephyr/module.yml` exists.

## Configuration

| Item | Where |
|------|--------|
| WiFi | `CONFIG_WIFI_AUTOCONNECT_*` in `prj.conf` |
| Golioth PSK (lab only) | `CONFIG_GOLIOTH_APP_PSK_ID`, `CONFIG_GOLIOTH_APP_PSK` in `prj.conf` |
| Golioth log level (Zephyr `LOG_*`) | `CONFIG_GOLIOTH_LOG_LEVEL_ERR=y` (or WRN/INF/DBG) in `prj.conf` |
| Golioth `GLTH_LOG*` level | `CONFIG_GOLIOTH_DEBUG_DEFAULT_LOG_LEVEL` (1 = error; see SDK `Kconfig.logging`) |
| Package name (Console) | `CONFIG_GOLIOTH_FW_UPDATE_PACKAGE_NAME` (default `main`) |
| Firmware version string | `APP_VERSION` in `include/app_version.h` (must match signed artifact and Console) |

Replace hardcoded PSKs with per-device provisioning before production.

### ZVFS (eventfd)

The Golioth stack uses POSIX **eventfd** through Zephyr’s ZVFS layer. A full-featured app (camera, TFLM, networking) can exceed default fd/eventfd limits; you may see `eventfd creation failed, errno: 12` and then `abort()`. This project sets `CONFIG_ZVFS_OPEN_MAX` and `CONFIG_ZVFS_EVENTFD_MAX` in `prj.conf` accordingly.

### TLS / cloud connectivity

If you see `TLS handshake error` or `Failed to connect: -113` from `golioth_coap_client_zephyr`, check: **PSK / PSK-ID** match the device in the Golioth Console, **DNS** can resolve Golioth’s hostname (`CONFIG_DNS_SERVER1`), outbound **firewall** allows the device to reach Golioth, and optionally raise **`CONFIG_MBEDTLS_HEAP_SIZE`** if memory during handshake is tight.

## Build artifact for the Console

With **sysbuild** + MCUboot, upload the **signed** application image produced by the build, for example:

`build/kk_edge_ai_tflm_hello/zephyr/zephyr.signed.bin`

(Exact path depends on your build directory and application folder name.)

The artifact **version** in Golioth must match `APP_VERSION` and the version baked into the image via `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION`.

## Golioth Console workflow

1. Create a **package** whose name matches `CONFIG_GOLIOTH_FW_UPDATE_PACKAGE_NAME` (default `main`).
2. **Upload** `zephyr.signed.bin` as a new package version; set the version string to match `APP_VERSION`.
3. Assign the device to a **cohort**.
4. Create a **deployment** targeting that package version and roll out.

Reference: [Zephyr Firmware Update Walkthrough](https://docs.golioth.io/reference/firmware/golioth-firmware-sdk/firmware-upgrade/fw-update-zephyr).

## UART MCUmgr vs cloud OTA

- **MCUmgr (USART1):** SMP image upload as today; `dfu_state` suspends camera/inference during wired DFU.
- **Golioth:** Runs after DHCP obtains an IPv4 address; `dfu_cloud_ota_set_active()` mirrors suspend behavior during download/apply.

Do not run heavy workloads during either path.

## MCUboot mode (overwrite-only)

This project uses **MCUboot overwrite-only** (`SB_CONFIG_MCUBOOT_MODE_OVERWRITE_ONLY=y`) with your existing flash partition layout. The Golioth Zephyr `fw_update` logic uses Zephyr `flash_img` / MCUboot APIs compatible with that layout; if you change to dual-slot swap, re-validate partitions and sysbuild settings.

## Signing keys

Use your own MCUboot signing key for release images; align the key used to sign binaries uploaded to Golioth with the key in your bootloader.
