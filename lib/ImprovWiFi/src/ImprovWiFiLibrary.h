#pragma once

#if defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #define WIFI_OPEN ENC_TYPE_NONE
#elif defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #define WIFI_OPEN WIFI_AUTH_OPEN
#endif

#include <Stream.h>
#include "ImprovTypes.h"

#ifdef ARDUINO
  #include <Arduino.h>
#endif

/**
 * Improv WiFi class
 *
 * ### Description
 *
 * Handles the Improv WiFi Serial protocol (https://www.improv-wifi.com/serial/)
 *
 * ### Example
 *
 * Simple example of using ImprovWiFi lib. A complete one can be seen in `examples/` folder.
 *
 * ```cpp
 * #include <ImprovWiFiLibrary.h>
 *
 * ImprovWiFi improvSerial(&Serial);
 *
 * void setup() {
 *   improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "My-Device-9a4c2b", "2.1.5", "My Device");
 * }
 *
 * void loop() {
 *   improvSerial.handleSerial();
 * }
 * ```
 *
 */
class ImprovWiFi
{
private:
  const char *const CHIP_FAMILY_DESC[5] = {"ESP32", "ESP32-C3", "ESP32-S2", "ESP32-S3", "ESP8266"};
  ImprovTypes::ImprovWiFiParamsStruct improvWiFiParams;

  uint8_t _buffer[128];
  uint8_t _position = 0;

  Stream *serial;

  void sendDeviceUrl(ImprovTypes::Command cmd);
  bool onCommandCallback(ImprovTypes::ImprovCommand cmd);
  void onErrorCallback(ImprovTypes::Error err);
  void setState(ImprovTypes::State state);
  void sendResponse(std::vector<uint8_t> &response);
  void setError(ImprovTypes::Error error);
  void getAvailableWifiNetworks();
  inline void replaceAll(std::string &str, const std::string &from, const std::string &to);

  // improv SDK
  bool parseImprovSerial(size_t position, uint8_t byte, const uint8_t *buffer);
  ImprovTypes::ImprovCommand parseImprovData(const std::vector<uint8_t> &data, bool check_checksum = true);
  ImprovTypes::ImprovCommand parseImprovData(const uint8_t *data, size_t length, bool check_checksum = true);
  std::vector<uint8_t> build_rpc_response(ImprovTypes::Command command, const std::vector<std::string> &datum, bool add_checksum);

public:
  /**
   * ## Constructors
   **/

  /**
   * Create an instance of ImprovWiFi
   *
   * # Parameters
   *
   * - `serial` - Pointer to stream object used to handle requests, for the most cases use `Serial`
   */
  ImprovWiFi(Stream *serial)
  {
    this->serial = serial;
  }

  /**
   * ## Type definition
   */

  /**
   * Callback function called when any error occurs during the protocol handling or wifi connection.
   */
  typedef void(OnImprovError)(ImprovTypes::Error);

  /**
   * Callback function called when the attempt of wifi connection is successful. It informs the SSID and Password used to that, it's a perfect time to save them for further use.
   */
  typedef void(OnImprovConnected)(const char *ssid, const char *password);

  /**
   * Callback function to customize the wifi connection if you needed. Optional.
   */
  typedef bool(CustomConnectWiFi)(const char *ssid, const char *password);

  /**
   * ## Methods
   **/

  /**
   * Check if a communication via serial is happening. Put this call on your loop().
   *
   */
  void handleSerial();

  /**
   * Set details of your device.
   *
   * # Parameters
   *
   * - `chipFamily` - Chip variant, supported are CF_ESP32, CF_ESP32_C3, CF_ESP32_S2, CF_ESP32_S3, CF_ESP8266. Consult ESP Home [docs](https://esphome.io/components/esp32.html) for more information.
   * - `firmwareName` - Firmware name
   * - `firmwareVersion` - Firmware version
   * - `deviceName` - Your device name
   * - `deviceUrl`- The local URL to access your device. A placeholder called {LOCAL_IPV4} is available to form elaboreted URLs. E.g. `http://{LOCAL_IPV4}?name=Guest`.
   *   There is overloaded method without `deviceUrl`, in this case the URL will be the local IP.
   *
   */
  void setDeviceInfo(ImprovTypes::ChipFamily chipFamily, const char *firmwareName, const char *firmwareVersion, const char *deviceName, const char *deviceUrl);
  void setDeviceInfo(ImprovTypes::ChipFamily chipFamily, const char *firmwareName, const char *firmwareVersion, const char *deviceName);

  /**
   * Method to set the typedef OnImprovError callback.
   */
  void onImprovError(OnImprovError *errorCallback);

  /**
   * Method to set the typedef OnImprovConnected callback.
   */
  void onImprovConnected(OnImprovConnected *connectedCallback);

  /**
   * Method to set the typedef CustomConnectWiFi callback.
   */
  void setCustomConnectWiFi(CustomConnectWiFi *connectWiFiCallBack);

  /**
   * Default method to connect in a WiFi network.
   * It waits `DELAY_MS_WAIT_WIFI_CONNECTION` milliseconds (default 500) during `MAX_ATTEMPTS_WIFI_CONNECTION` (default 20) until it get connected. If it does not happen, an error `ERROR_UNABLE_TO_CONNECT` is thrown.
   *
   */
  bool tryConnectToWifi(const char *ssid, const char *password);

  /**
   * Check if connection is established using `WiFi.status() == WL_CONNECTED`
   *
   */
  bool isConnected();

private:
  OnImprovError *onImproErrorCallback;
  OnImprovConnected *onImprovConnectedCallback;
  CustomConnectWiFi *customConnectWiFiCallback;
};
