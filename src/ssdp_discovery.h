#ifndef SSDP_DISCOVERY_H
#define SSDP_DISCOVERY_H

#include <Arduino.h>

// On-demand SSDP discovery of Bambu printers on the local network.
//
// Bambu printers broadcast an SSDP NOTIFY unprompted every ~5s to the multicast
// group 239.255.255.250 (destination ports 1990 and 2021). The payload carries
// USN (serial), Location (IP), DevName.bambu.com (name) and DevModel.bambu.com
// (model). This is entirely local - no cloud, no token, no Cloudflare.
//
// Limitation: multicast does not cross subnets/VLANs, so only printers on the
// same LAN segment as this device are found.

// Start a scan: clear the table and open the multicast listeners. Returns the
// number of UDP sockets that opened (0, 1 or 2). 0 means nothing is listening
// (WiFi STA down or both binds failed) - the caller should report an error.
int ssdpStartScan();

// Call from loop() every iteration. Drains the multicast sockets and closes
// them once the scan window elapses. No-op unless a scan is active.
void ssdpTick();

// Abort an in-progress scan immediately and free the sockets. Safe to call any
// time (e.g. before a save that reinits networking).
void ssdpStopScan();

// True while a scan window is open.
bool ssdpScanActive();

// Serialize discovered devices as [{"serial","ip","name","model"}] into `out`.
void ssdpScanResultJson(String& out);

#endif // SSDP_DISCOVERY_H
