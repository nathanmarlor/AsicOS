# AsicOS

Open-source Bitcoin mining firmware for ESP32-S3 boards with BM1370 ASIC chips.

Built from the ground up as a lightweight, industrial-grade mining controller with a modern web dashboard, full Prometheus observability, and over-the-air updates.

## Supported Hardware

| Board | ASICs | VR | Hashrate |
|-------|-------|----|----------|
| **BitForge Nano** | 2x BM1370 | TPS546D24A | ~3 TH/s |
| **NerdQAxe++** | 4x BM1370 | TPS53647 | ~6 TH/s |

## Quick Start

### Browser Flash (no tools required)

Visit **[nathanmarlor.github.io/AsicOS](https://nathanmarlor.github.io/AsicOS/)** and connect your board via USB. Select your board type and click Flash.

### First Boot

1. Connect to the `AsicOS` WiFi access point
2. Open `http://192.168.4.1` in your browser
3. Enter your WiFi credentials and pool details
4. Save and restart — mining starts automatically

## Web Dashboard

Two modes designed for different needs:

**Simple Mode** — hashrate, temperature, accepted shares, health overview. Clean and minimal for daily monitoring.

**Advanced Mode** — full telemetry: per-chip/domain hashrate, ASIC hashmap with nonce distribution, real-time temperature and power charts, submitted share feed with real difficulties, pool stats (RTT, block height, rejection breakdown), system info (CPU, WiFi RSSI, heap), and streaming log console.

Charts and share history persist across page refreshes via server-side ring buffers.

## Features

**Mining**
- Stratum v1 with version rolling (AsicBoost)
- Dual pool with automatic failover and exponential backoff
- Per-chip and per-domain hashrate from hardware counters
- Callback-style measurement with per-chip timestamps (matches ForgeOS/ESP-Miner)
- Real share difficulties tracked server-side

**Auto-Tuner**
- Eco / Balanced / Performance profiles
- Sweeps frequency and voltage combinations
- Samples hashrate with EMA reset between tests
- Proposes optimal settings for user confirmation

**Power & Thermal**
- PID fan control with manual override (persisted to NVS)
- EMC2101 / EMC2302 fan controllers via I2C mux
- TPS546D24A / TPS53647 voltage regulators (PMBus)
- INA260 power monitoring
- ADC VCORE readback
- Emergency shutdown with automatic recovery reboot

**Monitoring**
- 40+ Prometheus metrics at `/metrics`
- Hashrate, temperatures, power, shares, pool RTT, block height, rejection reasons, per-chip HW errors, per-task CPU usage, WiFi RSSI, heap breakdown
- WebSocket log streaming with ANSI stripping
- System alerts for overheat, VR fault, and crash/watchdog events

**OTA Updates**
- Separate firmware and web UI updates
- GitHub release check with one-click update
- Manual .bin upload via web UI
- Firmware updates preserve all NVS settings

## Building from Source

### Prerequisites

- [ESP-IDF v5.3](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/get-started/)
- Node.js 18+ (for web UI)

### Firmware

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

### Web UI

```bash
cd ui
npm install
npm run build     # outputs to ../build/www/
```

### Flash Web UI to Device

```bash
# Generate SPIFFS image and flash
python3 $IDF_PATH/components/spiffs/spiffsgen.py 0x300000 build/www www.bin
esptool.py -p /dev/ttyUSB0 write_flash 0x420000 www.bin
```

### Board Selection

Set the board variant in `sdkconfig` or via menuconfig:

```
CONFIG_ASICOS_BOARD_BITFORGE_NANO=y   # BitForge Nano (default)
CONFIG_ASICOS_BOARD_NERDQAXEPP=y      # NerdQAxe++
```

## API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/system/info` | GET | System telemetry (hashrate, temps, power, pool, config) |
| `/api/system/history` | GET | Chart history (120 points per metric) |
| `/api/mining/info` | GET | Mining details (per-chip, shares, recent nonces) |
| `/api/system` | POST | Update settings (pool, WiFi, frequency, voltage, fan) |
| `/api/system/restart` | POST | Restart device |
| `/api/system/ota` | POST | Upload firmware binary |
| `/api/system/ota/www` | POST | Upload web UI binary |
| `/api/system/ota/check` | GET | Check GitHub for updates |
| `/api/system/ota/github` | POST | Install update from GitHub |
| `/api/tuner/start` | POST | Start auto-tuner |
| `/metrics` | GET | Prometheus metrics |
| `/ws` | WS | Real-time log streaming |

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    ESP32-S3                          │
│                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ Stratum  │  │ Job      │  │ ASIC Sender      │  │
│  │ Client   │─>│ Creator  │─>│ (BM1370 UART)    │  │
│  │          │  │ (queue)  │  │                   │  │
│  └──────────┘  └──────────┘  └──────────────────┘  │
│       │                            │                │
│       │         ┌──────────┐       │                │
│       └────────>│ Result   │<──────┘                │
│                 │ Task     │                        │
│                 └──────────┘                        │
│                      │                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ Hashrate │  │ Power    │  │ HTTP Server      │  │
│  │ Monitor  │  │ Task     │  │ + WebSocket      │  │
│  │ (5s poll)│  │ (PID fan)│  │ + Prometheus     │  │
│  └──────────┘  └──────────┘  └──────────────────┘  │
│                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ WiFi     │  │ Auto     │  │ Vue 3 Web UI     │  │
│  │ + DNS    │  │ Tuner    │  │ (SPIFFS)         │  │
│  └──────────┘  └──────────┘  └──────────────────┘  │
└─────────────────────────────────────────────────────┘
```

## Credits

Hashrate calculation and ASIC register handling informed by [ESP-Miner](https://github.com/skot/ESP-Miner) (bitaxe) and [ForgeOS](https://github.com/nathanmarlor/forge-os). CRC-5/USB implementation from the BM1370 community documentation.

## License

MIT
