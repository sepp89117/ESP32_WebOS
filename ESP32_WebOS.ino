/*
 * ESP32_WebOS v0.1
 *
 * Platform: esp32@3.0.4
 * Partition Scheme: Huge APP
 */

// ************************************ User Setup ************************************
// If you receive the error "Board not supported!" during compilation, please create an issue "Board not supported!" with the board name and link to the manufacturer information
#define USE_ETH 1 // Uncomment to use Ethernet instead of WiFi
#define WIFI_SSID "your-ssid" // Only needed with WiFi but don't comment it out!
#define WIFI_PASSWORD "your-password" // Only needed with WiFi but don't comment it out!
#define NTP_URL "time.google.com" // Visit https://gist.github.com/mutin-sa/eea1c396b1e610a2da1e5550d94b0453 'List of Top Public Time Servers'
// #define USE_SPIFFS 1 // TODO implementation; currently only SD
#define FORMAT_SPIFFS_IF_FAILED true // You only need to format SPIFFS the first time you run a test or else use the SPIFFS plugin to create a partition https://github.com/me-no-dev/arduino-esp32fs-plugin

// ************************************ Includings ************************************
#include <Arduino.h>
#ifdef USE_ETH
#include <ETH.h>
#define NW_LIB ETH
#else // !USE_ETH
#include <WiFi.h>
#define NW_LIB WiFi
#endif // USE_ETH
#include <ESP32Time.h>
#include <NimBLEDevice.h>
#include <string>
#include <sstream>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <atomic>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#ifdef USE_SPIFFS
#include "SPIFFS.h"
#endif // USE_SPIFFS
#ifdef SOC_SDMMC_HOST_SUPPORTED
#include "SD_MMC.h"
#define SD_LIB SD_MMC
#else // !SOC_SDMMC_HOST_SUPPORTED
#include "SD.h"
#define SD_LIB SD
#endif // SOC_SDMMC_HOST_SUPPORTED
#include "SPI.h"

// ************************************ Defines ************************************
#define CONFIG_FILE_NAME "/config.txt"
#define TEMP_CONFIG_FILE_NAME "/config.tmp"
//** ESP Control Tokens
#define NTP_UPDATE_TOKEN "/update_ntp"       // Force update time from ntp
#define SET_RTC_OFFSET_TOKEN "/setRtcOffset" // Set RTC offset
#define RESTART_TOKEN "/restart"             // Force restart
//** BLE Tokens
#define BLE_SCAN_TOKEN "/scanBLE"         // Force start ble scan
#define BLE_CONNECT_TOKEN "/conBLE"       // Connect device
#define BLE_DISCONNECT_TOKEN "/disconBLE" // Disconnect device
//** GPIO Control Tokens
#define SET_PIN_MODE_TOKEN "/setPinMode"
#define SET_PIN_VALUE_TOKEN "/setPinValue"
//** File System Tokens
#define UPLOAD_TOKEN "/upload"  // Upload request
#define MOVE_TOKEN "/mv"        // Rename or move files and directories to another location
#define COPY_TOKEN "/cp"        // Copy a file
#define COPY_DIR_TOKEN "/cpdir" // Copy a directory
#define MKDIR_TOKEN "/mkdir"    // Create a directory
#define MK_TOKEN "/mk"          // Create a file
#define RMDIR_TOKEN "/rmdir"    // Remove a directory
#define RM_TOKEN "/rm"          // Remove a file

// ************************************ Structures ************************************
struct WSClient
{
    uint32_t id = ULONG_MAX;
    bool isConnected() { return this->id < ULONG_MAX; };
    bool fsUpdatesRegistered = false;   // Client has subscribed to file system (FS) updates
    bool gpioUpdatesRegistered = false; // Client has subscribed to GPIO updates
    bool bleUpdatesRegistered = false;  // Client has subscribed to BLE updates
};

struct GPIOPin
{
    uint8_t pin;     // Pin number
    const char type; // Analog + digital ('A') or digital only ('D')
    int value;
    uint8_t mode;
    bool isFree;    // Is the pin free to use?
    bool canOutput; // Does the pin have an output?
};

struct BLEDev
{
    NimBLEAdvertisedDevice advertisedDevice;
    NimBLEClient *pClient;
    std::vector<NimBLERemoteService *> *services;
    bool isSet() { return !this->advertisedDevice.getAddress().equals(NimBLEAddress("")); };
    bool found; // Was the device found during the last scan?
    bool isConnected() { return this->pClient != nullptr && this->pClient->isConnected(); };
    bool toConnect;    // Should the device be connected?
    bool toDisconnect; // Should the device be disconnected?
};

// ************************************ Instances ************************************
ESP32Time rtc(0);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
NimBLEScan *pBLEScan;

// ************************************ Variables ************************************
WSClient wsClients[DEFAULT_MAX_WS_CLIENTS]; // Websocket clients
uint32_t lastSocketUpdate = 0;              // Timestamp in seconds of the last data update for websocket clients
uint32_t nextNTPUpdate = 0;                 // Timestamp in seconds when NTP update should be performed
bool toRestart = false;                     // Should ESP32 be restarted?
bool nwConnected = false;                   // Are we connected to the network?
//** FS
size_t sdTotalBytes = 0;
size_t sdUsedBytes = 0;
bool spiffsAvailable = true;
//** BLE
BLEDev bleDevs[CONFIG_BT_NIMBLE_MAX_CONNECTIONS];
uint32_t bleScanSeconds = 5;
bool isBleScanActive = false;
bool isBleConnecting = false;
//** Buffers
std::string statusJsonBuffer;
std::string fsJsonBuffer;
std::string gpioJsonBuffer;
std::string bleJsonBuffer;

// { uint8_t pin; const char type; int value; uint8_t mode; bool isFree; bool canOutput /* set in `initGPIOs()` */; }
#if defined(ARDUINO_ESP32_DEV)
GPIOPin gpios[] = {
    {0, 'A', 0, INPUT, true, true},
    {1, 'D', 0, INPUT, false, true},
    {2, 'A', 0, INPUT, true, true},
    {3, 'D', 0, INPUT, false, true},
    {4, 'A', 0, INPUT, true, true},
    {5, 'D', 0, INPUT, true, true},
    {12, 'A', 0, INPUT, true, true},
    {13, 'A', 0, INPUT, true, true},
    {14, 'A', 0, INPUT, true, true},
    {15, 'A', 0, INPUT, true, true},
    {16, 'D', 0, INPUT, true, true},
    {17, 'D', 0, INPUT, true, true},
    {18, 'D', 0, INPUT, true, true},
    {19, 'D', 0, INPUT, true, true},
    {21, 'D', 0, INPUT, true, true},
    {22, 'D', 0, INPUT, true, true},
    {23, 'D', 0, INPUT, true, true},
    {25, 'A', 0, INPUT, true, true},
    {26, 'A', 0, INPUT, true, true},
    {27, 'A', 0, INPUT, true, true},
    {32, 'A', 0, INPUT, true, true},
    {33, 'A', 0, INPUT, true, true}};
#elif defined(ARDUINO_ESP32_WROVER_KIT)
GPIOPin gpios[] = {
    {0, 'A', 0, INPUT, true, true},
    {2, 'A', 0, INPUT, true, true},
    {4, 'A', 0, INPUT, true, true},
    {12, 'A', 0, INPUT, true, true},
    {13, 'A', 0, INPUT, true, true},
    {14, 'A', 0, INPUT, true, true},
    {15, 'A', 0, INPUT, true, true},
    {16, 'D', 0, INPUT, true, true},
    {17, 'D', 0, INPUT, true, true},
    {18, 'D', 0, INPUT, true, true},
    {19, 'D', 0, INPUT, true, true},
    {21, 'D', 0, INPUT, true, true},
    {22, 'D', 0, INPUT, true, true},
    {23, 'D', 0, INPUT, true, true},
    {25, 'A', 0, INPUT, true, true},
    {26, 'A', 0, INPUT, true, true},
    {27, 'A', 0, INPUT, true, true},
    {32, 'A', 0, INPUT, true, true},
    {33, 'A', 0, INPUT, true, true},
    {34, 'A', 0, INPUT, true, true},
    {35, 'A', 0, INPUT, true, true},
    {36, 'A', 0, INPUT, true, true},
    {39, 'A', 0, INPUT, true, true}};
