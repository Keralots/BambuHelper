#ifndef WIREGUARD_CONFIG_H
#define WIREGUARD_CONFIG_H

#include <Arduino.h>
#include "bambu_state.h"

/**
 * Wireguard Configuration Parser
 * 
 * Parses standard Wireguard INI-format config files (.conf) and extracts
 * key material + peer information into the WireguardConfig struct.
 * 
 * Supported format:
 *   [Interface]
 *   PrivateKey = base64-encoded-32-bytes
 *   ListenPort = 51820
 *   Address = 192.168.1.5/24
 * 
 *   [Peer]
 *   PublicKey = base64-encoded-32-bytes
 *   Endpoint = 209.202.254.14:51820
 *   AllowedIPs = 10.192.122.3/32, 10.192.124.1/24
 *   PersistentKeepalive = 25
 * 
 * Lines starting with '#' are ignored as comments.
 * Whitespace is trimmed from all values.
 */

/**
 * parseWireguardConfig - Parse INI-format Wireguard config
 * @config_text: Multiline INI config as null-terminated string
 * @wg_config: Output structure to populate
 * 
 * Returns: true on success, false if parse error (missing required keys, invalid base64, etc.)
 */
bool parseWireguardConfig(const char* config_text, WireguardConfig& wg_config);

/**
 * base64Decode - Decode standard base64 to binary
 * @encoded: Base64-encoded string (standard alphabet + padding)
 * @decoded: Output buffer (must be at least (len+3)/4*3 bytes)
 * @maxLen: Maximum bytes to write to decoded buffer
 * 
 * Returns: Number of decoded bytes, or 0 on error (invalid base64, overflow)
 */
size_t base64Decode(const char* encoded, uint8_t* decoded, size_t maxLen);

/**
 * base64Encode - Encode binary to standard base64
 * @raw: Binary data
 * @rawLen: Number of bytes in raw
 * @encoded: Output buffer (must be at least ((rawLen+2)/3)*4 + 1 bytes)
 * @maxLen: Size of encoded buffer
 * 
 * Returns: Length of encoded string (not including null terminator), 0 on error
 */
size_t base64Encode(const uint8_t* raw, size_t rawLen, char* encoded, size_t maxLen);

/**
 * validateWireguardConfig - Check if WireguardConfig is valid
 * @config: Configuration to validate
 * 
 * Returns: true if all required fields are populated
 */
bool validateWireguardConfig(const WireguardConfig& config);

#endif // WIREGUARD_CONFIG_H
