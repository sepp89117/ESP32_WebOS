# ESP32_WebOS
<b>Control GPIOs, manage files and work with BLE - everything on a lovingly designed web interface!</b><br>
So much more is possible! I look forward to your participation!

## Currently Supported Boards
- ESP32_DEV (not tested)
- ESP32_WROVER_KIT (not tested)
- ESP32_PICO (not tested)
- ESP32_POE_ISO (tested)
- LOLIN32 (not tested)
- LOLIN32_LITE (not tested)

## Requirements
- Arduino IDE with esp32 platform version 3.0.4
- Librarys 'NimBLE-Arduino' and 'ESPAsyncWebServer' installed
- ESP32 with connected SD card (SPIFFS will be implemented in the future)
- Files copied from folder "data" to SD card
- Partition Scheme: Huge APP
- Open sketch and check lines 10 to 15 in 'ESP32_WebOS.ino' for individual settings
- Select your ESP32 board and upload

## Screenshots
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/desktop_v0.1.png?raw=true"><br>
<br>
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/gpio_ctrl_v0.1.png?raw=true"><br>
Set GPIO modes and values! Settings are saved on SD! The read values ​​are updated every second.<br>
<br>
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/file_explorer_v0.1.png?raw=true"><br>
Download, rename, edit, delete files and directorys by context menu. Upload by drag and drop into File Explorer window.<br>
<br>
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/file_editor_v0.1.png?raw=true"><br>
<br>
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/ble_v0.1.png?raw=true"><br>
<br>
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/ble_details_v0.1.png?raw=true"><br>
<br>
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/calculator_v0.1.png?raw=true"><br>
<br>
<img src="https://github.com/sepp89117/ESP32_WebOS/blob/main/screenshots/light_mode_v0.1.png?raw=true"><br>
And in addition to the dark mode, a light mode is also automatically recognized!<br>

## External Libraries used
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) with [Apache-2.0 license](https://github.com/h2zero/NimBLE-Arduino#Apache-2.0-1-ov-file) by [h2zero](https://github.com/h2zero)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) without license by [me-no-dev](https://github.com/me-no-dev)

## TODOs
- Add Apache 2.0 license
- Implement BLE read, write, notify etc.
- Implement cut, copy, and paste for folders and files in File Explorer
- Implement keyup listeners for various actions in File Explorer
- Implement file properties window
- Extend the features of File Editor
- Implement availability of SPIFFS as file storage
- Scan WiFi and select and connect network from search results
- Network config -> static IP and change AP
- Let ESP32 perform the calculations of the calculator inputs
- SPI or I2C hotplug hardware detection possible?
