#include "ImprovWiFiLibrary.h"

void ImprovWiFi::handleSerial()
{

  if (serial->available() > 0)
  {
    uint8_t b = serial->read();

    if (parseImprovSerial(_position, b, _buffer))
    {
      _buffer[_position++] = b;
    }
    else
    {
      _position = 0;
    }
  }
}

void ImprovWiFi::onErrorCallback(ImprovTypes::Error err)
{
  if (onImproErrorCallback)
  {
    onImproErrorCallback(err);
  }
}

bool ImprovWiFi::onCommandCallback(ImprovTypes::ImprovCommand cmd)
{

  switch (cmd.command)
  {
  case ImprovTypes::Command::GET_CURRENT_STATE:
  {
    if (isConnected())
    {
      setState(ImprovTypes::State::STATE_PROVISIONED);
      sendDeviceUrl(cmd.command);
    }
    else
    {
      setState(ImprovTypes::State::STATE_AUTHORIZED);
    }

    break;
  }

  case ImprovTypes::Command::WIFI_SETTINGS:
  {

    if (cmd.ssid.empty())
    {
      setError(ImprovTypes::Error::ERROR_INVALID_RPC);
      break;
    }

    setState(ImprovTypes::STATE_PROVISIONING);

    bool success = false;

    if (customConnectWiFiCallback)
    {
      success = customConnectWiFiCallback(cmd.ssid.c_str(), cmd.password.c_str());
    }
    else
    {
      success = tryConnectToWifi(cmd.ssid.c_str(), cmd.password.c_str());
    }

    if (success)
    {
      setError(ImprovTypes::Error::ERROR_NONE);
      setState(ImprovTypes::STATE_PROVISIONED);
      sendDeviceUrl(cmd.command);
      if (onImprovConnectedCallback)
      {
        onImprovConnectedCallback(cmd.ssid.c_str(), cmd.password.c_str());
      }
    }
    else
    {
      setState(ImprovTypes::STATE_STOPPED);
      setError(ImprovTypes::ERROR_UNABLE_TO_CONNECT);
      onErrorCallback(ImprovTypes::ERROR_UNABLE_TO_CONNECT);
    }

    break;
  }

  case ImprovTypes::Command::GET_DEVICE_INFO:
  {
    std::vector<std::string> infos = {
        // Firmware name
        improvWiFiParams.firmwareName,
        // Firmware version
        improvWiFiParams.firmwareVersion,
        // Hardware chip/variant
        CHIP_FAMILY_DESC[improvWiFiParams.chipFamily],
        // Device name
        improvWiFiParams.deviceName};
    std::vector<uint8_t> data = build_rpc_response(ImprovTypes::GET_DEVICE_INFO, infos, false);
    sendResponse(data);
    break;
  }

  case ImprovTypes::Command::GET_WIFI_NETWORKS:
  {
    getAvailableWifiNetworks();
    break;
  }

  default:
  {
    setError(ImprovTypes::ERROR_UNKNOWN_RPC);
    return false;
  }
  }

  return true;
}
void ImprovWiFi::setDeviceInfo(ImprovTypes::ChipFamily chipFamily, const char *firmwareName, const char *firmwareVersion, const char *deviceName)
{
  improvWiFiParams.chipFamily = chipFamily;
  improvWiFiParams.firmwareName = firmwareName;
  improvWiFiParams.firmwareVersion = firmwareVersion;
  improvWiFiParams.deviceName = deviceName;
}
void ImprovWiFi::setDeviceInfo(ImprovTypes::ChipFamily chipFamily, const char *firmwareName, const char *firmwareVersion, const char *deviceName, const char *deviceUrl)
{
  setDeviceInfo(chipFamily, firmwareName, firmwareVersion, deviceName);
  improvWiFiParams.deviceUrl = deviceUrl;
}

bool ImprovWiFi::isConnected()
{
  return (WiFi.status() == WL_CONNECTED);
}

void ImprovWiFi::sendDeviceUrl(ImprovTypes::Command cmd)
{
  // URL where user can finish onboarding or use device
  // Recommended to use website hosted by device

  const IPAddress address = WiFi.localIP();
  char buffer[16];
  sprintf(buffer, "%d.%d.%d.%d", address[0], address[1], address[2], address[3]);
  std::string ipStr = std::string{buffer};

  if (improvWiFiParams.deviceUrl.empty())
  {
    improvWiFiParams.deviceUrl = "http://" + ipStr;
  }
  else
  {
    replaceAll(improvWiFiParams.deviceUrl, "{LOCAL_IPV4}", ipStr);
  }

  std::vector<uint8_t> data = build_rpc_response(cmd, {improvWiFiParams.deviceUrl}, false);
  sendResponse(data);
}

void ImprovWiFi::onImprovError(OnImprovError *errorCallback)
{
  onImproErrorCallback = errorCallback;
}

void ImprovWiFi::onImprovConnected(OnImprovConnected *connectedCallback)
{
  onImprovConnectedCallback = connectedCallback;
}

void ImprovWiFi::setCustomConnectWiFi(CustomConnectWiFi *connectWiFiCallBack)
{
  customConnectWiFiCallback = connectWiFiCallBack;
}

bool ImprovWiFi::tryConnectToWifi(const char *ssid, const char *password)
{
  uint8_t count = 0;

  if (isConnected())
  {
    WiFi.disconnect();
    delay(100);
  }

  WiFi.begin(ssid, password);

  while (!isConnected())
  {
    delay(DELAY_MS_WAIT_WIFI_CONNECTION);
    if (count > MAX_ATTEMPTS_WIFI_CONNECTION)
    {
      WiFi.disconnect();
      return false;
    }
    count++;
  }

  return true;
}

