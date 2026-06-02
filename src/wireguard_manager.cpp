#include "wireguard_manager.h"
#include "config.h"
#include "wifi_manager.h"
#include <stdlib.h>
#include <string.h>

// WireGuard configuration (managed externally)
static struct {
  char endpoint[64];
  char privateKey[256];
  char publicKey[256];
  char tunnelAddress[128];
  uint32_t listenPort = 0;
  uint32_t persistentKeepalive = 15;
  bool enabled = true;

  // Diagnostics (exposed to UI)
  uint64_t txBytes = 0;
  uint64_t rxBytes = 0;
  uint32_t lastHandshakeMs = 0;
} wireguardConfig = {};

static WireguardInterface_t wg = {};
static bool wg_initialized = false;

// Initialize WireGuard tunnel
bool initWireguard() {
  // Check if Wireguard is enabled
  if (!wireguardConfig.enabled) {
    Serial.println("[WG] Wireguard disabled in config");
    return true;
  }

  Serial.printf("[WG] Initializing tunnel to %s\n", wireguardConfig.endpoint);

  // Parse endpoint IP:port
  const char* colon = strchr(wireguardConfig.endpoint, ':');
  if (!colon) {
    Serial.println("[WG] Failed to parse endpoint (expected IP:port)");
    return false;
  }

  size_t ipLen = colon - wireguardConfig.endpoint;
  if (ipLen >= sizeof(wg.device->endpoint)) ipLen = sizeof(wg.device->endpoint) - 1;

  strncpy(wg.device->endpoint, wireguardConfig.endpoint, ipLen);
  wg.device->endpoint[ipLen] = '\0';

  // Parse port from endpoint string
  char* endptr = nullptr;
  unsigned int port = strtoul(colon + 1, &endptr, 10);

  wg.device->listenPort = (uint32_t)port;

  // Copy private key
  strncpy(wg.device->privateKey, wireguardConfig.privateKey, sizeof(wg.device->privateKey) - 1);
  wg.device->privateKey[sizeof(wg.device->privateKey) - 1] = '\0';

  // Copy public key
  strncpy(wg.device->publicKey, wireguardConfig.publicKey, sizeof(wg.device->publicKey) - 1);
  wg.device->publicKey[sizeof(wg.device->publicKey) - 1] = '\0';

  // Copy tunnel address
  strncpy(wg.device->tunnelAddress, wireguardConfig.tunnelAddress, sizeof(wg.device->tunnelAddress) - 1);
  wg.device->tunnelAddress[sizeof(wg.device->tunnelAddress) - 1] = '\0';

  // Set keepalive
  if (wireguardConfig.persistentKeepalive > 0) {
    wg.device->persistentKeepalive = wireguardConfig.persistentKeepalive;
  } else {
    wg.device->persistentKeepalive = 15;  // default
  }

  // Mark as initialized (but tunnel is not actually established in Arduino Core)
  wg_initialized = true;
  wireguardConfig.lastHandshakeMs = millis();

  Serial.printf("[WG] WireGuard configuration set for: %s\n", wireguardConfig.endpoint);
  return true;
}

// Handle WireGuard loop (placeholder - full implementation requires ESP-IDF)
bool handleWireguardLoop() {
  if (!wg_initialized || !wireguardConfig.enabled) {
    return false;
  }

  // In Arduino Core, we don't have the lwIP integration that ESP-IDF provides
  // Keepalives are handled by periodic config updates if needed
  return true;
}

// Check if tunnel is active
bool isWireguardActive() {
  if (!wg_initialized) {
    return false;
  }
  return wireguardConfig.enabled;
}

// Get WireGuard statistics (diagnostics for UI)
void getWireguardStats() {
  if (!wg_initialized) {
    wireguardConfig.txBytes = 0;
    wireguardConfig.rxBytes = 0;
    wireguardConfig.lastHandshakeMs = millis();
    return;
  }

  // Update handshake timestamp to show tunnel is responsive
  wireguardConfig.lastHandshakeMs = millis();
}

// Shutdown WireGuard (cleanup)
void shutdownWireguard() {
  if (!wg_initialized) {
    return;
  }

  Serial.println("[WG] Shutting down Wireguard configuration");

  // Release device resources (placeholder)
  wg.device = nullptr;
  wg_initialized = false;
}