#elif defined(ARDUINO_ESP32_PICO)
GPIOPin gpios[] = {
    {0, 'A', 0, INPUT, true, true},
    {1, 'D', 0, INPUT, false, true},
    {3, 'D', 0, INPUT, false, true},
    {4, 'A', 0, INPUT, true, true},
    {5, 'D', 0, INPUT, true, true},
    {12, 'A', 0, INPUT, true, true},
    {13, 'A', 0, INPUT, true, true},
    {14, 'A', 0, INPUT, true, true},
    {15, 'A', 0, INPUT, true, true},
    {16, 'D', 0, INPUT, true, true},
    {17, 'D', 0, INPUT, true, true},
    {18, 'D', 0, INPUT, true, true},
    {19, 'D', 0, INPUT, true, true},
    {21, 'D', 0, INPUT, true, true},
    {22, 'D', 0, INPUT, true, true},
    {23, 'D', 0, INPUT, true, true}};
#elif defined(ARDUINO_ESP32_POE_ISO)
GPIOPin gpios[] = {
    {0, 'A', 0, INPUT, true, true},  // used for boot and flash too
    {1, 'D', 0, INPUT, false, true}, // UART0 TX
    {2, 'A', 0, INPUT, false, true}, // used by SD card
    {3, 'D', 0, INPUT, false, true}, // UART0 RX
    {4, 'A', 0, INPUT, true, true},
    {5, 'D', 0, INPUT, true, true},
    {13, 'A', 0, INPUT, true, true},
    {14, 'A', 0, INPUT, false, true}, // used by SD card
    {15, 'A', 0, INPUT, false, true}, // used by SD card
    {16, 'D', 0, INPUT, true, true},
    {32, 'A', 0, INPUT, true, true},
    {33, 'A', 0, INPUT, true, true},
    {34, 'A', 0, INPUT, true, true}, // Connected to BUT1 with 10k pullup
    {35, 'A', 0, INPUT, true, true},
    {36, 'A', 0, INPUT, true, true},
    {39, 'A', 0, INPUT, true, true} // External power supply voltage
};
#elif defined(ARDUINO_LOLIN32)
GPIOPin gpios[] = {
    {0, 'A', 0, INPUT, true, true},
    {1, 'D', 0, INPUT, false, true}, // UART0 TX / digital only
    {2, 'A', 0, INPUT, true, true},
    {3, 'D', 0, INPUT, false, true}, // UART0 RX / digital only
    {4, 'A', 0, INPUT, true, true},
    {5, 'D', 0, INPUT, true, true}, // digital only
    {12, 'A', 0, INPUT, true, true},
    {13, 'A', 0, INPUT, true, true},
    {14, 'A', 0, INPUT, true, true},
    {15, 'A', 0, INPUT, true, true},
    {16, 'D', 0, INPUT, true, true}, // digital only
    {17, 'D', 0, INPUT, true, true}, // digital only
    {18, 'D', 0, INPUT, true, true}, // digital only
    {19, 'D', 0, INPUT, true, true}, // digital only
    {21, 'D', 0, INPUT, true, true}, // digital only
    {22, 'D', 0, INPUT, true, true}, // digital only
    {23, 'D', 0, INPUT, true, true}, // digital only
    {25, 'A', 0, INPUT, true, true}, // DAC1
    {26, 'A', 0, INPUT, true, true}, // DAC2
    {27, 'A', 0, INPUT, true, true},
    {32, 'A', 0, INPUT, true, true},
    {33, 'A', 0, INPUT, true, true},
    {34, 'A', 0, INPUT, true, false},
    {35, 'A', 0, INPUT, true, false},
    {36, 'A', 0, INPUT, true, false},
    // 37 na
    // 38 na
    {39, 'A', 0, INPUT, true, false}};
#elif defined(ARDUINO_LOLIN32_LITE) // default
GPIOPin gpios[] = {
    {0, 'A', 0, INPUT, true, true},
    // 1 na
    {2, 'A', 0, INPUT, true, true},
    // 3 na
    {4, 'A', 0, INPUT, true, true},
    {5, 'D', 0, INPUT, true, true}, // digital only
    {12, 'A', 0, INPUT, true, true},
    {13, 'A', 0, INPUT, true, true},
    {14, 'A', 0, INPUT, true, true},
    {15, 'A', 0, INPUT, true, true},
    {16, 'D', 0, INPUT, true, true}, // digital only
    {17, 'D', 0, INPUT, true, true}, // digital only
    {18, 'D', 0, INPUT, true, true}, // digital only
    {19, 'D', 0, INPUT, true, true}, // digital only
    // 21 na
    {22, 'D', 0, INPUT, true, true}, // digital only
    {23, 'D', 0, INPUT, true, true}, // digital only
    {25, 'A', 0, INPUT, true, true}, // DAC1
    {26, 'A', 0, INPUT, true, true}, // DAC2
    {27, 'A', 0, INPUT, true, true},
    {32, 'A', 0, INPUT, true, true},
    {33, 'A', 0, INPUT, true, true},
    {34, 'A', 0, INPUT, true, false},
    {35, 'A', 0, INPUT, true, false},
    {36, 'A', 0, INPUT, true, false},
    // 37 na
    // 38 na
    {39, 'A', 0, INPUT, true, false}};
#elif defined(ARDUINO_ARCH_ESP32)   // fallback
#warning "The pins of your chosen board are unknown. Please report this in an issue on github."
GPIOPin gpios[] = {
    {0, 'A', 0, INPUT, true, true},
    {1, 'D', 0, INPUT, false, true}, // UART0 TX / digital only
    {2, 'A', 0, INPUT, true, true},
    {3, 'D', 0, INPUT, false, true}, // UART0 RX / digital only
    {4, 'A', 0, INPUT, true, true},
    {5, 'D', 0, INPUT, true, true}, // digital only
    // 6 to 11 reserved for SPI flash access on WROVER and WROOM modules
    {12, 'A', 0, INPUT, true, true},
    {13, 'A', 0, INPUT, true, true},
    {14, 'A', 0, INPUT, true, true},
    {15, 'A', 0, INPUT, true, true},
    {16, 'D', 0, INPUT, true, true}, // digital only
    {17, 'D', 0, INPUT, true, true}, // digital only
    {18, 'D', 0, INPUT, true, true}, // digital only
    {19, 'D', 0, INPUT, true, true}, // digital only
    // 20 na
    {21, 'D', 0, INPUT, true, true}, // digital only
    {22, 'D', 0, INPUT, true, true}, // digital only
    {23, 'D', 0, INPUT, true, true}, // digital only
    // 24 na
    {25, 'A', 0, INPUT, true, true}, // DAC1
    {26, 'A', 0, INPUT, true, true}, // DAC2
    {27, 'A', 0, INPUT, true, true},
    // 28 to 31 na
    {32, 'A', 0, INPUT, true, true},
    {33, 'A', 0, INPUT, true, true},
    //* 34 to 39 have input only
    {34, 'A', 0, INPUT, true, false},
    {35, 'A', 0, INPUT, true, false},
    {36, 'A', 0, INPUT, true, false},
    {37, 'A', 0, INPUT, true, false},
    {38, 'A', 0, INPUT, true, false},
    {39, 'A', 0, INPUT, true, false}};
#else
#error "This program is only intended for ESP32 boards! Check the board you selected."
#endif

