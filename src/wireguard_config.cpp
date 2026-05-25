#include "wireguard_config.h"
#include <string.h>
#include <ctype.h>

// ============================================================================
//  Base64 Codec (for key decoding/encoding)
// ============================================================================

static const char BASE64_TABLE[] = 
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64Decode(const char* encoded, uint8_t* decoded, size_t maxLen) {
  if (!encoded || !decoded || maxLen == 0) return 0;

  size_t outIdx = 0;
  int v = 0;
  int bits = 0;

  for (int i = 0; encoded[i] && outIdx < maxLen; i++) {
    char c = encoded[i];

    // Skip whitespace and newlines
    if (isspace(c)) continue;

    // Padding marks end
    if (c == '=') break;

    // Lookup character in base64 table
    int idx = -1;
    for (int j = 0; j < 64; j++) {
      if (BASE64_TABLE[j] == c) {
        idx = j;
        break;
      }
    }

    if (idx == -1) return 0;  // invalid character

    v = (v << 6) | idx;
    bits += 6;

    if (bits >= 8) {
      bits -= 8;
      decoded[outIdx++] = (v >> bits) & 0xFF;
    }
  }

  return outIdx;
}

size_t base64Encode(const uint8_t* raw, size_t rawLen, char* encoded, size_t maxLen) {
  if (!raw || !encoded || maxLen < 1) return 0;

  size_t outIdx = 0;
  const size_t reqLen = ((rawLen + 2) / 3) * 4 + 1;
  if (maxLen < reqLen) return 0;

  for (size_t i = 0; i < rawLen; i += 3) {
    uint32_t v = 0;
    int bytes = rawLen - i;

    v = raw[i] << 16;
    if (bytes > 1) v |= raw[i + 1] << 8;
    if (bytes > 2) v |= raw[i + 2];

    encoded[outIdx++] = BASE64_TABLE[(v >> 18) & 0x3F];
    encoded[outIdx++] = BASE64_TABLE[(v >> 12) & 0x3F];
    encoded[outIdx++] = (bytes > 1) ? BASE64_TABLE[(v >> 6) & 0x3F] : '=';
    encoded[outIdx++] = (bytes > 2) ? BASE64_TABLE[v & 0x3F] : '=';
  }

  encoded[outIdx] = '\0';
  return outIdx;
}

// ============================================================================
//  String Utilities
// ============================================================================

// Trim leading and trailing whitespace
static void trimString(char* str) {
  if (!str) return;
  
  // Trim leading
  size_t start = 0;
  while (str[start] && isspace(str[start])) start++;
  
  // Trim trailing
  size_t end = strlen(str);
  while (end > start && isspace(str[end - 1])) end--;
  
  // Copy
  if (start > 0) {
    memmove(str, str + start, end - start);
  }
  str[end - start] = '\0';
}

// Extract value from "key = value" line
static bool parseKeyValue(const char* line, char* key, char* value, size_t keyLen, size_t valLen) {
  if (!line || !key || !value) return false;

  const char* eq = strchr(line, '=');
  if (!eq) return false;

  // Extract key
  size_t klen = eq - line;
  if (klen >= keyLen) return false;
  strncpy(key, line, klen);
  key[klen] = '\0';
  trimString(key);

  // Extract value
  const char* valStart = eq + 1;
  size_t vlen = strlen(valStart);
  if (vlen >= valLen) return false;
  strncpy(value, valStart, vlen);
  value[vlen] = '\0';
  trimString(value);

  return true;
}

// ============================================================================
//  Wireguard Config Parser
// ============================================================================

