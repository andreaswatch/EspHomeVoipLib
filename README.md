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

- `i2s_audio` (für VoIP-Komponente)
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
  platform: ESP32
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

# Optional: Logger für Debugging
logger:
  level: DEBUG
```

## Hinweise

- Testen Sie die Konfiguration in einer Entwicklungsumgebung.
- Stellen Sie sicher, dass die I2S-Pins korrekt konfiguriert sind.
- Für Opus und G.72x Codecs müssen die entsprechenden Bibliotheken kompiliert werden.