<#
PowerShell helper to download and flash a firmware to the ESP32-C6 co-processor.

Usage:
  .\flash_esp32c6_copro_mcu.ps1 -Port COM7 -Url "https://example.com/esp32c6-firmware.zip" -Flash -EraseAll

Important:
- This script uses `python -m esptool` to flash the firmware. Ensure Python and esptool are installed with `pip install esptool`.
- Flashing will overwrite co-processor contents. Use `-DryRun` first to check everything.

Options:
  -Port <string>        : COM port for the co-processor (default COM7)
  -Url <string>         : URL to download the firmware zip/tar containing binary files (optional if using local files)
  -Bootloader <string>  : explicit local path to bootloader binary
  -PartTable <string>   : explicit local path to partition-table binary
  -App <string>         : explicit local path to app binary
  -Flash                : run esptool write_flash commands (default is DryRun)
  -EraseAll             : erase the entire flash before flashing
  -Chip <string>        : chip type (esp32c6 default)
  -Confirm              : proceed without asking for interactive confirmation
  -DryRun               : only show what would be done

This script assumes standard addresses:
  bootloader   -> 0x1000
  parti tbl    -> 0x8000
  app/main bin -> 0x10000

#>

param(
  [string]$Port = 'COM7',
  [string]$Url = '',
  [string]$GithubRepo = '',
  [string]$ReleaseTag = '',
  [string]$Bootloader = '',
  [string]$PartTable = '',
  [string]$App = '',
  [switch]$Flash,
  [switch]$EraseAll,
  [string]$Chip = 'esp32c6',
  [switch]$Confirm,
  [switch]$DryRun
)

function Write-Log($msg) {
  Write-Host "[flash-script] $msg"
}

function Ensure-EsptoolAvailable {
  try {
    $py = "python"
    $p = & $py -c "import esptool; print('ok')" 2>&1
    if ($p -match 'ok') { return $py }
  } catch { }
  # Try python3
  try {
    $py = "python3"
    $p = & $py -c "import esptool; print('ok')" 2>&1
    if ($p -match 'ok') { return $py }
  } catch { }
  Write-Log "Python with esptool not found. Please install Python and then run: pip install esptool"
  return $null
}

function Detect-Chip($py, $port) {
  try {
    $res = & $py -m esptool chip_id --port $port 2>&1
    return $res
  } catch {
    return $null
  }
}

function Download-And-Extract($url, $targetDir) {
  $tmp = Join-Path $targetDir (Split-Path $url -Leaf)
  Write-Log "Downloading $url -> $tmp"
  Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
  $ext = ([System.IO.Path]::GetExtension($tmp)).ToLower()
  if ($ext -eq '.zip') {
    Write-Log "Extracting ZIP..."
    Expand-Archive -Path $tmp -DestinationPath $targetDir -Force
  } elseif ($ext -eq '.gz' -or $ext -eq '.tgz') {
    Write-Log "GZIP/TAR archive handling not implemented; please supply a zip or local files"
  } else {
    Write-Log "Downloaded raw file: $tmp"
  }
  return $targetDir
}

# Main
Write-Log "Starting flash script"

$esppy = Ensure-EsptoolAvailable
if (-not $esppy) { Write-Log 'esptool not found; aborting'; exit 1 }

Write-Log "Using Python interpreter: $esppy"

# If download URL provided, download to temp dir
$tempDir = Join-Path $env:TEMP "esp32c6_flash_$(Get-Random)"
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
if ($Url) {
  Download-And-Extract -url $Url -targetDir $tempDir | Out-Null
}