bool parseWireguardConfig(const char* config_text, WireguardConfig& wg_config) {
  if (!config_text) return false;

  // Initialize output
  memset(&wg_config, 0, sizeof(WireguardConfig));

  // Tracking
  bool inInterface = false;
  bool inPeer = false;
  bool hasPrivateKey = false;
  bool hasPublicKey = false;
  bool hasEndpoint = false;
  bool hasTunnelAddr = false;

  // Make a copy for line-by-line parsing
  size_t len = strlen(config_text) + 1;
  char* textCopy = new char[len];
  if (!textCopy) return false;
  strcpy(textCopy, config_text);

  // Parse line by line
  char* line = textCopy;
  char* nextLine = nullptr;

  while (line && *line) {
    // Find next line
    nextLine = strchr(line, '\n');
    if (nextLine) {
      *nextLine = '\0';
      nextLine++;
    }

    // Trim line
    char* lineTrim = line;
    while (*lineTrim && isspace(*lineTrim)) lineTrim++;

    // Skip empty lines and comments
    if (!*lineTrim || lineTrim[0] == '#') {
      line = nextLine;
      continue;
    }

    // Check for section headers
    if (lineTrim[0] == '[') {
      if (strncmp(lineTrim, "[Interface]", 11) == 0) {
        inInterface = true;
        inPeer = false;
      } else if (strncmp(lineTrim, "[Peer]", 6) == 0) {
        inInterface = false;
        inPeer = true;
      } else {
        inInterface = inPeer = false;
      }
      line = nextLine;
      continue;
    }

    // Parse key=value
    char keyBuf[64], valBuf[256];
    if (!parseKeyValue(lineTrim, keyBuf, valBuf, sizeof(keyBuf), sizeof(valBuf))) {
      line = nextLine;
      continue;
    }

    // Process based on section
    if (inInterface) {
      if (strcmp(keyBuf, "PrivateKey") == 0) {
        size_t decoded = base64Decode(valBuf, wg_config.privateKey, 32);
        if (decoded != 32) {
          delete[] textCopy;
          return false;  // must be exactly 32 bytes
        }
        hasPrivateKey = true;
      } else if (strcmp(keyBuf, "ListenPort") == 0) {
        wg_config.listenPort = (uint16_t)strtoul(valBuf, nullptr, 10);
      } else if (strcmp(keyBuf, "Address") == 0) {
        // Extract IP from "192.168.1.5/24" format
        char* slashPos = strchr(valBuf, '/');
        if (slashPos) {
          size_t ipLen = slashPos - valBuf;
          if (ipLen < sizeof(wg_config.tunnelAddress)) {
            strncpy(wg_config.tunnelAddress, valBuf, ipLen);
            wg_config.tunnelAddress[ipLen] = '\0';
            hasTunnelAddr = true;
          }
        }
      }
    } else if (inPeer) {
      if (strcmp(keyBuf, "PublicKey") == 0) {
        size_t decoded = base64Decode(valBuf, wg_config.publicKey, 32);
        if (decoded != 32) {
          delete[] textCopy;
          return false;  // must be exactly 32 bytes
        }
        hasPublicKey = true;
      } else if (strcmp(keyBuf, "Endpoint") == 0) {
        strncpy(wg_config.endpoint, valBuf, sizeof(wg_config.endpoint) - 1);
        wg_config.endpoint[sizeof(wg_config.endpoint) - 1] = '\0';
        hasEndpoint = true;
      } else if (strcmp(keyBuf, "AllowedIPs") == 0) {
        // Simplified: just store the first IP (future: parse CIDR list)
        // For now, we just acknowledge it's present
      } else if (strcmp(keyBuf, "PersistentKeepalive") == 0) {
        wg_config.persistentKeepalive = (uint16_t)strtoul(valBuf, nullptr, 10);
      }
    }

    line = nextLine;
  }

  delete[] textCopy;

  // Validation: must have required fields
  if (!hasPrivateKey || !hasPublicKey || !hasEndpoint || !hasTunnelAddr) {
    return false;
  }

  return true;
}

bool validateWireguardConfig(const WireguardConfig& config) {
  // Check required fields are present (non-zero)
  bool privKeyValid = false;
  for (int i = 0; i < 32; i++) {
    if (config.privateKey[i] != 0) {
      privKeyValid = true;
      break;
    }
  }
  if (!privKeyValid) return false;

  bool pubKeyValid = false;
  for (int i = 0; i < 32; i++) {
    if (config.publicKey[i] != 0) {
      pubKeyValid = true;
      break;
    }
  }
  if (!pubKeyValid) return false;

  if (!config.endpoint || config.endpoint[0] == '\0') return false;
  if (!config.tunnelAddress || config.tunnelAddress[0] == '\0') return false;

  return true;
}