void ImprovWiFi::getAvailableWifiNetworks()
{
  int networkNum = WiFi.scanNetworks();

  for (int id = 0; id < networkNum; ++id)
  {
    std::vector<std::string> wifinetworks = {
        WiFi.SSID(id).c_str(),
        std::to_string(WiFi.RSSI(id)),
        ( WiFi.encryptionType(id) == WIFI_OPEN ? "NO" : "YES")
    };

    std::vector<uint8_t> data = build_rpc_response(
        ImprovTypes::GET_WIFI_NETWORKS, wifinetworks, false);
    sendResponse(data);
    delay(1);
  }
  // final response
  std::vector<uint8_t> data =
      build_rpc_response(ImprovTypes::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
  sendResponse(data);
}

inline void ImprovWiFi::replaceAll(std::string &str, const std::string &from, const std::string &to)
{
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos)
  {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

bool ImprovWiFi::parseImprovSerial(size_t position, uint8_t byte, const uint8_t *buffer)
{
  if (position == 0)
    return byte == 'I';
  if (position == 1)
    return byte == 'M';
  if (position == 2)
    return byte == 'P';
  if (position == 3)
    return byte == 'R';
  if (position == 4)
    return byte == 'O';
  if (position == 5)
    return byte == 'V';

  if (position == 6)
    return byte == ImprovTypes::IMPROV_SERIAL_VERSION;

  if (position <= 8)
    return true;

  uint8_t type = buffer[7];
  uint8_t data_len = buffer[8];

  if (position <= 8 + data_len)
    return true;

  if (position == 8 + data_len + 1)
  {
    uint8_t checksum = 0x00;
    for (size_t i = 0; i < position; i++)
      checksum += buffer[i];

    if (checksum != byte)
    {
      _position = 0;
      onErrorCallback(ImprovTypes::Error::ERROR_INVALID_RPC);
      return false;
    }

    if (type == ImprovTypes::ImprovSerialType::TYPE_RPC)
    {
      _position = 0;
      auto command = parseImprovData(&buffer[9], data_len, false);
      return onCommandCallback(command);
    }
  }

  return false;
}

ImprovTypes::ImprovCommand ImprovWiFi::parseImprovData(const std::vector<uint8_t> &data, bool check_checksum)
{
  return parseImprovData(data.data(), data.size(), check_checksum);
}

ImprovTypes::ImprovCommand ImprovWiFi::parseImprovData(const uint8_t *data, size_t length, bool check_checksum)
{
  ImprovTypes::ImprovCommand improv_command;
  ImprovTypes::Command command = (ImprovTypes::Command)data[0];
  uint8_t data_length = data[1];

  if (data_length != length - 2 - check_checksum)
  {
    improv_command.command = ImprovTypes::Command::UNKNOWN;
    return improv_command;
  }

  if (check_checksum)
  {
    uint8_t checksum = data[length - 1];

    uint32_t calculated_checksum = 0;
    for (uint8_t i = 0; i < length - 1; i++)
    {
      calculated_checksum += data[i];
    }

    if ((uint8_t)calculated_checksum != checksum)
    {
      improv_command.command = ImprovTypes::Command::BAD_CHECKSUM;
      return improv_command;
    }
  }

  if (command == ImprovTypes::Command::WIFI_SETTINGS)
  {
    uint8_t ssid_length = data[2];
    uint8_t ssid_start = 3;
    size_t ssid_end = ssid_start + ssid_length;

    uint8_t pass_length = data[ssid_end];
    size_t pass_start = ssid_end + 1;
    size_t pass_end = pass_start + pass_length;

    std::string ssid(data + ssid_start, data + ssid_end);
    std::string password(data + pass_start, data + pass_end);
    return {.command = command, .ssid = ssid, .password = password};
  }

  improv_command.command = command;
  return improv_command;
}

void ImprovWiFi::setState(ImprovTypes::State state)
{

  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(11);
  data[6] = ImprovTypes::IMPROV_SERIAL_VERSION;
  data[7] = ImprovTypes::TYPE_CURRENT_STATE;
  data[8] = 1;
  data[9] = state;

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data[10] = checksum;

  serial->write(data.data(), data.size());
}

void ImprovWiFi::setError(ImprovTypes::Error error)
{
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(11);
  data[6] = ImprovTypes::IMPROV_SERIAL_VERSION;
  data[7] = ImprovTypes::TYPE_ERROR_STATE;
  data[8] = 1;
  data[9] = error;

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data[10] = checksum;

  serial->write(data.data(), data.size());
}

void ImprovWiFi::sendResponse(std::vector<uint8_t> &response)
{
  std::vector<uint8_t> data = {'I', 'M', 'P', 'R', 'O', 'V'};
  data.resize(9);
  data[6] = ImprovTypes::IMPROV_SERIAL_VERSION;
  data[7] = ImprovTypes::TYPE_RPC_RESPONSE;
  data[8] = response.size();
  data.insert(data.end(), response.begin(), response.end());

  uint8_t checksum = 0x00;
  for (uint8_t d : data)
    checksum += d;
  data.push_back(checksum);

  serial->write(data.data(), data.size());
}

std::vector<uint8_t> ImprovWiFi::build_rpc_response(ImprovTypes::Command command, const std::vector<std::string> &datum, bool add_checksum)
{
  std::vector<uint8_t> out;
  uint32_t length = 0;
  out.push_back(command);
  for (const auto &str : datum)
  {
    uint8_t len = str.length();
    length += len + 1;
    out.push_back(len);
    out.insert(out.end(), str.begin(), str.end());
  }
  out.insert(out.begin() + 1, length);

  if (add_checksum)
  {
    uint32_t calculated_checksum = 0;

    for (uint8_t byte : out)
    {
      calculated_checksum += byte;
    }
    out.push_back(calculated_checksum);
  }
  return out;
}