if (-not $Url -and $GithubRepo) {
  Write-Log "Looking up latest release for GitHub repo: $GithubRepo"
  $api = "https://api.github.com/repos/$GithubRepo/releases"
    try {
    $releases = Invoke-RestMethod -Uri $api -UseBasicParsing
    if ($releases -and $releases.Count -gt 0) {
      if ($ReleaseTag) {
        $release = $releases | Where-Object { $_.tag_name -eq $ReleaseTag } | Select-Object -First 1
      } else {
        $release = $releases[0]
      }
      Write-Log "Found release: $($release.tag_name)"
      # Find first asset that looks like firmware (zip / bin)
      $asset = $release.assets | Where-Object { $_.name -match '\.(zip|bin|tar|tgz)$' } | Select-Object -First 1
      if ($asset) {
        $downloadUrl = $asset.browser_download_url
        Write-Log "Downloading asset: $($asset.name)"
        Download-And-Extract -url $downloadUrl -targetDir $tempDir | Out-Null
      } else {
        Write-Log "No suitable asset found in release; please provide -Url or -App/-Bootloader/-PartTable"
      }
    } else { Write-Log "No releases found in $GithubRepo" }
  } catch { Write-Log "Failed to query GitHub: $_" }
}

# Attempt to locate binaries either from provided args or temp dir
if (-not $Bootloader) { $Bootloader = Get-ChildItem -Path $tempDir -Recurse -File -Filter "*boot*.bin" -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object { $_.FullName } }
if (-not $PartTable)  { $PartTable  = Get-ChildItem -Path $tempDir -Recurse -File -Filter "*part*.bin" -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object { $_.FullName } }
if (-not $App)        { $App        = Get-ChildItem -Path $tempDir -Recurse -File -Filter "*app*.bin" -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object { $_.FullName } }

Write-Log "Bootloader: $Bootloader"
Write-Log "Partition Table: $PartTable"
Write-Log "App: $App"

# Basic validation - ensure we have at least the app binary
if (-not $App) { Write-Log "No app binary found. Provide -App or a zip with 'app' binary"; exit 1 }

# Confirm device presence
Write-Log "Detecting chip on port $Port"
$chipInfo = Detect-Chip -py $esppy -port $Port
Write-Log "Chip info: $chipInfo"
if ($chipInfo -notmatch $Chip) {
  Write-Log "Warning: chip description does not contain '$Chip' - it may still work, but make sure the correct device is attached."
}

# Present summary and ask for confirmation
Write-Host "\nFlash plan summary:" -ForegroundColor Yellow
Write-Host " Port: $Port"
Write-Host " Chip: $Chip"
Write-Host " Bootloader: $Bootloader"
Write-Host " Partition table: $PartTable"
Write-Host " App: $App"
Write-Host " EraseAll: $EraseAll"
Write-Host " DryRun: $DryRun"

if (-not $Confirm) {
  $ans = Read-Host "Proceed with flashing? (y/N)"
  if ($ans -ne 'y' -and $ans -ne 'Y') { Write-Log 'Aborting per user'; exit 0 }
}

if ($DryRun) { Write-Log "Dry run enabled — no changes made"; exit 0 }

# Optional erase
if ($EraseAll) {
  Write-Log "Erasing entire flash (takes a while)..."
  Write-Log "$esppy -m $Chip erase_flash --port $Port"
  & $esppy -m $Chip erase_flash --port $Port
}

# Compose write_flash command - only include files that are present
$cmd = "$esppy -m $Chip write_flash -z --flash_mode dio --flash_freq 80m"
if ($Bootloader) { $cmd += " 0x1000 `"$Bootloader`"" }
if ($PartTable)  { $cmd += " 0x8000 `"$PartTable`"" }
$cmd += " 0x10000 `"$App`""

# Build argument list for esptool call
$args = @('-m', $Chip, 'write_flash', '-z', '--flash_mode', 'dio', '--flash_freq', '80m')
if ($Bootloader) { $args += '0x1000'; $args += $Bootloader }
if ($PartTable)  { $args += '0x8000'; $args += $PartTable }
$args += '0x10000'; $args += $App
Write-Log "Executing: $esppy $($args -join ' ')"
& $esppy @args

Write-Log "Flash procedure finished."
Write-Host "Clean up temp: $tempDir"
# Note: leave temp for inspection

Write-Log "Done."

# End of script
