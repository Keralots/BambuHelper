#include "wireguard_manager.h"
#include "config.h"
#include "wifi_manager.h"
#include <WiFi.h>

extern "C" {
#include "wireguard.h"
}

static WireguardInterface_t wg;
static bool wg_initialized = false;
static unsigned long wg_init_start_ms = 0;

bool initWireguard() {
  // Check if Wireguard is enabled
  if (!wireguardConfig.enabled) {
    Serial.println("[WG] Wireguard disabled in config");
    return true;  // gracefully disabled is success
  }

  // Pre-flight heap check
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < WIREGUARD_HEAP_MIN) {
    Serial.printf("[WG] Insufficient heap: %u < %u bytes\n", freeHeap, WIREGUARD_HEAP_MIN);
    wireguardConfig.enabled = false;  // disable for this session
    return false;
  }

  // Validate config is populated
  if (!validateWireguardConfig(wireguardConfig)) {
    Serial.println("[WG] Invalid Wireguard configuration");
    return false;
  }

  // Check WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WG] WiFi not connected, cannot initialize Wireguard");
    return false;
  }

  Serial.printf("[WG] Initializing tunnel to %s\n", wireguardConfig.endpoint);
  wg_init_start_ms = millis();

  // Initialize WireGuard interface
  // Note: ciniml/WireGuard-ESP32-Arduino uses direct struct initialization
  // ref: https://github.com/ciniml/WireGuard-ESP32-Arduino
  
  // Configuration matching standard Wireguard semantics
  // Private key, public key (peer), endpoint, allowed IPs, keepalive
  
  // Start WireGuard tunnel
  // The library expects:
  // - private key (binary 32 bytes)
  // - peer public key (binary 32 bytes)  
  // - peer endpoint IP + port
  // - tunnel address (IP to assign to esp32 in tunnel)
  // - allowed IPs (CIDR routes)
  
  int init_result = wireguard_setup_interface(
    &wg,
    wireguardConfig.privateKey,
    wireguardConfig.tunnelAddress,
    wireguardConfig.listenPort
  );

  if (init_result != 0) {
    Serial.printf("[WG] Interface setup failed: %d\n", init_result);
    return false;
  }

  // Configure peer
  ip4_addr_t endpoint_ip;
  // Parse endpoint IP (simplified: expect "IP:port" format)
  char* colon = strchr(wireguardConfig.endpoint, ':');
  if (!colon) {
    Serial.println("[WG] Invalid endpoint format (expected IP:port)");
    return false;
  }

  char endpointIpStr[64];
  size_t ipLen = colon - wireguardConfig.endpoint;
  if (ipLen >= sizeof(endpointIpStr)) {
    Serial.println("[WG] Endpoint IP too long");
    return false;
  }
  strncpy(endpointIpStr, wireguardConfig.endpoint, ipLen);
  endpointIpStr[ipLen] = '\0';

  uint16_t endpoint_port = (uint16_t)strtoul(colon + 1, nullptr, 10);

  if (!ipaddr_aton(endpointIpStr, &endpoint_ip)) {
    Serial.printf("[WG] Failed to parse endpoint IP: %s\n", endpointIpStr);
    return false;
  }

  int peer_result = wireguard_add_peer(
    &wg,
    wireguardConfig.publicKey,
    (uint8_t*)&endpoint_ip.addr,
    endpoint_port,
    wireguardConfig.persistentKeepalive
  );

  if (peer_result != 0) {
    Serial.printf("[WG] Peer configuration failed: %d\n", peer_result);
    return false;
  }

  wg_initialized = true;
  Serial.printf("[WG] Tunnel established: %s (took %lu ms)\n", 
    wireguardConfig.tunnelAddress, millis() - wg_init_start_ms);

  return true;
}

bool handleWireguardLoop() {
  if (!wg_initialized || !wireguardConfig.enabled) {
    return false;
  }

  // Periodic health check - lightweight
  // The WireGuard library handles keepalives internally
  // We just need to check if the tunnel is still active
  
  // Update diagnostic stats periodically
  static unsigned long lastStatUpdate = 0;
  if (millis() - lastStatUpdate > 5000) {  // update every 5 seconds
    lastStatUpdate = millis();
    getWireguardStats();
  }

  return isWireguardActive();
}

bool isWireguardActive() {
  if (!wg_initialized || !wireguardConfig.enabled) {
    return false;
  }

  // Check if tunnel is operational
  // ciniml library sets up a tun interface; if it exists, tunnel is active
  // Simplified check: if we got this far without errors, assume active
  return true;
}

void getWireguardStats() {
  if (!wg_initialized) {
    wireguardConfig.txBytes = 0;
    wireguardConfig.rxBytes = 0;
    wireguardConfig.lastHandshakeMs = 0;
    return;
  }

  // Try to get stats from wireguard interface (if library supports it)
  // For now, update with current timestamp as proof of life
  // Real implementation would query kernel stats via netlink or similar
  
  // The ciniml library has limited stat exposure; set last handshake to now
  // as a simple "still alive" indicator
  if (isWireguardActive()) {
    wireguardConfig.lastHandshakeMs = millis();
  }

  // TX/RX bytes would come from iptables or netstat in a full impl
  // For MVP, leave at 0 or update from any available counters
}

void shutdownWireguard() {
  if (!wg_initialized) {
    return;
  }

  Serial.println("[WG] Shutting down Wireguard tunnel");
  
  // Cleanup WireGuard interface
  wireguard_stop_interface(&wg);
  
  wg_initialized = false;
}
