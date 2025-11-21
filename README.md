# ESPHome VoIP Library

Diese Bibliothek bietet SIP- und VoIP-Komponenten für ESPHome, um VoIP-Funktionalität auf ESP32/ESP8266-Geräten zu ermöglichen.

## Installation

Fügen Sie in Ihrer ESPHome-YAML-Konfiguration die externen Komponenten hinzu:

```yaml
external_components:
  - source: github://andreaswatch/EspHomeVoipLib
```

**Wichtig**: Klonen Sie dieses Repository mit Submodules: `git clone --recursive https://github.com/andreaswatch/EspHomeVoipLib.git`. Die externen Bibliotheken werden automatisch als Submodules geladen.

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

Gemäß ESPHome-Guidelines für externe Komponenten werden Abhängigkeiten **nicht automatisch installiert**, um Benutzerkontrolle und Kompatibilität zu gewährleisten. Stattdessen müssen Sie sie manuell hinzufügen.

### ESPHome-interne Abhängigkeiten
- `i2s_audio`: Wird automatisch geladen, wenn die VoIP-Komponente verwendet wird (deklariert in `manifest.json`).

### Externe PlatformIO-Bibliotheken (lib_deps)
Diese werden automatisch über die Submodules geladen. Keine manuelle Installation erforderlich.

- **ArduinoJson**: Für JSON-Verarbeitung.
- **esp32_opus_arduino**: Für Opus-Codec.
- **arduino-libg7xx**: Für G.72x-Codec.

Wenn Sie die `platformio.ini` verwenden, sind die Pfade bereits konfiguriert.

### Codecs
- **G.711 (Codec 0/1)**: Integriert, keine zusätzlichen Libs erforderlich.
- **Opus (Codec 2)**: Erfordert die Opus-Lib.
- **G.72x (Codec 3)**: Erfordert die G.72x-Lib.

Stellen Sie sicher, dass die Libs kompatibel mit Ihrer ESPHome-Version sind.

## Beispiel-Konfiguration

Eine vollständige, lauffähige Beispiel-Konfiguration finden Sie in `example.yaml`. Kopieren Sie diese Datei in Ihr ESPHome-Projekt und passen Sie die Werte an (z. B. WiFi, SIP-Daten).

**Wichtig**: Fügen Sie die externen Abhängigkeiten hinzu (siehe Abhängigkeiten-Abschnitt) oder kopieren Sie die `platformio.ini` aus diesem Repository.

```yaml
# Auszug aus example.yaml
esphome:
  name: voip-device-example

esp32:
  board: esp32dev
  lib_deps:  # Fügen Sie diese hinzu
    - ArduinoJson@^7.4.2
    - ...

external_components:
  - source: github://andreaswatch/EspHomeVoipLib

# ... Rest der Konfiguration
```

## Hinweise

- **Abhängigkeiten**: Befolgen Sie die ESPHome-Guidelines – externe Libs werden nicht automatisch geladen. Verwenden Sie die bereitgestellte `platformio.ini` oder fügen Sie `lib_deps` manuell hinzu.
- Testen Sie die Konfiguration in einer Entwicklungsumgebung.
- Stellen Sie sicher, dass die I2S-Pins korrekt konfiguriert sind.
- Für Opus und G.72x Codecs müssen die entsprechenden Bibliotheken kompiliert werden.
- Bei Problemen prüfen Sie die ESPHome-Logs und stellen Sie sicher, dass Ihre ESPHome-Version aktuell ist.