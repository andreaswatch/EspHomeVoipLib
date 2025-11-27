### Microphone recorder
The repository ships a small helper component `mic_recorder` which allows recording the I2S microphone independently from the VoIP component and exposes a small web API.

Example configuration (already included in `p4sip.yaml`):
```yaml
  mic_recorder:
    id: mic_recorder
    mic_id: board_microphone
```

Endpoints provided (once the web server is enabled):
- `/micrec/start?ms=1000` — start a recording for `ms` milliseconds, returns JSON status.
- `/micrec/stop` — stop recording immediately.
- `/micrec/latest` — download the last recorded audio as a WAV file (`audio/wav`).
- `/micrec/stream` — same as `/micrec/latest` for now (future enhancements could add live streaming).

You can add a button in YAML (already in `p4sip.yaml`) that triggers a 1s recording:
```yaml
  - platform: template
    name: "Mic Download 1s"
    on_press:
      - lambda: |-
          if (id(mic_recorder) != nullptr) id(mic_recorder).record_for_ms(1000);
```

This stores the recorded data in memory and exposes it as a WAV file via `/micrec/latest`. For longer recordings or to persist recordings across reboots, extend the component to save to SPIFFS/LittleFS.

Quick test via curl:

```bash
curl 'http://<device-ip>/micrec/start?ms=1000'
sleep 1.2
curl 'http://<device-ip>/micrec/latest' -o mic.wav
aplay -f cd mic.wav   # or play the mic.wav in your OS
```
# ESPHome VoIP Library

Diese Bibliothek bietet SIP- und VoIP-Komponenten für ESPHome, um VoIP-Funktionalität auf ESP32/ESP8266-Geräten zu ermöglichen.

## Installation

Fügen Sie in Ihrer ESPHome-YAML-Konfiguration die externen Komponenten hinzu:

```yaml
external_components:
  - source: github://andreaswatch/EspHomeVoipLib
```

## Verwendung

### SIP-Komponente

Die SIP-Komponente ermöglicht grundlegende SIP-Kommunikation.

```yaml
sip:
  id: my_sip
  sip_ip: "192.168.1.1"  # IP-Adresse des SIP-Servers
  sip_port: 5060         # SIP-Port (Standard: 5060)
  my_ip: "192.168.1.100" # Lokale IP-Adresse
  my_port: 5060          # Lokaler SIP-Port
  sip_user: "user"       # SIP-Benutzername
  sip_pass: "password"   # SIP-Passwort
  codec: 1               # Codec: 0=PCMU (G.711 μ-law), 1=PCMA (G.711 A-law), 2=Opus
```

### VoIP-Komponente

Die VoIP-Komponente integriert SIP mit Audio-Streaming über I2S.

```yaml
voip:
  id: my_voip
  sip_ip: "192.168.1.1"
  sip_user: "user"
  sip_pass: "password"
  codec: 1
  mic_gain: 2            # Mikrofon-Verstärkung
  amp_gain: 6            # Verstärker-Verstärkung
  # I2S-Konfiguration für Mikrofon
  mic_bck_pin: 26
  mic_ws_pin: 25
  mic_data_pin: 33
  mic_bits: 24
  mic_format: 0          # 0=ONLY_LEFT, 1=ONLY_RIGHT, 2=RIGHT_LEFT
  mic_buf_count: 4
  mic_buf_len: 8
  # I2S-Konfiguration für Verstärker
  amp_bck_pin: 14
  amp_ws_pin: 12
  amp_data_pin: 27
  amp_bits: 16
  amp_format: 0
  amp_buf_count: 16
  amp_buf_len: 60
```

## Abhängigkeiten

- Zusätzliche Bibliotheken für Codecs:
  - Opus (für Codec 2): Muss in der ESPHome-Umgebung verfügbar sein
  - G.72x (für Codec 3): Muss in der ESPHome-Umgebung verfügbar sein
  - G.711 (für Codec 0/1): Integriert

Stellen Sie sicher, dass diese Bibliotheken installiert sind, z.B. über PlatformIO oder ESP-IDF.

## Beispiel-Konfiguration

Hier ist eine vollständige Beispiel-Konfiguration, die alle erforderlichen Abhängigkeiten und Einstellungen enthält. Kopieren Sie diese als Basis für Ihre `config.yaml`:

```yaml
esphome:
  name: voip-device

esp32:
  board: esp32dev
  framework:
    type: arduino
    version: recommended
  build_flags:
    - -std=c++11
    - -DARDUINO_ARCH_ESP32
    - -DCORE_DEBUG_LEVEL=0
  lib_deps:
    - ArduinoJson@^7.4.2
    - viamgr/AwesomeClickButton@^1.0.1
    - https://github.com/sh123/esp32_opus_arduino.git
    - https://github.com/pschatzmann/arduino-libg7xx.git

external_components:
  - source: github://andreaswatch/EspHomeVoipLib

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

# Optional: OTA-Updates
ota:
  password: !secret ota_password

# SIP-Komponente
sip:
  id: my_sip
  sip_ip: !secret sip_server_ip
  sip_port: 5060
  my_ip: !secret local_ip
  my_port: 5060
  sip_user: !secret sip_user
  sip_pass: !secret sip_pass
  codec: 1

# VoIP-Komponente
voip:
  id: my_voip
  sip_ip: !secret sip_server_ip
  sip_user: !secret sip_user
  sip_pass: !secret sip_pass
  codec: 1
  mic_gain: 2
  amp_gain: 6
  mic_bck_pin: 26
  mic_ws_pin: 25
  mic_data_pin: 33
  mic_bits: 24
  mic_format: 0
  mic_buf_count: 4
  mic_buf_len: 8
  amp_bck_pin: 14
  amp_ws_pin: 12
  amp_data_pin: 27
  amp_bits: 16
  amp_format: 0
  amp_buf_count: 16
  amp_buf_len: 60

# Beispiel für einen Button zum Anrufen
button:
  - platform: template
    name: "Call 123"
    on_press:
      - lambda: id(my_voip).dial("123", "Test Call");

# Beispiel für einen Button zum Auflegen
button:
  - platform: template
    name: "Hangup"
    on_press:
      - lambda: id(my_voip).hangup();

# Beispiel für einen Button zum Aufnehmen und Abspielen (1s)
button:
  - platform: template
    name: "Record 1s & play back"
    on_press:
      - lambda: id(my_voip).record_and_playback_1s();

# Optional: Logger für Debugging
logger:
  level: DEBUG
```


## Waveshare ESP32-P4 Dev Board (ES8311)

If you use the Waveshare ESP32-P4 Dev Board with an ES8311 codec, the Waveshare default I2S pin mapping is:

- MCLK: GPIO13
- BCLK / SCLK: GPIO12
- LRCK (WS): GPIO10
- DIN (DATA_IN / DSDIN): GPIO9
- DOUT (DATA_OUT / ASDOUT): GPIO11
- PA_Ctrl (power amp enable): GPIO53

This mapping is the hardware default on the Waveshare dev board; use it in your YAML as shown in `p4sip.yaml` / `example_esp32p4.yaml`. If you are using another dev board or a custom breakout, verify the board schematic and use the correct pins for LRCLK/BCLK/DIN/DOUT and, if necessary, connect MCLK as well.

To quickly test the microphone input on that board, use `i2s_test.yaml` (the included example in this repo). It uses the Waveshare ES8311 mapping and logs the microphone sample size when the mic is started.



## Testen mit ESPHome in Docker

Um die Komponente zu testen, ohne ESPHome global zu installieren, verwenden Sie Docker:

1. **Stellen Sie sicher, dass Docker installiert ist.**

2. **Navigieren Sie zum Projektverzeichnis:**
   ```bash
   cd /path/to/EspHomeVoipLib
   ```

3. **Bearbeiten Sie `example.yaml`:**
   - Passen Sie WiFi-SSID/Passwort an.
   - Setzen Sie die SIP-Server-IP, Benutzer und Passwort.
   - Passen Sie I2S-Pins an Ihre Hardware an.

4. **Bauen Sie die Firmware:**
   ```bash
   docker run --rm -v $(pwd):/config -it esphome/esphome run example.yaml
   ```

   Dies kompiliert die Firmware und zeigt eventuelle Fehler an. Wenn erfolgreich, wird eine `.bin`-Datei erstellt.

5. **Für OTA-Updates (nach initialem Flash):**
   ```bash
   docker run --rm -v $(pwd):/config -it esphome/esphome run example.yaml --device OTA_IP
   ```

**Hinweis:** Ersetzen Sie `OTA_IP` durch die IP-Adresse Ihres ESP32 nach dem ersten Flash.

## Hinweise

- Testen Sie die Konfiguration in einer Entwicklungsumgebung.
- Stellen Sie sicher, dass die I2S-Pins korrekt konfiguriert sind.
- Für Opus und G.72x Codecs müssen die entsprechenden Bibliotheken kompiliert werden.

## ESP-Hosted Co-Processor flashing (ESP32-C6)

If your board contains an ESP32-C6 co-processor and you need to flash it with Espressif's `esp-hosted` releases, please follow the manual guide in `ESP-HOSTED-CO-PROCESSOR-FLASHING.md`: it explains how to detect the co-processor, how to pick the right release, and how to flash with `esptool`.

> Note: The repository previously included a PowerShell flashing helper. That helper has been deprecated and replaced by documented manual steps. See the `scripts` folder for details (DEPRECATED). If you want me to re-enable or improve a helper script, I can do that on request.