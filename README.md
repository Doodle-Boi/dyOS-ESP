# dyOS-ESP
An embedded firmware for ESP32-DevKitV1
# Note:
	The Project is still in beta and its my first time coding anything like this. there may bugs and bad design choices.
## Hardware:
* ESP32 Dev kit v1
	* Clockspeed: 240/400 Mhz
	* Ram: 512KB
	* Flash Memory: 4MB
	* Networking- wifi, bluetooth

* 1.3" Oled Display(i2c, blue, 64 x 128)
* 0.96" Oled Display(i2c, blue, 64 x 128)

* SD card Module(SPI)

* 4 x 4 Button matrix
	* Tactile Push buttons
	* Uses 8 GPIOS total
		* 4 Rows, 4 Columns 