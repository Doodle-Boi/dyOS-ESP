# ESP32 I2C OLED Test (ESP-IDF 6.x)

A small standalone ESP-IDF project to test a 128x64 I2C OLED using the ESP-IDF I2C master driver. It assumes an SSD1306-compatible controller at address `0x3C`.

## Wiring

- OLED VCC -> ESP32 3V3
- OLED GND -> ESP32 GND
- OLED SDA -> GPIO21
- OLED SCL -> GPIO22

Disconnect the microSD card while testing if you want to isolate the display.

## Build and flash

Open a terminal:

```zsh
source ~/esp/esp-idf/export.sh
cd ~/Documents/oled-test-idf
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl+]`.

## Expected result

The screen should show `OLED TEST`, `128 x 64`, a border, and a moving checker pattern.

## If the display stays blank

1. Confirm VCC is 3.3V, GND is shared, SDA=GPIO21 and SCL=GPIO22.
2. Check the I2C address in `main/oled_test.c`: try `0x3D` if your module uses that address.
3. This first test targets SSD1306-compatible modules. Some 1.5-inch 128x64 OLEDs use SH1106/SH1107 controllers and need a different initialization/driver. Tell me the exact module marking or share a clear photo of the back if it stays blank.
