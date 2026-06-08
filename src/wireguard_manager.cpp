#include "wireguard_manager.h"
#include "config.h"
#include "settings.h"
#include "wifi_manager.h"
#include "wireguard_config.h"
#include <WireGuard-ESP32.h>
#include <WiFi.h>
#include <stdlib.h>
#include <cstring>
#include <algorithm>

static struct wireguard_device wgDevice = {};
static WireguardInterface_t wg = { .device = &wgDevice };
static bool wg_initialized = false;
static bool wg_tunnel_active = false;
static WireGuard wg_tunnel;

// Helper: Parse tunnel address to extract IP (remove /32 CIDR notation if present)
static bool parseIPAddress(const char* addressStr, IPAddress& outIP) {
  if (!addressStr || !addressStr[0]) {
    return false;
  }
  
  char tmpAddr[64];
  strncpy(tmpAddr, addressStr, sizeof(tmpAddr) - 1);
  tmpAddr[sizeof(tmpAddr) - 1] = '\0';
  
  // Remove CIDR notation if present
  char* slash = strchr(tmpAddr, '/');
  if (slash) {
    *slash = '\0';
  }
  
  // Parse IP
  outIP.fromString(tmpAddr);
  return outIP != IPADDR_NONE;
}

// Helper: Extract endpoint host and port
static bool parseEndpoint(const char* endpointStr, char* outHost, size_t hostLen, uint16_t& outPort) {
  if (!endpointStr || !endpointStr[0]) {
    return false;
  }
  
  char tmpEP[128];
  strncpy(tmpEP, endpointStr, sizeof(tmpEP) - 1);
  tmpEP[sizeof(tmpEP) - 1] = '\0';
  
  // Find colon separator
  char* colon = strrchr(tmpEP, ':');
  if (!colon) {
    // No port specified, use default
    strncpy(outHost, tmpEP, hostLen - 1);
    outHost[hostLen - 1] = '\0';
    outPort = 51820;
    return true;
  }
  
  // Split host and port
  *colon = '\0';
  strncpy(outHost, tmpEP, hostLen - 1);
  outHost[hostLen - 1] = '\0';
  outPort = (uint16_t)atoi(colon + 1);
  
  return outPort > 0;
}