// ************************************ Constants ************************************
const uint8_t gpios_len = sizeof(gpios) / sizeof(gpios[0]);
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
// Map with BLE UUIDs and their common names
const std::map<std::string, std::string> uuidMap = {
    // Services (data from 2024-07-24)
    {"0x1800", "GAP Service"},
    {"0x1801", "GATT Service"},
    {"0x1802", "Immediate Alert Service"},
    {"0x1803", "Link Loss Service"},
    {"0x1804", "Tx Power Service"},
    {"0x1805", "Current Time Service"},
    {"0x1806", "Reference Time Update Service"},
    {"0x1807", "Next DST Change Service"},
    {"0x1808", "Glucose Service"},
    {"0x1809", "Health Thermometer Service"},
    {"0x180a", "Device Information Service"},
    {"0x180d", "Heart Rate Service"},
    {"0x180e", "Phone Alert Status Service"},
    {"0x180f", "Battery Service"},
    {"0x1810", "Blood Pressure Service"},
    {"0x1811", "Alert Notification Service"},
    {"0x1812", "Human Interface Device Service"},
    {"0x1813", "Scan Parameters Service"},
    {"0x1814", "Running Speed and Cadence Service"},
    {"0x1815", "Automation IO Service"},
    {"0x1816", "Cycling Speed and Cadence Service"},
    {"0x1818", "Cycling Power Service"},
    {"0x1819", "Location and Navigation Service"},
    {"0x181a", "Environmental Sensing Service"},
    {"0x181b", "Body Composition Service"},
    {"0x181c", "User Data Service"},
    {"0x181d", "Weight Scale Service"},
    {"0x181e", "Bond Management Service"},
    {"0x181f", "Continuous Glucose Monitoring Service"},
    {"0x1820", "Internet Protocol Support Service"},
    {"0x1821", "Indoor Positioning Service"},
    {"0x1822", "Pulse Oximeter Service"},
    {"0x1823", "HTTP Proxy Service"},
    {"0x1824", "Transport Discovery Service"},
    {"0x1825", "Object Transfer Service"},
    {"0x1826", "Fitness Machine Service"},
    {"0x1827", "Mesh Provisioning Service"},
    {"0x1828", "Mesh Proxy Service"},
    {"0x1829", "Reconnection Configuration Service"},
    {"0x183a", "Insulin Delivery Service"},
    {"0x183b", "Binary Sensor Service"},
    {"0x183c", "Emergency Configuration Service"},
    {"0x183d", "Authorization Control Service"},
    {"0x183e", "Physical Activity Monitor Service"},
    {"0x183f", "Elapsed Time Service"},
    {"0x1840", "Generic Health Sensor Service"},
    {"0x1843", "Audio Input Control Service"},
    {"0x1844", "Volume Control Service"},
    {"0x1845", "Volume Offset Control Service"},
    {"0x1846", "Coordinated Set Identification Service"},
    {"0x1847", "Device Time Service"},
    {"0x1848", "Media Control Service"},
    {"0x1849", "Generic Media Control Service"},
    {"0x184a", "Constant Tone Extension Service"},
    {"0x184b", "Telephone Bearer Service"},
    {"0x184c", "Generic Telephone Bearer Service"},
    {"0x184d", "Microphone Control Service"},
    {"0x184e", "Audio Stream Control Service"},
    {"0x184f", "Broadcast Audio Scan Service"},
    {"0x1850", "Published Audio Capabilities Service"},
    {"0x1851", "Basic Audio Announcement Service"},
    {"0x1852", "Broadcast Audio Announcement Service"},
    {"0x1853", "Common Audio Service"},
    {"0x1854", "Hearing Access Service"},
    {"0x1855", "Telephony and Media Audio Service"},
    {"0x1856", "Public Broadcast Announcement Service"},
    {"0x1857", "Electronic Shelf Label Service"},
    {"0x1858", "Gaming Audio Service"},
    {"0x1859", "Mesh Proxy Solicitation Service"},
    // Characteristics
    {"0x2a00", "Device Name"},
    {"0x2a01", "Appearance"},
    {"0x2a04", "Peripheral Preferred Connection Parameters"},
    {"0x2a05", "Service Changed"},
    {"0x2A19", "Battery Level"},
    //... add more if needed

    // Customs
    {"ade3d529-c784-4f63-a987-eb69f70ee816", "Tizen OIC Transport Profile GATT service"},
    {"ad7b334f-4637-4b86-90b6-9d787f03d218", "Tizen OIC Transport Profile GATT request characteristic"},
    {"e9241982-4580-42c4-8831-95048216b256", "Tizen OIC Transport Profile GATT response characteristic"}};

// ************************************ Functions ************************************
void disconnectAndDeleteBleDevClient(uint8_t index);

#ifdef USE_ETH
void onNwEvent(arduino_event_id_t event)
{
    switch (event)
    {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("ETH Started");
        ETH.setHostname("ESP32_WebOS");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("ETH Connected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.println("ETH Got IP");
        Serial.println(ETH);
        nwConnected = true;
        break;
    case ARDUINO_EVENT_ETH_LOST_IP:
        Serial.println("ETH Lost IP");
        nwConnected = false;
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        nwConnected = false;
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        nwConnected = false;
        break;
    default:
        break;
    }
}
#endif

class MyClientCallback : public NimBLEClientCallbacks
{
    void onConnect(NimBLEClient *pClient)
    {
        Serial.println("Connected");
    }

    void onDisconnect(NimBLEClient *pClient)
    {
        Serial.println("BLE dev disconnected");
        for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
        {
            if (bleDevs[i].advertisedDevice.getAddress() == pClient->getPeerAddress())
            {
                disconnectAndDeleteBleDevClient(i);
                break;
            }
        }
    }

    void onConnectFailure(NimBLEClient *pClient)
    {
        Serial.println("Failed to connect");
        for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
        {
            if (bleDevs[i].advertisedDevice.getAddress() == pClient->getPeerAddress())
            {
                disconnectAndDeleteBleDevClient(i);
                break;
            }
        }
    }

    uint32_t onPassKeyRequest()
    {
        Serial.println("onPassKeyRequest()");
        return 123456; // TODO
    };

    bool onConfirmPIN(uint32_t pass_key)
    {
        Serial.println("onConfirmPIN()");
        return true; // TODO
    };

    void onAuthenticationComplete(ble_gap_conn_desc *desc)
    {
        if (!desc->sec_state.encrypted)
        {
            Serial.println("Encrypt connection failed!");
            NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
            return;
        }
        Serial.println("Encrypt connection success.");
    };
};

void setup()
{
    Serial.begin(115200);
    while (!Serial)
        ;
    delay(1000);

    Serial.println("Setup...");
    delay(50);

    statusJsonBuffer.reserve(256);
    fsJsonBuffer.reserve(2048);
    gpioJsonBuffer.reserve(1024);
    bleJsonBuffer.reserve(4096);

    // SPIFFS
#ifdef USE_SPIFFS
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
    {
        Serial.println("SPIFFS Mount Failed!");
        spiffsAvailable = false;
    }
#endif // USE_SPIFFS

    // SD
    if (
#ifdef SOC_SDMMC_HOST_SUPPORTED
        !SD_LIB.begin("/sdcard",
#ifdef BOARD_HAS_1BIT_SDMMC
                      true // mode1bit
#else                      // !BOARD_HAS_1BIT_SDMMC
                      false
#endif                     // BOARD_HAS_1BIT_SDMMC
                      )
#else  // !SOC_SDMMC_HOST_SUPPORTED
        !SD_LIB.begin()
#endif // SOC_SDMMC_HOST_SUPPORTED
    )
    {
        Serial.println("Card Mount Failed");
    }
    else
    {
        uint8_t cardType = SD_LIB.cardType();

        if (cardType == CARD_NONE)
        {
            Serial.println("No SD card attached");
        }
        else
        {

            Serial.print("SD Card Type: ");
            if (cardType == CARD_MMC)
            {
                Serial.println("MMC");
            }
            else if (cardType == CARD_SD)
            {
                Serial.println("SDSC");
            }
            else if (cardType == CARD_SDHC)
            {
                Serial.println("SDHC");
            }
            else
            {
                Serial.println("UNKNOWN");
            }

            uint64_t cardSize = SD_LIB.cardSize() / (1024 * 1024);
            Serial.printf("SD Card Size: %llu MB\n", cardSize);

            sdTotalBytes = SD_LIB.totalBytes();
            sdUsedBytes = SD_LIB.usedBytes();
        }
    }

    Serial.printf("Init %u pins...\n", gpios_len);
    initGPIOs();
    Serial.println("Pins initiated.");

    // BLE
    NimBLEDevice::init("");
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_DISPLAY);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_SIGN | BLE_SM_PAIR_KEY_DIST_LINK);
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(1000);
    pBLEScan->setWindow(100);

// Network
#ifdef USE_ETH
    Network.onEvent(onNwEvent);
#endif
    while (!connectNetwork())
    {
        delay(1000);
    }
    Serial.println("IP address: ");
    Serial.println(NW_LIB.localIP());

    // Websocket and Web Server
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.onFileUpload(handleFileUpload);
    server.onNotFound(handleClient);

    server.begin();

    char *defaultStr = "0";
    char resultStr[7];
    if (readConfigValue("GMT_OFFSET", defaultStr, resultStr) > 0)
    {
        long offset = strtol(resultStr, NULL, 10);
        rtc.offset = offset;
    }

    updateNTP(true);
}

