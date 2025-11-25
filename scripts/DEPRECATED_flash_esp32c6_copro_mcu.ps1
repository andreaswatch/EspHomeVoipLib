<#
DEPRECATED (moved): Old PowerShell helper for flashing an ESP32-C6 co-processor.

This helper was replaced by manual, documented steps in the repository root
`ESP-HOSTED-CO-PROCESSOR-FLASHING.md`. The original helper used `esptool` but
was prone to environment quirks and caused confusion when the co-processor is
attached via SDIO or not exposed as a single USB serial device.

If you absolutely need a helper script, consult the doc and then re-enable a
script implementation that suits your environment.

This file is intentionally inert.
#>

Write-Host "DEPRECATED: see ../ESP-HOSTED-CO-PROCESSOR-FLASHING.md for instructions"