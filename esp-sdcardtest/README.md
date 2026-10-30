# ESP32 SD-card shell (ESP-IDF 6.2)

A read-only microSD shell for a classic ESP32 DevKit using SPI. It follows the ESP-IDF `console` REPL API and the `esp_vfs_fat_sdspi_mount()` helper.

## Hardware wiring

| SD module pin | ESP32 DevKit V1 |
|---|---|
| DO / MISO | GPIO 19 |
| DI / MOSI | GPIO 23 |
| CLK / SCK | GPIO 18 |
| CS / SS | GPIO 13 |
| GND | GND |
| VCC | Module's supported supply voltage; check its regulator/level-shifting specifications |

Use a FAT32 card for the first test. The firmware sets `read_only = true` and `format_if_mount_failed = false`: it will not write to or auto-format the card.

## Build and flash

```zsh
source ~/esp/esp-idf/export.sh
cd ~/Documents/sdcard-shell-clean
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

If you put the extracted folder elsewhere, adjust the `cd` path. If your port differs, use the port shown by `ls -l /dev/ttyUSB*(N) /dev/ttyACM*(N)` in zsh.

## Commands

- `help`
- `ls [path]`
- `cd [directory]` (no argument returns to the SD root)
- `pwd`
- `cat <file>`

Paths like `/folder/file.txt` are interpreted relative to the SD root. The prompt changes as you `cd` into directories.

## Notes

- This project is isolated from the ESP-IDF installation and the separate `dyOS-ESP` repository.
- The selected CS pin is GPIO 13 to match the earlier known-good wiring. If the physical CS wire is on another pin, change `PIN_NUM_CS` in `main/main.c` to match it.
- If mounting fails, verify the wiring and card format first. Do not enable automatic formatting as a troubleshooting step.
