#ifndef WIREGUARD_MANAGER_H
#define WIREGUARD_MANAGER_H

#include "Arduino.h"

struct wireguard_device {
  char endpoint[64];
  char privateKey[256];
  char publicKey[256];
  char tunnelAddress[128];
  uint32_t listenPort;
  uint32_t persistentKeepalive;
  bool enabled;
};

struct WireguardInterface_t {
  struct wireguard_device *device;
};

extern bool initWireguard();
extern bool handleWireguardLoop();
extern bool isWireguardActive();
extern void getWireguardStats();
extern void shutdownWireguard();

#endif // WIREGUARD_MANAGER_H
