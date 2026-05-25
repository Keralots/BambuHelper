#ifndef WIREGUARD_MANAGER_H
#define WIREGUARD_MANAGER_H

#include <Arduino.h>
#include "bambu_state.h"

/**
 * Wireguard Tunnel Manager
 * 
 * Lifecycle management for device-wide Wireguard VPN tunnel.
 * Initializes tunnel before MQTT connections, maintains health monitoring,
 * and provides diagnostics via web UI endpoint.
 */

/**
 * initWireguard - Initialize Wireguard tunnel if enabled
 * 
 * Must be called after WiFi is connected and before MQTT initialization.
 * Uses global wireguardConfig from settings.cpp.
 * 
 * Returns: true if tunnel initialized successfully or disabled, false on error
 * 
 * On error: logs to Serial, gracefully falls back (device continues without tunnel)
 */
bool initWireguard();

/**
 * handleWireguardLoop - Periodic maintenance for tunnel
 * 
 * Should be called in main loop (low priority, ~1/sec recommended).
 * Checks tunnel health, updates diagnostics (TX/RX bytes, last handshake).
 * Re-establishes tunnel if it drops.
 * 
 * Returns: true if tunnel is active and healthy
 */
bool handleWireguardLoop();

/**
 * isWireguardActive - Check if tunnel is currently established
 * 
 * Returns: true if tunnel is up and passing traffic
 */
bool isWireguardActive();

/**
 * getWireguardStats - Get tunnel diagnostic information
 * 
 * Populates txBytes, rxBytes, lastHandshakeMs in wireguardConfig.
 * Used by /wireguard/status endpoint.
 */
void getWireguardStats();

/**
 * shutdownWireguard - Gracefully close tunnel
 * 
 * Called on WiFi disconnect or device shutdown.
 */
void shutdownWireguard();

#endif // WIREGUARD_MANAGER_H
