# NerdQAxe++ Compatibility Checklist

## Hardware Differences from BitForge Nano

| Component | BitForge Nano | NerdQAxe++ | AsicOS Status |
|-----------|--------------|------------|---------------|
| ASIC count | 2 | 4 | Board config ✅ |
| VR model | TPS546D24A (0x24) | TPS53647 (0x71) | Dual driver ✅ |
| VR phases | 2 | 3 | Config in TPS53647 init ⚠️ |
| Fan controller | 2x EMC2101 via PAC9544 mux | EMC2302 (0x2E) direct | Dual driver ✅ |
| Temp sensors | EMC2101 external diode | TMP1075 (0x48-0x4B) | Dual driver ✅ |
| Power monitor | INA260 (0x40) | VR PMBus telemetry | Dispatch by type ✅ |
| Display | None | ST7789 320x170 parallel | Display driver ✅ |
| I2C SDA/SCL | GPIO 47/48 | GPIO 44/43 | Board config ✅ |
| UART TX/RX | GPIO 17/18 | GPIO 17/18 | Same ✅ |
| ASIC Reset | GPIO 1 | GPIO 1 | Same ✅ |
| Buck Enable | GPIO 10 (PMBus controlled) | GPIO 10 | Board config ✅ |
| LDO Enable | None (-1) | GPIO 13 | Board config ✅ |
| LED | GPIO 9 | None (-1) | Board config ✅ |
| Buttons | None | GPIO 14 + GPIO 0 | Display driver ✅ |
| ADC VCORE | Yes (ADC1_CH1) | No | `has_adc_vcore` flag ✅ |
| Default freq | 250 MHz | 600 MHz | Board config ✅ |
| Default voltage | 1400 mV | 1150 mV | Board config ✅ |

## Pre-Flash Checklist

- [ ] Build with `CONFIG_ASICOS_BOARD_NERDQAXEPP=y`
- [ ] Verify CI builds successfully for NerdQAxe++ target
- [ ] Have serial logging ready before first boot

## Boot Sequence Test Plan

### Phase 1: Basic Boot
- [ ] Device powers on without crash
- [ ] Serial console shows boot log
- [ ] WiFi AP mode starts (`AsicOS-Setup`)
- [ ] Web UI loads at 192.168.4.1
- [ ] Can configure WiFi credentials
- [ ] Device connects to WiFi after restart

### Phase 2: I2C Peripherals
- [ ] TPS53647 VR initializes (check: `VR init: TPS53647 addr=0x71`)
- [ ] VR voltage set succeeds (no `ESP_FAIL` errors)
- [ ] TMP1075 temp sensors read valid values (20-80C range)
- [ ] EMC2302 fan controller initializes
- [ ] Fan RPM readings are non-zero
- [ ] Fan speed control works (try manual override to 100%)

### Phase 3: ASIC Communication
- [ ] UART initialized at 115200 baud
- [ ] ASIC reset sequence executes (GPIO 1 low/high)
- [ ] LDO enable (GPIO 13 high) before ASIC init
- [ ] Chip enumeration finds 4 chips (check: `Enumerated 4 chip(s)`)
- [ ] All 4 chips get addresses (0x00, 0x04, 0x08, 0x0C)
- [ ] Full register init completes without errors
- [ ] Frequency ramp to target completes
- [ ] FAST_UART register set before baud switch to 1Mbaud
- [ ] POST shows `ASIC Chain: 4 chip(s) found`

### Phase 4: Mining
- [ ] Stratum connects to pool
- [ ] Jobs dispatched (check: `Job sent: id=...`)
- [ ] Nonce responses received (check: `ASIC result: nonce=...`)
- [ ] Share difficulty is reasonable (>100)
- [ ] Shares submitted to pool
- [ ] Shares accepted by pool (accepted > 0, rejected = 0)
- [ ] Hashrate stabilizes (~2.4 TH/s at 600MHz with 4 chips)

### Phase 5: Display
- [ ] ST7789 display initializes without crash
- [ ] Mining screen shows hashrate, temps, shares
- [ ] Button 1 (GPIO 14) cycles screens
- [ ] Backlight auto-off after 60s works
- [ ] Button press wakes backlight

### Phase 6: Thermal Management
- [ ] PID fan control adjusts fan speed with temperature
- [ ] Overheat protection triggers at correct threshold
- [ ] VR fault detection works
- [ ] Emergency shutdown disables VR on overheat

## Known Risk Areas

### 1. TPS53647 Init Sequence
The TPS53647 driver was written based on the NerdQAxe++ reference but hasn't been tested on hardware. Key differences from TPS546D24A:
- Uses `pmbus_write16` for OPERATION (vs `pmbus_write8` for TPS546)
- Different linear16 voltage format
- 3-phase configuration

**If VR init fails:** The device will log errors but continue boot. ASICs won't get power and enumeration will find 0 chips. Check serial log for `VR init` messages.

### 2. 4-Chip Enumeration
The bulk read enumeration needs to handle 44 bytes (4 chips x 11 bytes). The 128-byte buffer is sufficient, but timing is critical:
- At 115200 baud, 44 bytes takes ~3.8ms
- The 200ms pre-read delay should be enough
- If only 2-3 chips found, increase the delay or try multiple enumeration attempts

### 3. LDO Enable Pin
The NerdQAxe++ has a separate LDO (GPIO 13) that the Nano doesn't. The boot sequence enables it before ASIC reset. If the LDO pin is wrong, ASICs won't power on.

### 4. Display GPIO Conflicts
The display uses GPIO 47/48 for data lines, which are also the I2C pins on the Nano. On NerdQAxe++ the I2C is on 44/43 so there's no conflict, but verify the display data pins don't interfere with any other peripheral.

### 5. Fan Control Mode
EMC2302 uses a different register set than EMC2101. The fan driver dispatches by `fan_type` but the EMC2302 implementation may need tuning for the NerdQAxe++ specific fan configuration (inverted polarity, PWM frequency).

## Quick Test Commands

```bash
# Build for NerdQAxe++
echo "CONFIG_ASICOS_BOARD_NERDQAXEPP=y" >> sdkconfig.defaults
idf.py build

# Flash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  erase_flash && \
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  write_flash \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x20000 build/AsicOS.bin \
  0x420000 www.bin

# Monitor serial
python3 -c "
import serial
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
f = open('nerdqaxe_boot.log', 'w')
while True:
    data = ser.read(1024)
    if data:
        text = data.decode('utf-8', errors='replace')
        print(text, end='')
        f.write(text)
        f.flush()
"

# Check API after WiFi connects
curl http://192.168.x.x/api/system/info | python3 -m json.tool
```