void loop()
{
    connectNetwork();

    ws.cleanupClients();

    updateSocket();

    updateNTP(false);

    if (toRestart)
        ESP.restart();

    for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
    {
        if (bleDevs[i].toConnect)
        {
            bleDevs[i].toConnect = false;
            if (connectBLE(i))
                Serial.println("BLE dev connected.");
            else
                Serial.println("BLE dev connect failed!");
        }
        else if (bleDevs[i].toDisconnect)
        {
            bleDevs[i].toDisconnect = false;
            if (disconnectBLE(i))
                Serial.println("BLE dev disconnected.");
            else
                Serial.println("BLE dev disconnect failed!");
        }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
}

bool connectNetwork()
{
#ifdef USE_ETH
    if (nwConnected)
        return true;
#else
    if (WiFi.status() == WL_CONNECTED)
        return true;
#endif

    Serial.println("Connecting Network");
    uint32_t startNwCon = millis();
#ifdef USE_ETH
    while (ETH.begin() == 0)
    {
        if (millis() - startNwCon >= 6000)
        {
            Serial.println("");
            Serial.println("ETH Network timeout!");
            delay(1000);
            return false;
        }
        delay(500);
        Serial.print(".");
    }
#else
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startNwCon >= 6000)
        {
            Serial.println("");
            Serial.println("WiFi Network timeout!");
            WiFi.disconnect();
            delay(1000);
            nwConnected = false;
            return false;
        }
        delay(500);
        Serial.print(".");
    }
#endif
    Serial.println("");
    Serial.println("Network connected.");
    nwConnected = true;
    return true;
}

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (!index)
    {
        Serial.printf("Upload Start: %s\n", filename.c_str());
        if (SD_LIB.exists(filename))
        {
            SD_LIB.remove(filename);
        }
    }

    File file = SD_LIB.open(filename, FILE_APPEND);
    if (file)
    {
        if (file.write(data, len) != len)
        {
            Serial.println("File write failed");
            request->send(400, "text/plain", "File write failed");
        }
        file.close();
    }
    else
    {
        Serial.println("Failed to open file for writing");
        request->send(400, "text/plain", "Failed to open file for writing");
        return;
    }
    if (final)
    {
        Serial.printf("Upload Complete: %s, Size: %u\n", filename.c_str(), index + len);
        request->send(200, "text/plain", "OK");
    }
}

String getContentType(String filename)
{
    if (filename.endsWith(".htm") || filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".css"))
        return "text/css";
    else if (filename.endsWith(".js"))
        return "application/javascript";
    else if (filename.endsWith(".png"))
        return "image/png";
    else if (filename.endsWith(".gif"))
        return "image/gif";
    else if (filename.endsWith(".jpg"))
        return "image/jpeg";
    else if (filename.endsWith(".svg"))
        return "image/svg+xml";
    else if (filename.endsWith(".ico"))
        return "image/x-icon";
    else if (filename.endsWith(".xml"))
        return "text/xml";
    else if (filename.endsWith(".pdf"))
        return "application/x-pdf";
    else if (filename.endsWith(".zip"))
        return "application/x-zip";
    else if (filename.endsWith(".gz"))
        return "application/x-gzip";
    return "text/plain";
}

