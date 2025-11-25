ESP-Hosted: ESP32-C6 Co-Processor — Manual Flashing Guide
======================================================

This guide explains how to find, download and manually flash an official Espressif
"esp-hosted" firmware release to the ESP32-C6 co‑processor on a board.

Quick summary
-------------
1. Verify whether the co‑processor connects as a separate USB serial device (COM) or is attached to the main MCU over SDIO/SPI.
2. If the co-processor exposes a COM port, use `esptool` to query the chip and flash binary parts (bootloader, partition table, app).
3. If the co-processor does *not* have a direct COM port (e.g. SDIO), use the board's recommended flash path (vendor docs). A direct flash may require wiring a USB‑UART to the co‑processor or using JTAG.

Prerequisites
-------------
- Python >=3.9
- esptool (install with `pip install esptool`)
- `7zip` or other archive tool to extract release zips
- A USB cable connected to the board's USB‑Serial interface (or a USB‑TTL adapter if connecting directly to the co‑processor)

Step 1 — Identify if the C6 is directly accessible
--------------------------------------------------
1. Connect your board and discover COM ports on Windows: Show Device Manager -> Ports (COM & LPT) or run:
```pwsh
Get-CimInstance Win32_SerialPort | Select-Object DeviceId,Caption,Description
```
2. Use esptool to check if a given COM belongs to an ESP32-C6:
```pwsh
python -m esptool --port COM7 chip-id
```
Output indicating ESP32-C6 looks like:
- `Chip type: ESP32-C6` (the script earlier warned because you had `ESP32-P4` on those COMs.)

Notes:
- If `esptool` returns `ESP32-P4`, `ESP32-P4` is connected at that port — not the C6.
- If `esptool` fails or returns unknown, check board Boot mode or use a direct UART/JTAG connection for the co-processor.

Step 2 — Get the correct esp-hosted release
-------------------------------------------
- Official esp-hosted releases are at: https://github.com/espressif/esp-hosted/releases
- There are at least two release flavors: `esp-hosted-ng` (Next‑Gen) and `esp-hosted-fg` (First‑Gen). Pick the one that matches the board/vendor instructions.
- Download the appropriate release zip for your transport (e.g. `ESP-Hosted-NG-release_v1.0.4.0.0.zip`).

Step 3 — Unpack and find binaries
---------------------------------
Extract the ZIP and look for `esp32` subfolder and a transport folder — for example:
```
esp32/sdio+uart
  bootloader.bin
  partition-table.bin
  network_adapter.bin  (this is typically the app binary for esp-hosted; name may vary)
  flash_cmd
```

Step 4 — Determine addresses and the right app file
--------------------------------------------------
- Most ESP32-family boards use these offsets:
  - Bootloader  -> 0x1000
  - Partition table -> 0x8000
  - App (main) -> 0x10000
- Some releases include a `flash_cmd` or release README that shows exact addresses.
- Confirm which binary is the main app (file might be `network_adapter.bin`, `hosted_app.bin`, or similar)

Step 5 — Confirm chip type again (optional but recommended)
--------------------------------------------------------
Before flashing, confirm the COM port maps to the C6:
```pwsh
python -m esptool --port COM7 chip-id
```
If you get `ESP32-C6`, you can flash directly with esptool.

Step 6 — Dry-run write (optional)
---------------------------------
You can test identifying the files and performing a dry run using the following steps:
1. Copy the binaries to a local folder (e.g. `C:\tmp\esp-hosted`)
2. Use `echo` and checks instead of flashing — or run esptool write_flash with `--resume` off in a non-production environment.

Step 7 — Flashing (example)
---------------------------
Replace `COMx` and `C:\tmp\esp-hosted` paths with values relevant to you.
```pwsh
# 1) Optional: Put the device into bootloader mode (board-specific: hold BOOT/EN, etc.)
# 2) Flash sequentially
python -m esptool --port COM7 write_flash -z 0x1000 C:\tmp\esp-hosted\bootloader.bin 0x8000 C:\tmp\esp-hosted\partition-table.bin 0x10000 C:\tmp\esp-hosted\network_adapter.bin
```

Notes and caution:
- If the device is attached via SDIO to the host MCU (no direct COM), esptool will not be able to talk to the C6 directly. You must use the board-specific method.
- For SDIO-based co-processor setups you may need to flash via the host MCU or use vendor-specific tools. Consult board docs.
- Use `--baud 115200` or a suitable baud rate if needed and `--chip esp32c6` flag if `esptool` requires explicit chip type.

Step 8 — Verify
---------------
- After flashing, reset the co-processor and verify logs or the host's expected behavior.
- Re-run `chip-id` to confirm the co‑processor is active: `python -m esptool --port COM7 chip-id`

Troubleshooting
---------------
- If you repeatedly see `ESP32-P4` on COM ports, C6 is not exposed over USB; check board docs for direct flash or JTAG.
- If `esptool` complains about `--port` or `chip_id` deprecation, update esptool via `pip install -U esptool` and use `chip-id` command.
- If `network_adapter.bin` is not the app, check `flash_cmd` in release or the release README for the proper file name.

Resources
---------
- esp-hosted repo: https://github.com/espressif/esp-hosted
- esp-hosted releases: https://github.com/espressif/esp-hosted/releases
- esptool: https://github.com/espressif/esptool
- Espressif docs and board reference: https://www.espressif.com/en

If you want, I can also add a short board-specific subsection if you give me your dev-kit exact model (e.g. ESP32-P4 DevKit or other). For many ESP32-P4 boards, the co-processor is SDIO attached and not direct USB; that means you cannot flash the C6 via a normal COM port.

---
Generated by repository maintainer tools. Remove when done.
