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
- Open sketch, select board and upload

## Screenshots
TODO

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