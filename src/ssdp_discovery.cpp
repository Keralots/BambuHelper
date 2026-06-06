#include "ssdp_discovery.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
//  Discovered-device table
// ---------------------------------------------------------------------------
// Everything here runs in the main loop task: ssdpTick() drains the sockets and
// ssdpScanResultJson() is called from the (synchronous) web handler, which is
// also serviced from loop(). They never run concurrently, so no locking is
// needed. This deliberately avoids AsyncUDP + a critical-section spinlock, which
// could deadlock with interrupts disabled during a concurrent save/TLS op.
struct SsdpDevice {
  char serial[20];
  char ip[16];
  char name[24];
  char model[12];
  uint32_t lastSeen;
};

static const uint8_t  SSDP_MAX_DEVICES = 8;
// Listen window per scan. Printers broadcast ~every 5s, alternating dest port
// 1990/2021, so a single port sees a device only ~every 10s. 12s guarantees at
// least one broadcast per device on each port - shorter windows miss the
// less-frequent broadcasters (observed: H2C in cloud mode).
static const uint32_t SSDP_SCAN_MS = 12000;

static SsdpDevice s_devices[SSDP_MAX_DEVICES];

static WiFiUDP s_udp2021;
static WiFiUDP s_udp1990;
static bool          s_scanActive  = false;
static bool          s_listening   = false;  // sockets currently open
static unsigned long s_scanStartMs = 0;

static const IPAddress SSDP_GROUP(239, 255, 255, 250);

// ---------------------------------------------------------------------------
//  Header parsing
// ---------------------------------------------------------------------------

// If `line` begins (case-insensitively) with `key`, copy the trimmed value into
// `out` and return true.
static bool headerValue(const char* line, const char* key, char* out, size_t outLen) {
  size_t klen = strlen(key);
  if (strncasecmp(line, key, klen) != 0) return false;
  const char* v = line + klen;
  while (*v == ' ' || *v == '\t') v++;
  strlcpy(out, v, outLen);
  size_t n = strlen(out);
  while (n > 0 && (out[n-1] == ' ' || out[n-1] == '\t' ||
                   out[n-1] == '\r' || out[n-1] == '\n')) {
    out[--n] = '\0';
  }
  return true;
}

// Reduce a Location value to a bare IPv4: strip scheme, trailing :port / path.
static void normalizeIp(const char* in, char* out, size_t outLen) {
  const char* p = in;
  if (strncasecmp(p, "http://", 7) == 0)       p += 7;
  else if (strncasecmp(p, "https://", 8) == 0) p += 8;
  size_t j = 0;
  while (*p && *p != ':' && *p != '/' && *p != ' ' && j < outLen - 1) {
    out[j++] = *p++;
  }
  out[j] = '\0';
}

static void upsertDevice(const char* serial, const char* ip,
                         const char* name, const char* model) {
  if (!serial || serial[0] == '\0') return;

  char up[20];
  strlcpy(up, serial, sizeof(up));
  for (char* c = up; *c; c++) *c = toupper((unsigned char)*c);

  int slot = -1, freeSlot = -1, oldestSlot = 0;
  uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < SSDP_MAX_DEVICES; i++) {
    if (s_devices[i].serial[0] == '\0') {
      if (freeSlot < 0) freeSlot = i;
      continue;
    }
    if (strcmp(s_devices[i].serial, up) == 0) { slot = i; break; }
    if (s_devices[i].lastSeen < oldest) { oldest = s_devices[i].lastSeen; oldestSlot = i; }
  }
  if (slot < 0) slot = (freeSlot >= 0) ? freeSlot : oldestSlot;

  SsdpDevice& d = s_devices[slot];
  strlcpy(d.serial, up, sizeof(d.serial));
  if (ip)    strlcpy(d.ip, ip, sizeof(d.ip));
  if (name)  strlcpy(d.name, name, sizeof(d.name));
  if (model) strlcpy(d.model, model, sizeof(d.model));
  d.lastSeen = millis();
}

// Parse one SSDP packet payload (already null-terminated in `buf`).
static void parsePacket(char* buf) {
  char serial[20] = {0};
  char loc[64]    = {0};
  char name[24]   = {0};
  char model[12]  = {0};

  char* save = nullptr;
  for (char* line = strtok_r(buf, "\r\n", &save); line;
       line = strtok_r(nullptr, "\r\n", &save)) {
    headerValue(line, "USN:", serial, sizeof(serial)) ||
    headerValue(line, "Location:", loc, sizeof(loc)) ||
    headerValue(line, "DevName.bambu.com:", name, sizeof(name)) ||
    headerValue(line, "DevModel.bambu.com:", model, sizeof(model));
  }

  if (serial[0] == '\0') return;  // not a Bambu device announcement

  char ip[16] = {0};
  if (loc[0]) normalizeIp(loc, ip, sizeof(ip));
  upsertDevice(serial, ip, name, model);
}

// Drain all currently-buffered datagrams on one socket.
static void drainSocket(WiFiUDP& udp) {
  char buf[600];
  int sz;
  while ((sz = udp.parsePacket()) > 0) {
    int n = udp.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n <= 0) break;
    buf[n] = '\0';
    parsePacket(buf);
  }
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
static void closeSockets() {
  if (!s_listening) return;
  s_udp2021.stop();
  s_udp1990.stop();
  s_listening = false;
}

int ssdpStartScan() {
  closeSockets();
  memset(s_devices, 0, sizeof(s_devices));
  s_scanActive = false;

  if (WiFi.status() != WL_CONNECTED) return 0;

  int opened = 0;
  if (s_udp2021.beginMulticast(SSDP_GROUP, 2021)) opened++;
  if (s_udp1990.beginMulticast(SSDP_GROUP, 1990)) opened++;
  if (opened == 0) { closeSockets(); return 0; }

  s_listening   = true;
  s_scanActive  = true;
  s_scanStartMs = millis();
  return opened;
}

void ssdpStopScan() {
  closeSockets();
  s_scanActive = false;
}

void ssdpTick() {
  if (!s_scanActive) return;
  drainSocket(s_udp2021);
  drainSocket(s_udp1990);
  if (millis() - s_scanStartMs > SSDP_SCAN_MS) {
    ssdpStopScan();
  }
}

bool ssdpScanActive() { return s_scanActive; }

void ssdpScanResultJson(String& out) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < SSDP_MAX_DEVICES; i++) {
    if (s_devices[i].serial[0] == '\0') continue;
    JsonObject o = arr.add<JsonObject>();
    o["serial"] = s_devices[i].serial;
    o["ip"]     = s_devices[i].ip;
    o["name"]   = s_devices[i].name;
    o["model"]  = s_devices[i].model;
  }
  serializeJson(doc, out);
}