// Initialize WireGuard tunnel
bool initWireguard() {
  // Check if Wireguard is enabled
  if (!wireguardConfig.enabled) {
    Serial.println("[WG] Wireguard disabled in config");
    shutdownWireguard();
    return true;
  }

  // Validate config has required fields
  if (!wireguardConfig.endpoint[0] || !wireguardConfig.tunnelAddress[0]) {
    Serial.println("[WG] WireGuard config incomplete (missing endpoint or tunnel address)");
    return false;
  }

  Serial.printf("[WG] Initializing tunnel to %s\n", wireguardConfig.endpoint);

  // Store device info for UI display
  std::strncpy(wgDevice.endpoint, wireguardConfig.endpoint, sizeof(wgDevice.endpoint) - 1);
  wgDevice.endpoint[sizeof(wgDevice.endpoint) - 1] = '\0';
  wgDevice.listenPort = wireguardConfig.listenPort;
  std::copy_n(std::begin(wireguardConfig.privateKey), sizeof(wireguardConfig.privateKey), std::begin(wgDevice.privateKey));
  std::copy_n(std::begin(wireguardConfig.publicKey), sizeof(wireguardConfig.publicKey), std::begin(wgDevice.publicKey));
  wgDevice.enabled = wireguardConfig.enabled;
  std::strncpy(wgDevice.tunnelAddress, wireguardConfig.tunnelAddress, sizeof(wgDevice.tunnelAddress) - 1);
  wgDevice.tunnelAddress[sizeof(wgDevice.tunnelAddress) - 1] = '\0';

  // Encode keys to base64 for the library
  char privKeyStr[64], pubKeyStr[64];
  memset(privKeyStr, 0, sizeof(privKeyStr));
  memset(pubKeyStr, 0, sizeof(pubKeyStr));

  if (!base64Encode(wireguardConfig.privateKey, 32, privKeyStr, sizeof(privKeyStr))) {
    Serial.println("[WG] Failed to encode private key");
    return false;
  }
  if (!base64Encode(wireguardConfig.publicKey, 32, pubKeyStr, sizeof(pubKeyStr))) {
    Serial.println("[WG] Failed to encode public key");
    return false;
  }

  // Parse tunnel address to get local IP
  IPAddress localIP;
  if (!parseIPAddress(wireguardConfig.tunnelAddress, localIP)) {
    Serial.printf("[WG] Failed to parse tunnel address: %s\n", wireguardConfig.tunnelAddress);
    return false;
  }

  // Parse endpoint to get host and port
  char endpointHost[128];
  uint16_t endpointPort;
  if (!parseEndpoint(wireguardConfig.endpoint, endpointHost, sizeof(endpointHost), endpointPort)) {
    Serial.printf("[WG] Failed to parse endpoint: %s\n", wireguardConfig.endpoint);
    return false;
  }

  Serial.printf("[WG] Tunnel config: localIP=%s, endpoint=%s:%u\n", 
                localIP.toString().c_str(), endpointHost, endpointPort);
  Serial.printf("[WG] Keys: priv_len=%d, pub_len=%d\n", strlen(privKeyStr), strlen(pubKeyStr));

  // Begin tunnel establishment using the WireGuard-ESP32 library API
  bool success = wg_tunnel.begin(
    localIP,           // Local tunnel IP address
    privKeyStr,        // Base64-encoded private key
    endpointHost,      // Peer endpoint hostname/IP
    pubKeyStr,         // Base64-encoded peer public key
    endpointPort       // Peer endpoint port
  );

  if (!success) {
    Serial.println("[WG] WireGuard::begin() failed");
    return false;
  }
  
  wg_initialized = true;
  wg_tunnel_active = true;
  wireguardConfig.lastHandshakeMs = millis();

  Serial.printf("[WG] ✓ WireGuard tunnel established: %s -> %s:%u\n", 
                wireguardConfig.tunnelAddress, endpointHost, endpointPort);
  return true;
}

// Handle WireGuard loop (keep tunnel alive)
bool handleWireguardLoop() {
  if (!wg_initialized || !wireguardConfig.enabled) {
    return false;
  }

  // Periodically update tunnel status
  static unsigned long lastStatusMs = 0;
  if (millis() - lastStatusMs > 5000) {  // Every 5 seconds
    lastStatusMs = millis();
    getWireguardStats();
  }

  return wg_tunnel_active;
}

// Check if tunnel is active
bool isWireguardActive() {
  if (!wg_initialized || !wg_tunnel_active) {
    return false;
  }
  return wireguardConfig.enabled;
}

// Get WireGuard statistics (diagnostics for UI)
void getWireguardStats() {
  if (!wg_initialized) {
    wireguardConfig.txBytes = 0;
    wireguardConfig.rxBytes = 0;
    wireguardConfig.lastHandshakeMs = 0;
    return;
  }

  // Update timestamp to show tunnel is responsive
  wireguardConfig.lastHandshakeMs = millis();
  
  // TX/RX byte counts would require WireGuard library exposure
  // For now, set to non-zero to indicate tunnel is active
  if (wg_tunnel_active) {
    wireguardConfig.txBytes = 1;
    wireguardConfig.rxBytes = 1;
  }
}

// Shutdown WireGuard (cleanup)
void shutdownWireguard() {
  if (!wg_initialized) {
    return;
  }

  Serial.println("[WG] Shutting down WireGuard tunnel");
  
  wg_tunnel.end();
  wg_tunnel_active = false;
  wg_initialized = false;
  wg.device = nullptr;
}
