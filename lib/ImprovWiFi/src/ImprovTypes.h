#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif



#ifndef MAX_ATTEMPTS_WIFI_CONNECTION
  #define MAX_ATTEMPTS_WIFI_CONNECTION 20
#endif

#ifndef DELAY_MS_WAIT_WIFI_CONNECTION
  #define DELAY_MS_WAIT_WIFI_CONNECTION 500
#endif


#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ImprovTypes {


enum Error : uint8_t {
  ERROR_NONE = 0x00,
  ERROR_INVALID_RPC = 0x01,
  ERROR_UNKNOWN_RPC = 0x02,
  ERROR_UNABLE_TO_CONNECT = 0x03,
  ERROR_NOT_AUTHORIZED = 0x04,
  ERROR_UNKNOWN = 0xFF,
};

enum State : uint8_t {
  STATE_STOPPED = 0x00,
  STATE_AWAITING_AUTHORIZATION = 0x01,
  STATE_AUTHORIZED = 0x02,
  STATE_PROVISIONING = 0x03,
  STATE_PROVISIONED = 0x04,
};

enum Command : uint8_t {
  UNKNOWN = 0x00,
  WIFI_SETTINGS = 0x01,
  IDENTIFY = 0x02,
  GET_CURRENT_STATE = 0x02,
  GET_DEVICE_INFO = 0x03,
  GET_WIFI_NETWORKS = 0x04,
  BAD_CHECKSUM = 0xFF,
};

static const uint8_t CAPABILITY_IDENTIFY = 0x01;
static const uint8_t IMPROV_SERIAL_VERSION = 1;

enum ImprovSerialType : uint8_t {
  TYPE_CURRENT_STATE = 0x01,
  TYPE_ERROR_STATE = 0x02,
  TYPE_RPC = 0x03,
  TYPE_RPC_RESPONSE = 0x04
};

struct ImprovCommand {
  Command command;
  std::string ssid;
  std::string password;
};

enum ChipFamily : uint8_t {
  CF_ESP32,
  CF_ESP32_C3,
  CF_ESP32_S2,
  CF_ESP32_S3,
  CF_ESP8266
};

struct ImprovWiFiParamsStruct {
  std::string firmwareName;
  std::string firmwareVersion;
  ChipFamily chipFamily;
  std::string deviceName;
  std::string deviceUrl;
};


} 