void handleClient(AsyncWebServerRequest *request)
{
    uint8_t method = request->method();
    String url = request->url();

    if (method == HTTP_GET)
    {
        // Serial.printf("HTTP_GET url: '%s'\n", url.c_str());

        if (url.endsWith("/"))
            url += "index.html";

        if (SD_LIB.exists(url))
        {
            String contentType = request->hasArg("download") ? "application/octet-stream" : getContentType(url);
            AsyncWebServerResponse *response = request->beginResponse(SD_LIB, url, contentType);
            request->send(response);
        }
        else
        {
            request->send(404, "text/plain", "Not found");
        }
    }
    else if (method == HTTP_POST)
    {
        /* Serial.printf("HTTP_POST url: '%s'\n", url.c_str());
        int args = request->args();
        for (uint8_t i = 0; i < args; i++)
        {
            Serial.printf("ARG[%s]: %s\n", request->argName(i).c_str(), request->arg(i).c_str());
        } */
        String statement = url.substring(0, url.indexOf(' '));

        /* int setPinModeTokenIndex = statement.indexOf(SET_PIN_MODE_TOKEN);
        int setPinValueTokenIndex = statement.indexOf(SET_PIN_VALUE_TOKEN); */

        // Control
        if (statement == NTP_UPDATE_TOKEN)
        {
            request->send(200, "text/plain", "OK");
            updateNTP(true);
        }
        else if (statement == SET_RTC_OFFSET_TOKEN)
        {
            if (request->hasParam("offset", true))
            {
                String offsetStr = decodeURL(request->getParam("offset", true)->value());
                int offset = offsetStr.toInt();
                rtc.offset = offset;
                updateConfigValue("GMT_OFFSET", offsetStr.c_str());

                request->send(200, "text/plain", "OK");
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == BLE_SCAN_TOKEN)
        {
            request->send(200, "text/plain", "OK");

            startBleScan();
        }
        else if (statement == BLE_CONNECT_TOKEN)
        {
            if (request->hasParam("dev", true))
            {
                String iStr = decodeURL(request->getParam("dev", true)->value());
                int index = strtol(iStr.c_str(), NULL, 10);

                if (index >= 0 && index < CONFIG_BT_NIMBLE_MAX_CONNECTIONS)
                {
                    bleDevs[index].toDisconnect = false;
                    bleDevs[index].toConnect = true;
                    request->send(200, "text/plain", "OK");
                }
                else
                {
                    request->send(400, "text/plain", "Error!");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == BLE_DISCONNECT_TOKEN)
        {
            if (request->hasParam("dev", true))
            {
                String iStr = decodeURL(request->getParam("dev", true)->value());
                int index = strtol(iStr.c_str(), NULL, 10);

                if (index >= 0 && index < CONFIG_BT_NIMBLE_MAX_CONNECTIONS)
                {
                    bleDevs[index].toConnect = false;
                    bleDevs[index].toDisconnect = true;
                    request->send(200, "text/plain", "OK");
                }
                else
                {
                    request->send(400, "text/plain", "Error!");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == SET_PIN_MODE_TOKEN)
        {
            if (request->hasParam("pin", true) && request->hasParam("mode", true))
            {
                String pinStr = decodeURL(request->getParam("pin", true)->value());
                String modeStr = decodeURL(request->getParam("mode", true)->value());

                uint8_t pin = pinStr.toInt();
                uint8_t mode = modeStr.toInt();

                if (setPinMode(pin, mode))
                {
                    char modeConfigName[13];
                    sprintf(modeConfigName, "GPIO%u_MODE", pin);
                    updateConfigValue(modeConfigName, modeStr.c_str());

                    request->send(200, "text/plain", "OK");
                }
                else
                {
                    request->send(400, "text/plain", "Error");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == SET_PIN_VALUE_TOKEN)
        {
            if (request->hasParam("pin", true) && request->hasParam("value", true))
            {
                String pinStr = decodeURL(request->getParam("pin", true)->value());
                String valueStr = decodeURL(request->getParam("value", true)->value());

                uint8_t pin = pinStr.toInt();
                int value = valueStr.toInt();

                Serial.printf("Pin %u write value %i...\n", pin, value);
                setPinValue(pin, value);
                char valueConfigName[12];
                sprintf(valueConfigName, "GPIO%u_VAL", pin);
                updateConfigValue(valueConfigName, valueStr.c_str());

                request->send(200, "text/plain", "OK");
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == RESTART_TOKEN)
        {
            request->send(200, "text/plain", "OK");

            toRestart = true;
            Serial.println("ESP restart triggered.");
        }
        else if (statement == UPLOAD_TOKEN)
        { // noop
        }
        else if (statement == RM_TOKEN)
        {
            // Remove a file
            if (request->hasParam("path", true))
            {
                String path = decodeURL(request->getParam("path", true)->value());
                if (SD_LIB.remove(path.c_str()))
                {
                    Serial.printf("File '%s' deleted.\n", path.c_str());
                    request->send(200, "text/plain", "File deleted");
                }
                else
                {
                    Serial.printf("Delete file '%s' failed!\n", path.c_str());
                    request->send(400, "text/plain", "Delete file failed");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == RMDIR_TOKEN)
        {
            // Remove a directory
            if (request->hasParam("path", true))
            {
                String path = decodeURL(request->getParam("path", true)->value());
                if (removeDirectory(path))
                {
                    Serial.printf("Directory '%s' deleted.\n", path.c_str());
                    request->send(200, "text/plain", "Directory deleted");
                }
                else
                {
                    Serial.printf("Delete directory '%s' failed!\n", path.c_str());
                    request->send(400, "text/plain", "Delete directory failed");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == MKDIR_TOKEN)
        {
            // Make a directory
            if (request->hasParam("path", true))
            {
                String path = decodeURL(request->getParam("path", true)->value());
                if (SD_LIB.mkdir(path.c_str()))
                {
                    Serial.printf("Directory '%s' created.\n", path.c_str());
                    request->send(200, "text/plain", "Directory created");
                }
                else
                {
                    Serial.printf("Create directory '%s' failed!\n", path.c_str());
                    request->send(400, "text/plain", "Create directory failed");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == MK_TOKEN)
        {
            // Make a file
            if (request->hasParam("path", true))
            {
                String path = decodeURL(request->getParam("path", true)->value());
                if (!SD_LIB.exists(path.c_str()))
                {
                    File file = SD_LIB.open(path.c_str(), FILE_WRITE);
                    if (file)
                    {
                        file.close();
                        Serial.printf("File '%s' created.\n", path.c_str());
                        request->send(200, "text/plain", "File created");
                    }
                    else
                    {
                        Serial.printf("Create file '%s' failed!\n", path.c_str());
                        request->send(400, "text/plain", "Create file failed");
                    }
                }
                else
                {
                    Serial.printf("File '%s' already exists!\n", path.c_str());
                    request->send(400, "text/plain", "File already exists");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else if (statement == MOVE_TOKEN)
        {
            // Rename or move
            if (request->hasParam("path", true) && request->hasParam("to", true))
            {
                String path = decodeURL(request->getParam("path", true)->value());
                String to = decodeURL(request->getParam("to", true)->value());

                if (SD_LIB.exists(path.c_str()))
                {
                    if (SD_LIB.rename(path.c_str(), to.c_str()))
                    {
                        Serial.printf("Move '%s' to '%s' successfully.\n", path.c_str(), to.c_str());
                        request->send(200, "text/plain", "OK");
                    }
                    else
                    {
                        Serial.printf("Move file '%s' to '%s' failed!\n", path.c_str(), to.c_str());
                        request->send(400, "text/plain", "Move file failed");
                    }
                }
                else
                {
                    Serial.printf("File '%s' does not exist!\n", path.c_str());
                    request->send(400, "text/plain", "File does not exist!");
                }
            }
            else
            {
                request->send(400, "text/plain", "Missing parameter");
            }
        }
        else
        {
            request->send(400, "text/plain", "Bad Request");
        }

        statement = String();
    }
    else if (method == HTTP_DELETE)
    {
        Serial.printf("HTTP_DELETE url: '%s'\n", url.c_str());
    }
    else if (method == HTTP_PUT)
    {
        Serial.printf("HTTP_PUT url: '%s'\n", url.c_str());
    }
    else if (method == HTTP_PATCH)
    {
        Serial.printf("HTTP_PATCH url: '%s'\n", url.c_str());
    }
    else if (method == HTTP_HEAD)
    {
        Serial.printf("HTTP_HEAD url: '%s'\n", url.c_str());
    }
    else if (method == HTTP_OPTIONS)
    {
        Serial.printf("HTTP_OPTIONS url: '%s'\n", url.c_str());
    }
    else if (method == HTTP_ANY)
    {
        Serial.printf("HTTP_ANY url: '%s'\n", url.c_str());
    }
    else
    {
        Serial.printf("Unknown method %u with url: '%s'\n", method, url.c_str());
    }
}

bool removeDirectory(String path)
{
    File dir = SD_LIB.open(path);
    if (!dir)
    {
        Serial.println("Failed to open directory");
        return false;
    }

    if (!dir.isDirectory())
    {
        Serial.println("Not a directory");
        return false;
    }

    File file;
    while ((file = dir.openNextFile()))
    {
        String filePath = path + "/" + file.name();
        if (file.isDirectory())
        {
            // Rekursiv das Unterverzeichnis löschen
            if (!removeDirectory(filePath))
            {
                file.close();
                return false;
            }
        }
        else
        {
            // Datei löschen
            if (!SD_LIB.remove(filePath))
            {
                Serial.print("Failed to delete file: ");
                Serial.println(filePath);
                file.close();
                return false;
            }
        }
        file.close();
    }
    dir.close();

    // Löschen des nun leeren Verzeichnisses
    return SD_LIB.rmdir(path);
}

void updateNTP(bool override)
{
    if (rtc.getEpoch() >= nextNTPUpdate || override)
    {
        Serial.println("Get time from NTP...");
        bool updated = updateTime(0);
        Serial.println(updated ? "Got time from NTP." : "Got no time from NTP!");
        nextNTPUpdate = rtc.getEpoch() + (updated ? 3600 : 60);
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        // Serial.printf("ws[%s][%lu] connect\n", server->url(), client->id());
        client->ping();

        // set a wsClient
        for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
        {
            if (!wsClients[i].isConnected())
            {
                wsClients[i].id = client->id();
                break;
            }
        }
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        // Serial.printf("ws[%s][%lu] disconnect\n", server->url(), client->id());

        // reset the wsClient
        for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
        {
            if (wsClients[i].id == client->id())
            {
                wsClients[i].id = ULONG_MAX;
                wsClients[i].fsUpdatesRegistered = false;
                wsClients[i].gpioUpdatesRegistered = false;
                wsClients[i].bleUpdatesRegistered = false;
                break;
            }
        }
    }
    else if (type == WS_EVT_ERROR)
    {
        Serial.printf("ws[%s][%lu] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
    }
    else if (type == WS_EVT_PONG)
    {
        // Serial.printf("ws[%s][%lu] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
    }
    else if (type == WS_EVT_DATA)
    {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        String msg = "";
        if (info->final && info->index == 0 && info->len == len)
        {
            // the whole message is in a single frame and we got all of it's data
            // Serial.printf("ws[%s][%lu] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

            if (info->opcode == WS_TEXT)
            {
                for (size_t i = 0; i < info->len; i++)
                {
                    msg += (char)data[i];
                }
            }
            else
            {
                char buff[4];
                for (size_t i = 0; i < info->len; i++)
                {
                    sprintf(buff, "%02x ", (uint8_t)data[i]);
                    msg += buff;
                }
            }
            // Serial.printf("%s\n", msg.c_str());
        }
        else
        {
            // message is comprised of multiple frames or the frame is split into multiple packets
            /* if (info->index == 0)
            {
                if (info->num == 0)
                    Serial.printf("ws[%s][%lu] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
                Serial.printf("ws[%s][%lu] frame[%lu] start[%llu]\n", server->url(), client->id(), info->num, info->len);
            }

            Serial.printf("ws[%s][%lu] frame[%lu] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len); */

            if (info->opcode == WS_TEXT)
            {
                for (size_t i = 0; i < len; i++)
                {
                    msg += (char)data[i];
                }
            }
            else
            {
                char buff[4];
                for (size_t i = 0; i < len; i++)
                {
                    sprintf(buff, "%02x ", (uint8_t)data[i]);
                    msg += buff;
                }
            }
            /* Serial.printf("%s\n", msg.c_str());

            if ((info->index + len) == info->len)
            {
                Serial.printf("ws[%s][%lu] frame[%lu] end[%llu]\n", server->url(), client->id(), info->num, info->len);
                if (info->final)
                {
                    Serial.printf("ws[%s][%lu] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
                }
            } */
        }

        if (msg == "registerFsUpdates")
        {
            for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
            {
                if (wsClients[i].id == client->id())
                {
                    wsClients[i].fsUpdatesRegistered = true;
                    break;
                }
            }
        }
        else if (msg == "registerGpioUpdates")
        {
            for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
            {
                if (wsClients[i].id == client->id())
                {
                    wsClients[i].gpioUpdatesRegistered = true;
                    break;
                }
            }
        }
        else if (msg == "registerBleUpdates")
        {
            for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
            {
                if (wsClients[i].id == client->id())
                {
                    wsClients[i].bleUpdatesRegistered = true;
                    break;
                }
            }
        }
        else if (msg == "unregisterFsUpdates")
        {
            for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
            {
                if (wsClients[i].id == client->id())
                {
                    wsClients[i].fsUpdatesRegistered = false;
                    break;
                }
            }
        }
        else if (msg == "unregisterGpioUpdates")
        {
            for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
            {
                if (wsClients[i].id == client->id())
                {
                    wsClients[i].gpioUpdatesRegistered = false;
                    break;
                }
            }
        }
        else if (msg == "unregisterBleUpdates")
        {
            for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
            {
                if (wsClients[i].id == client->id())
                {
                    wsClients[i].bleUpdatesRegistered = false;
                    break;
                }
            }
        }
    }
}

void updateSocket()
{
    uint32_t timestamp = rtc.getEpoch();
    if (timestamp - lastSocketUpdate >= 1 && ws.availableForWriteAll() && ws.count() > 0)
    {
        buildStatusJson();
        buildFsJson();
        buildGpiosJson();
        buildBleDevsJson();

        for (uint8_t i = 0; i < DEFAULT_MAX_WS_CLIENTS; i++)
        {
            if (wsClients[i].isConnected() && ws.availableForWrite(wsClients[i].id))
            {
                ws.text(wsClients[i].id, statusJsonBuffer.c_str());

                if (wsClients[i].fsUpdatesRegistered)
                    ws.text(wsClients[i].id, fsJsonBuffer.c_str());

                if (wsClients[i].gpioUpdatesRegistered)
                    ws.text(wsClients[i].id, gpioJsonBuffer.c_str());

                if (wsClients[i].bleUpdatesRegistered)
                    ws.text(wsClients[i].id, bleJsonBuffer.c_str());
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        lastSocketUpdate = timestamp;
    }
}

void buildStatusJson()
{
    statusJsonBuffer = "{";

    statusJsonBuffer += "\"status\":{";

    statusJsonBuffer += "\"ts\":";
    statusJsonBuffer += std::to_string(rtc.getEpoch());
    statusJsonBuffer += ",";
    statusJsonBuffer += "\"freeHeap\":";
    statusJsonBuffer += std::to_string(ESP.getFreeHeap());
    statusJsonBuffer += ",";
    statusJsonBuffer += "\"freePsram\":";
    statusJsonBuffer += std::to_string(ESP.getFreePsram());
    statusJsonBuffer += ",";
    statusJsonBuffer += "\"wifiRssi\":";
    statusJsonBuffer += std::to_string(getWiFiStrength());
    statusJsonBuffer += ",";
    statusJsonBuffer += "\"sdTotal\":";
    statusJsonBuffer += std::to_string(sdTotalBytes);
    statusJsonBuffer += ",";
    statusJsonBuffer += "\"sdUsed\":";
    statusJsonBuffer += std::to_string(sdUsedBytes);
    statusJsonBuffer += ",";
    statusJsonBuffer += "\"bleConnecting\":";
    statusJsonBuffer += (isBleConnecting ? "true" : "false");
    statusJsonBuffer += ",";
    statusJsonBuffer += "\"bleScanActive\":";
    statusJsonBuffer += (isBleScanActive ? "true" : "false");

    statusJsonBuffer += "}";

    statusJsonBuffer += "}";
}

void buildFsJson()
{
    fsJsonBuffer = "{";

    fsJsonBuffer += "\"fs\":{";

    fsJsonBuffer += "\"name\":\"/\",";
    fsJsonBuffer += "\"isDirectory\":true,";

    fsJsonBuffer += "\"sub\":[";
    File root = SD_LIB.open("/");
    addDirectoryJson(root);
    root.close();
    fsJsonBuffer += "]";

    fsJsonBuffer += "}";

    fsJsonBuffer += "}";
}

void buildGpiosJson()
{
    gpioJsonBuffer = "{";

    gpioJsonBuffer += "\"gpios\":[";
    // TODO if (gpioUpdatesRegistered)
    checkGPIOs();
    for (uint8_t i = 0; i < gpios_len; i++)
    {
        if (i > 0)
            gpioJsonBuffer += ",";

        gpioJsonBuffer += "{";

        gpioJsonBuffer += "\"n\":";
        gpioJsonBuffer += std::to_string(gpios[i].pin); //.c_str()
        gpioJsonBuffer += ",";

        gpioJsonBuffer += "\"t\":\"";
        gpioJsonBuffer += gpios[i].type;
        gpioJsonBuffer += "\",";

        gpioJsonBuffer += "\"v\":";
        gpioJsonBuffer += std::to_string(gpios[i].value);
        gpioJsonBuffer += ",";

        gpioJsonBuffer += "\"m\":";
        gpioJsonBuffer += std::to_string(gpios[i].mode);
        gpioJsonBuffer += ",";

        gpioJsonBuffer += "\"f\":";
        gpioJsonBuffer += (gpios[i].isFree ? "true" : "false");
        gpioJsonBuffer += ",";

        gpioJsonBuffer += "\"o\":";
        gpioJsonBuffer += (gpios[i].canOutput ? "true" : "false");

        gpioJsonBuffer += "}";
    }
    gpioJsonBuffer += "]";

    gpioJsonBuffer += "}";
}

void buildBleDevsJson()
{
    bleJsonBuffer = "{";

    bleJsonBuffer += "\"bleDevs\":[";
    uint8_t sets = 0;
    if (!isBleScanActive && !isBleConnecting)
    {
        for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
        {
            if (!bleDevs[i].isSet())
                continue;

            if (sets > 0)
                bleJsonBuffer += ",";

            bleJsonBuffer += "{";

            NimBLEAddress devAdr = bleDevs[i].advertisedDevice.getAddress();
            std::string devAdrStr = devAdr.toString();
            std::string devName = bleDevs[i].advertisedDevice.getName();
            int rssi = bleDevs[i].advertisedDevice.getRSSI();
            uint16_t advItvl = bleDevs[i].advertisedDevice.getAdvInterval(); // The advertisement interval in 0.625ms units.

            bleJsonBuffer += "\"i\":";
            bleJsonBuffer += std::to_string(i);
            bleJsonBuffer += ",";

            bleJsonBuffer += "\"name\":\"";
            bleJsonBuffer += devName.c_str();
            bleJsonBuffer += "\",";

            bleJsonBuffer += "\"addr\":\"";
            bleJsonBuffer += devAdrStr.c_str();
            bleJsonBuffer += "\",";

            bleJsonBuffer += "\"rssi\":";
            bleJsonBuffer += std::to_string(rssi);
            bleJsonBuffer += ",";

            bleJsonBuffer += "\"advi\":";
            bleJsonBuffer += std::to_string(advItvl);
            bleJsonBuffer += ",";

            int i2 = 0;
            bleJsonBuffer += "\"srv\":[";
            if (bleDevs[i].services != nullptr)
            {
                for (NimBLERemoteService *service : *bleDevs[i].services)
                {
                    if (i2++ > 0)
                        bleJsonBuffer += ",";

                    bleJsonBuffer += "{";

                    bleJsonBuffer += "\"id\":\"";
                    bleJsonBuffer += service->getUUID().toString().c_str();
                    bleJsonBuffer += "\",";

                    bleJsonBuffer += "\"name\":\"";
                    bleJsonBuffer += getNameFromUUID(service->getUUID()).c_str();
                    bleJsonBuffer += "\",";

                    int i3 = 0;
                    bleJsonBuffer += "\"chrs\":[";
                    std::vector<NimBLERemoteCharacteristic *> *characteristics = service->getCharacteristics();
                    if (characteristics != nullptr)
                    {
                        for (NimBLERemoteCharacteristic *characteristic : *characteristics)
                        {
                            uint8_t chrProp = getCharacteristicProperties(characteristic);
                            if (i3++ > 0)
                                bleJsonBuffer += ",";

                            bleJsonBuffer += "{";

                            bleJsonBuffer += "\"id\":\"";
                            bleJsonBuffer += characteristic->getUUID().toString().c_str();
                            bleJsonBuffer += "\",";

                            bleJsonBuffer += "\"name\":\"";
                            bleJsonBuffer += getNameFromUUID(characteristic->getUUID()).c_str();
                            bleJsonBuffer += "\",";

                            bleJsonBuffer += "\"prop\":";
                            bleJsonBuffer += std::to_string(chrProp);

                            bleJsonBuffer += "}";
                        }
                    }
                    bleJsonBuffer += "]";
                    bleJsonBuffer += "}";
                }
            }
            bleJsonBuffer += "],";

            bleJsonBuffer += "\"con\":"; // Connection status
            bleJsonBuffer += (bleDevs[i].isConnected() ? "true" : "false");

            bleJsonBuffer += "}";

            sets++;
        }
    }
    bleJsonBuffer += "]";

    bleJsonBuffer += "}";
}

void addDirectoryJson(File dir)
{
    if (!dir)
        return;

    bool isFirst = true;
    while (true)
    {
        File entry = dir.openNextFile();
        if (!entry)
            break;

        if (!isFirst)
            fsJsonBuffer += ",";
        isFirst = false;

        fsJsonBuffer += "{";
        fsJsonBuffer += "\"name\":\"";
        fsJsonBuffer += entry.name();
        fsJsonBuffer += "\",";
        fsJsonBuffer += "\"isDirectory\":";
        fsJsonBuffer += (entry.isDirectory() ? "true" : "false");
        fsJsonBuffer += ",";

        if (entry.isDirectory())
        {
            fsJsonBuffer += "\"size\":0,";
            fsJsonBuffer += "\"sub\":[";
            addDirectoryJson(entry);
            fsJsonBuffer += "]";
        }
        else
        {
            fsJsonBuffer += "\"size\":";
            fsJsonBuffer += std::to_string(entry.size());
            fsJsonBuffer += ",";
            fsJsonBuffer += "\"sub\":[]";
        }
        fsJsonBuffer += "}";
        entry.close();
    }
}

bool updateTime(uint8_t retrys)
{
    log_d(">> Get time from NTP '%s'...", NTP_URL);
    struct tm timeinfo;

    configTime(0, 0, NTP_URL);
    if (!getLocalTime(&timeinfo))
    {
        if (retrys > 0)
        {
            log_e("-- Failed to retrieve time from NTP! Retry...");
            delay(1000);
            updateTime(--retrys);
        }
        else
        {
            log_e("+ Failed to retrieve time from NTP! Abort!");
            return false;
        }
    }
    else
    {
        log_d("+ Time successfully retrieved from NTP.");
        return true;
    }

    return false;
}

#pragma region GPIO
void initGPIOs()
{
    char configName[13];
    char defaultStr[6];
    char resultStr[6];

    for (uint8_t i = 0; i < gpios_len; i++)
    {
        sprintf(defaultStr, "%u", INPUT);
        sprintf(configName, "GPIO%u_MODE", gpios[i].pin);

        if (readConfigValue(configName, defaultStr, resultStr) > 0)
        {
            uint8_t mode = strtoul(resultStr, NULL, 10);
            gpios[i].mode = mode;
        }

        gpios[i].canOutput = digitalPinCanOutput(gpios[i].pin);
        int value = 0;

        if (gpios[i].mode == OUTPUT)
        {
            if (!gpios[i].canOutput)
            {
                gpios[i].mode = INPUT;
                Serial.printf("Pin %u could not set as OUTPUT!\n", gpios[i].pin);
            }
            else
            {
                sprintf(defaultStr, "%u", 0);
                sprintf(configName, "GPIO%u_VAL", gpios[i].pin);

                if (readConfigValue(configName, defaultStr, resultStr) > 0)
                {
                    value = strtol(resultStr, NULL, 10);
                }
            }
        }

        if (gpios[i].isFree)
        {
            pinMode(gpios[i].pin, gpios[i].mode);
            if (gpios[i].mode == OUTPUT)
                setPinValue(gpios[i].pin, value);
        }
        else
        {
            gpios[i].value = -1;
        }
    }
}

bool isDigitalPin(GPIOPin gpio)
{
    return gpio.type == 'D';
}

bool setPinMode(uint8_t pinNo, uint8_t mode)
{
    for (uint8_t i = 0; i < gpios_len; i++)
    {
        if (gpios[i].pin == pinNo)
        {
            if (!gpios[i].isFree)
            {
                Serial.printf("Pin %u is not free!\n", gpios[i].pin);
                return false;
            }

            if ((mode == OUTPUT && gpios[i].canOutput) || mode != OUTPUT)
            {
                pinMode(pinNo, mode);
                gpios[i].mode = mode;
                Serial.printf("Pin %u set mode %u.\n", gpios[i].pin, mode);
                return true;
            }
            else
            {
                if ((mode == OUTPUT && !gpios[i].canOutput))
                    Serial.printf("Pin %u has no output!\n", gpios[i].pin);
                else
                    Serial.printf("Pin %u invalid mode %u!\n", gpios[i].pin, mode);

                return false;
            }
        }
    }
    Serial.printf("Pin %u not found!\n", pinNo);
    return false;
}

void setPinValue(uint8_t pinNo, int value)
{
    for (uint8_t i = 0; i < gpios_len; i++)
    {
        if (gpios[i].pin == pinNo)
        {
            if (!gpios[i].canOutput)
            {
                Serial.printf("Pin %u has no output!\n", gpios[i].pin);
                return;
            }
            else if (gpios[i].mode != OUTPUT)
            {
                Serial.printf("Pin %u mode is not output!\n", gpios[i].pin);
                return;
            }
            else if (!gpios[i].isFree)
            {
                Serial.printf("Pin %u is not free!\n", gpios[i].pin);
                return;
            }

            if (isDigitalPin(gpios[i]))
            {
                digitalWrite(pinNo, (value == 0 ? LOW : HIGH));
                gpios[i].value = (value == 0 ? LOW : HIGH);
            }
            else
            {
                analogWrite(pinNo, value);
                gpios[i].value = value;
            }

            Serial.printf("Pin %u wrote value %i.\n", gpios[i].pin, value);
            return;
        }
    }
    Serial.printf("Pin %u not found!\n", pinNo);
}

bool checkGPIOs()
{
    bool somethingChanged = false;
    for (uint8_t i = 0; i < gpios_len; i++)
    {
        if (gpios[i].isFree && gpios[i].mode != OUTPUT)
        {
            int oldValue = gpios[i].value;

            if (isDigitalPin(gpios[i]))
                gpios[i].value = digitalRead(gpios[i].pin);
            else
                gpios[i].value = analogRead(gpios[i].pin);

            if (gpios[i].value != oldValue)
                somethingChanged = true;
        }
    }

    return somethingChanged;
}
#pragma endregion GPIO

int getWiFiStrength()
{
#ifdef USE_ETH
    return 0;
#else
    long rssi = 0;

    for (uint8_t i = 0; i < 4; i++)
    {
        rssi += WiFi.RSSI();
        delay(20);
    }

    return rssi / 4;
#endif
}

size_t readConfigValue(char *cName, char *defaultVal, char *valOut)
{
    const uint8_t bufferSize = 64; // 64 bytes size should be sufficient
    char buffer[bufferSize];

    bool toWrite = false;

    File configFile = SD_LIB.open(CONFIG_FILE_NAME, FILE_READ);
    if (configFile)
    {
        Serial.printf("Reading configuration file...\n");
        while (configFile.available())
        {
            // Read line
            size_t readSize = configFile.readBytesUntil('\n', buffer, bufferSize);

            // Set null terminator
            if (readSize < bufferSize)
                buffer[readSize] = '\0';

            // Check if line is empty
            if (buffer[0] == 0)
                continue;

            // Parse name and value
            char *name = strtok(buffer, "=");
            char *value = strtok(NULL, "\0");

            size_t valLen = trim(value);

            if (valLen <= 0)
                continue;

            if (strcmp(name, cName) == 0)
            {
                strcpy(valOut, value);
                valOut[valLen] = 0;
                configFile.close();
                return valLen;
            }
        }
        configFile.close();
        Serial.printf("Config name not found!\n");
        toWrite = true;
    }
    else if (SD_LIB.cardType() == CARD_NONE)
    {
        Serial.printf("No SD card attached! Loading default config!\n");
    }
    else if (!SD_LIB.exists(CONFIG_FILE_NAME))
    {
        Serial.printf("Configuration file does not exist! Loading default config!\n");
        toWrite = true;
    }
    else
    {
        Serial.printf("Unknown error opening configuration file! Loading default config!\n");
    }

    size_t valLen = trim(defaultVal);

    if (toWrite)
    {
        configFile = SD_LIB.open(CONFIG_FILE_NAME, FILE_APPEND);
        if (configFile)
        {
            configFile.printf("%s=%s\n", cName, defaultVal);
            configFile.close();
            Serial.printf("Configuration written.\n");
        }
        else
        {
            Serial.printf("Error creating configuration file!\n");
        }
    }

    strcpy(valOut, defaultVal);
    valOut[valLen] = 0;
    return valLen;
}

void updateConfigValue(const char *sName, const char *sValue)
{
    const uint8_t bufferSize = 64; // 64 bytes size should be sufficient
    char buffer[bufferSize];

    File configFile = SD_LIB.open(CONFIG_FILE_NAME);
    if (!configFile)
    {
        Serial.printf("Error opening gw config file! Couldn't write value update!\n");
        return;
    }

    File tempFile = SD_LIB.open(TEMP_CONFIG_FILE_NAME, FILE_WRITE);
    if (!tempFile)
    {
        Serial.printf("Temporary file '%s' couldn't be created!\n", TEMP_CONFIG_FILE_NAME);
        return;
    }
    Serial.printf("Updating configuration file...\n");

    bool updated = false;

    while (configFile.available())
    {
        // Read setting name
        size_t readSize = configFile.readBytesUntil('=', buffer, bufferSize);

        // Write to temp file
        tempFile.write((uint8_t *)buffer, readSize);
        tempFile.write('=');

        // Set null terminator
        if (readSize < bufferSize)
            buffer[readSize] = '\0';

        // Check if line is empty
        if (buffer[0] == 0)
            continue;

        if (!updated && strcmp(buffer, sName) == 0)
        {
            Serial.printf("Set value of '%s' to '%s'.\n", buffer, sValue);

            // Set sValue
            tempFile.write((uint8_t *)sValue, strlen(sValue));
            tempFile.write('\n');

            // Read the rest of the line
            readSize = configFile.readBytesUntil('\n', buffer, bufferSize);

            updated = true;
        }
        else
        {
            // Read the rest of the line
            readSize = configFile.readBytesUntil('\n', buffer, bufferSize);

            // Write to temp file
            tempFile.write((uint8_t *)buffer, readSize);
            tempFile.write('\n');
        }
    }

    if (!updated)
    {
        // Add setting to config file
        tempFile.write((uint8_t *)sName, strlen(sName));
        tempFile.write('=');
        tempFile.write((uint8_t *)sValue, strlen(sValue));
        tempFile.write('\n');
    }

    configFile.close();
    tempFile.close();

    // Delete source file
    if (SD_LIB.remove(CONFIG_FILE_NAME))
    {
        // Rename temp file
        if (SD_LIB.rename(TEMP_CONFIG_FILE_NAME, CONFIG_FILE_NAME))
        {
            Serial.printf("Configuration update successful.\n");
        }
        else
        {
            Serial.printf("Couldn't rename temporary configuration file! Configuration update failed!\n");
        }
    }
    else
    {
        Serial.printf("Couldn't delete old configuration file! Configuration update failed!\n");
    }
}

#pragma region BLE
bool connectBLE(uint8_t index)
{
    if (!bleDevs[index].isConnected())
    {
        isBleConnecting = true;
        bleDevs[index].pClient = NimBLEDevice::createClient();
        bleDevs[index].pClient->setClientCallbacks(new MyClientCallback(), false);
        bleDevs[index].pClient->setConnectTimeout(3);

        if (bleDevs[index].pClient->connect(&bleDevs[index].advertisedDevice))
        {
            Serial.println("Connected to device, discovering services and characteristics...");

            // Discover services
            bleDevs[index].services = bleDevs[index].pClient->getServices(true);
            for (NimBLERemoteService *service : *bleDevs[index].services)
            {
                service->getCharacteristics(true);
            }
            isBleConnecting = false;
            return true;
        }
        else
        {
            Serial.println("Failed to connect to the device");
        }
        isBleConnecting = false;
        return false;
    }
    else
    {
        Serial.println("BLEDev is already connected!");
    }
    isBleConnecting = false;
    return false;
}

bool disconnectBLE(uint8_t index)
{
    if (bleDevs[index].isConnected())
    {
        Serial.printf("disconnectBLE(%u);\n", index);
        int rc = bleDevs[index].pClient->disconnect();
        if (rc == 0)
            return true;

        Serial.printf("Disconnect BLE device returned error code %i.\n", rc);
        return false;
    }
    else
    {
        return false;
    }
    return false;
}

void startBleScan()
{
    if (isBleScanActive || isBleConnecting)
        return;

    Serial.println("Start BLE scan...");
    pBLEScan->setActiveScan(true);
    isBleScanActive = pBLEScan->start(bleScanSeconds, onBLEScanComplete, false);
}

static void onBLEScanComplete(NimBLEScanResults scanResults)
{
    Serial.println("BLE scan complete.");
    isBleScanActive = false;

    int resultCount = scanResults.getCount();
    int nextFreeIndex = getUnsetBLEDevIndex();

    // reset found flag in bleDevs
    for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
        bleDevs[i].found = false;

    for (uint8_t i2 = 0; i2 < resultCount; i2++)
    {
        NimBLEAdvertisedDevice advBLEDev = scanResults.getDevice(i2);
        bool listed = false;
        for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
        {
            if (bleDevs[i].advertisedDevice.getAddress() == advBLEDev.getAddress())
            {
                listed = true;
                bleDevs[i].found = true;
                break;
            }
        }

        if (nextFreeIndex < 0 || nextFreeIndex >= CONFIG_BT_NIMBLE_MAX_CONNECTIONS)
        {
            Serial.println("Max BLE devices reached!");
        }
        else if (!listed)
        {
            bleDevs[nextFreeIndex].advertisedDevice = advBLEDev;
            bleDevs[nextFreeIndex].found = true;
            nextFreeIndex = getUnsetBLEDevIndex();
        }
    }

    // Clear not found devices
    for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
    {
        if (!bleDevs[i].found && bleDevs[i].isSet())
            resetBLEDev(i);
    }
}

void disconnectAndDeleteBleDevClient(uint8_t index)
{
    disconnectBLE(index);
    bleDevs[index].services = nullptr;
    if (bleDevs[index].pClient != nullptr)
    {
        NimBLEDevice::deleteClient(bleDevs[index].pClient);
        bleDevs[index].pClient = nullptr;
    }
}

void resetBLEDev(uint8_t index)
{
    Serial.printf("resetBLEDev(%u);\n", index);
    bleDevs[index].toDisconnect = false;
    bleDevs[index].toConnect = false;
    bleDevs[index].found = false;

    disconnectAndDeleteBleDevClient(index);

    Serial.printf("Deleting advertised device...\n");
    bleDevs[index].advertisedDevice = NimBLEAdvertisedDevice();
}

int getUnsetBLEDevIndex()
{
    for (uint8_t i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++)
    {
        if (!bleDevs[i].isSet())
            return i;
    }
    return -1;
}

String getNameFromUUID(const NimBLEUUID &uuid)
{
    auto it = uuidMap.find(uuid.toString());
    if (it != uuidMap.end())
    {
        return it->second.c_str();
    }
    else
    {
        return "Unknown";
    }
}

uint8_t getCharacteristicProperties(NimBLERemoteCharacteristic *chr)
{
    uint8_t chrProp = 0;
    if (chr->canBroadcast())
        chrProp |= BLE_GATT_CHR_PROP_BROADCAST;
    if (chr->canIndicate())
        chrProp |= BLE_GATT_CHR_PROP_INDICATE;
    if (chr->canNotify())
        chrProp |= BLE_GATT_CHR_PROP_NOTIFY;
    if (chr->canRead())
        chrProp |= BLE_GATT_CHR_PROP_READ;
    if (chr->canWrite())
        chrProp |= BLE_GATT_CHR_PROP_WRITE;
    if (chr->canWriteNoResponse())
        chrProp |= BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    return chrProp;
}
#pragma endregion BLE

String decodeURL(String data)
{
    const char *leader = data.c_str();
    char *follower = (char *)leader;

    while (*leader)
    {
        if (*leader == '%')
        {
            leader++;
            char high = *leader;
            leader++;
            char low = *leader;

            if (high >= '0' && high <= '9')
                high -= '0';
            else if (high >= 'A' && high <= 'F')
                high = high - 'A' + 10;
            high &= 0x0f;

            if (low >= '0' && low <= '9')
                low -= '0';
            else if (low >= 'A' && low <= 'F')
                low = low - 'A' + 10;
            low &= 0x0f;

            *follower = (high + 4) | low;
        }
        else
        {
            *follower = *leader;
        }

        leader++;
        follower++;
    }
    *follower = '\0';

    size_t len = follower - data.c_str();

    return String(data.c_str(), len);
}

size_t trim(char *value)
{
    if (value == nullptr)
        return 0;

    size_t valLen = strlen(value);

    while (valLen > 0 && (value[valLen - 1] == ' ' || value[valLen - 1] == '\t' || value[valLen - 1] == '\n' || value[valLen - 1] == '\r'))
    {
        value[valLen - 1] = '\0';
        valLen--;
    }

    return valLen;
}