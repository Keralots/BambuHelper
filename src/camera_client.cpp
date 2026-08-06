// ===========================================================================
//  camera_client.cpp  -  Bambu P1/A1 LAN camera (issue #120). See header.
//  Real impl is BOARD_HAS_CAMERA only; stubs elsewhere keep all envs linking.
// ===========================================================================
#include "camera_client.h"
#include "config.h"

#if BOARD_HAS_CAMERA

#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "settings.h"

// --- tunables --------------------------------------------------------------
static const uint16_t CAM_PORT           = 6000;
static const uint32_t CAM_BUF_SIZE       = 200 * 1024; // max single JPEG (PSRAM)
static const uint32_t CAM_CONNECT_MIN_MS = 2000;       // first retry gap after a failed connect
static const uint32_t CAM_CONNECT_MAX_MS = 16000;      // backoff cap (connect() blocks ~timeout)
static const uint8_t  CAM_TLS_TIMEOUT_S  = 3;          // keep a blocking connect short
static const int      CAM_READ_BUDGET    = 16 * 1024;  // max bytes drained / loop
static const size_t   CAM_CHUNK          = 1460;

// --- state -----------------------------------------------------------------
static bool              g_active = false;
static WiFiClientSecure* g_tls    = nullptr;
static uint32_t          g_connectBackoffMs = CAM_CONNECT_MIN_MS;

static uint8_t* g_inflight = nullptr;   // accumulates until a full SOI..EOI
static uint32_t g_inflightLen = 0;
static uint8_t* g_published = nullptr;  // last complete frame (stable for draw)
static uint32_t g_publishedLen = 0;
static uint32_t g_frameId = 0;

static char g_ip[16]         = {0};
static char g_accessCode[12] = {0};
static unsigned long g_lastConnectMs = 0;

// ---------------------------------------------------------------------------
//  Gates
// ---------------------------------------------------------------------------
// P1S=01P, P1P=01S, A1=039, A1mini=030 (per Bambu serial-prefix table).
static bool isP1A1Serial(const char* s) {
  if (!s || strlen(s) < 3) return false;
  return strncmp(s, "01P", 3) == 0 || strncmp(s, "01S", 3) == 0 ||
         strncmp(s, "039", 3) == 0 || strncmp(s, "030", 3) == 0;
}

bool cameraCanStreamDisplayedPrinter() {
  // Camera is a 2nd/3rd TLS socket. The spike proved 2 MQTT + camera (3 TLS)
  // coexist fine on the 8MB PSRAM boards, so allow up to 2 live connections
  // (= max 3 TLS, the proven ceiling); cap there to avoid the untested 4-printer
  // + camera case. cameraService() also heap-gates each connect as a backstop.
  if (getActiveConnCount() > 2) return false;
  PrinterSlot& p = displayedPrinter();
  if (!p.state.connected) return false;
  if (p.config.mode != CONN_LOCAL) return false;
  const char* ip = p.config.ip[0] ? p.config.ip : p.state.localIp;
  if (!ip[0] || !p.config.accessCode[0]) return false;
  if (!isP1A1Serial(p.config.serial)) return false;
  return true;
}

bool cameraDisplayedHasCameraTile() {
  PrinterConfig& c = displayedPrinter().config;
  // Standard 2x3 slots are always on screen.
  for (uint8_t i = 0; i < GAUGE_SLOT_COUNT; i++)
    if (c.gaugeSlots[i] == GAUGE_CAMERA) return true;
  // Extra slots only render in their active layout, so only count them there -
  // a camera placed in an inactive extra grid must not open the socket or grab
  // tap-to-fullscreen when no CAM tile is actually visible.
  const bool landscape = (dispSettings.rotation == 1 || dispSettings.rotation == 3);
  if (landscape && dispSettings.landscape8Slots) {
    for (uint8_t i = 0; i < LANDSCAPE_EXTRA_COUNT; i++)
      if (c.landscapeExtras[i] == GAUGE_CAMERA) return true;
  }
  if (!landscape && dispSettings.portrait9Slots) {
    for (uint8_t i = 0; i < PORTRAIT_EXTRA_COUNT; i++)
      if (c.portraitExtras[i] == GAUGE_CAMERA) return true;
  }
  return false;
}

// True when the camera is not streaming, or is streaming the IP that matches the
// currently displayed printer. The loop stops a mismatched stream (rotation can
// move displayIndex while a socket is open) so the tile/fullscreen never shows a
// previous printer's frames.
bool cameraStreamingDisplayed() {
  if (!g_active) return true;
  PrinterSlot& p = displayedPrinter();
  const char* ip = p.config.ip[0] ? p.config.ip : p.state.localIp;
  return ip[0] && strcmp(ip, g_ip) == 0;
}

// ---------------------------------------------------------------------------
//  Buffers
// ---------------------------------------------------------------------------
static void freeBuffers() {
  if (g_inflight)  { heap_caps_free(g_inflight);  g_inflight  = nullptr; }
  if (g_published) { heap_caps_free(g_published); g_published = nullptr; }
  g_inflightLen = 0;
  g_publishedLen = 0;
}

static bool allocBuffers() {
  if (g_inflight && g_published) return true;
  g_inflight  = (uint8_t*)heap_caps_malloc(CAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
  g_published = (uint8_t*)heap_caps_malloc(CAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
  if (!g_inflight || !g_published) { freeBuffers(); return false; }
  return true;
}

// ---------------------------------------------------------------------------
//  Protocol
// ---------------------------------------------------------------------------
// 80-byte bblp auth: 4x uint32 LE {0x40, 0x3000, 0, 0} + "bblp" padded to 32 +
// access code padded to 32. (Source: bambu-connect CameraClient.py; verified on
// a real P1S during the issue-#120 spike.)
static void sendAuth() {
  uint8_t pkt[80];
  memset(pkt, 0, sizeof(pkt));
  uint32_t hdr[4] = { 0x40, 0x3000, 0, 0 };
  memcpy(pkt, hdr, 16);
  memcpy(pkt + 16, "bblp", 4);
  size_t acl = strlen(g_accessCode);
  if (acl > 32) acl = 32;
  memcpy(pkt + 48, g_accessCode, acl);
  g_tls->write(pkt, sizeof(pkt));
}

// Find 2-byte JPEG marker (0xFF, second) in buf[from..len); index or -1.
static int findMarker(const uint8_t* buf, uint32_t len, uint32_t from, uint8_t second) {
  if (len < 2) return -1;
  for (uint32_t i = from; i + 1 < len; i++)
    if (buf[i] == 0xFF && buf[i + 1] == second) return (int)i;
  return -1;
}

static void publishFrame(uint32_t frameLen) {
  memcpy(g_published, g_inflight, frameLen);
  g_publishedLen = frameLen;
  g_frameId++;
}

// Pull complete JPEGs out of g_inflight, keeping it anchored at an SOI.
static void sliceFrames() {
  for (;;) {
    int soi = findMarker(g_inflight, g_inflightLen, 0, 0xD8);  // FFD8
    if (soi < 0) {
      if (g_inflightLen > 0 && g_inflight[g_inflightLen - 1] == 0xFF) {
        g_inflight[0] = 0xFF; g_inflightLen = 1;  // keep split marker
      } else {
        g_inflightLen = 0;
      }
      return;
    }
    if (soi > 0) { memmove(g_inflight, g_inflight + soi, g_inflightLen - soi); g_inflightLen -= soi; }
    int eoi = findMarker(g_inflight, g_inflightLen, 2, 0xD9);  // FFD9
    if (eoi < 0) return;  // wait for more bytes
    uint32_t frameLen = (uint32_t)eoi + 2;
    publishFrame(frameLen);
    uint32_t rem = g_inflightLen - frameLen;
    if (rem) memmove(g_inflight, g_inflight + frameLen, rem);
    g_inflightLen = rem;
  }
}

static void teardownSocket() {
  if (g_tls) { g_tls->stop(); delete g_tls; g_tls = nullptr; }
  g_inflightLen = 0;
}

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------
void cameraBegin() {
  if (g_active) return;
  if (!cameraCanStreamDisplayedPrinter()) return;
  PrinterSlot& p = displayedPrinter();
  const char* ip = p.config.ip[0] ? p.config.ip : p.state.localIp;
  if (!allocBuffers()) return;
  strlcpy(g_ip, ip, sizeof(g_ip));
  strlcpy(g_accessCode, p.config.accessCode, sizeof(g_accessCode));
  g_inflightLen = 0;
  g_publishedLen = 0;
  g_frameId = 0;
  g_lastConnectMs = 0;
  g_connectBackoffMs = CAM_CONNECT_MIN_MS;
  g_active = true;
}

void cameraStop() {
  g_active = false;
  teardownSocket();
  freeBuffers();
}

bool cameraActive() { return g_active; }

void cameraService() {
  if (!g_active) return;

  if (!g_tls || !g_tls->connected()) {
    unsigned long now = millis();
    // connect() is blocking (up to the TLS timeout). Space attempts by a growing
    // backoff so an unreachable port 6000 cannot stall the loop every couple of
    // seconds; the gap always exceeds the connect timeout.
    if (now - g_lastConnectMs < g_connectBackoffMs) return;
    g_lastConnectMs = now;
    if (ESP.getFreeHeap() < BAMBU_MIN_FREE_HEAP) return;
    if (!g_tls) {
      g_tls = new (std::nothrow) WiFiClientSecure();
      if (!g_tls) return;
      g_tls->setInsecure();
      g_tls->setTimeout(CAM_TLS_TIMEOUT_S);
    }
    esp_task_wdt_reset();
    if (!g_tls->connect(g_ip, CAM_PORT)) {
      teardownSocket();
      g_connectBackoffMs *= 2;
      if (g_connectBackoffMs > CAM_CONNECT_MAX_MS) g_connectBackoffMs = CAM_CONNECT_MAX_MS;
      return;
    }
    sendAuth();
    g_inflightLen = 0;
    g_connectBackoffMs = CAM_CONNECT_MIN_MS;  // reset on success
  }

  int budget = CAM_READ_BUDGET;
  uint8_t chunk[CAM_CHUNK];
  while (budget > 0 && g_tls->available()) {
    int want = g_tls->available();
    if (want > (int)CAM_CHUNK) want = CAM_CHUNK;
    if (want > budget) want = budget;
    int n = g_tls->read(chunk, want);
    if (n <= 0) break;
    budget -= n;
    if (g_inflightLen + (uint32_t)n > CAM_BUF_SIZE) g_inflightLen = 0;  // desync reset
    memcpy(g_inflight + g_inflightLen, chunk, n);
    g_inflightLen += (uint32_t)n;
    sliceFrames();
  }
}

bool cameraGetLatestFrame(const uint8_t** buf, size_t* len, uint32_t* frameId) {
  if (!g_publishedLen) return false;
  *buf = g_published;
  *len = g_publishedLen;
  *frameId = g_frameId;
  return true;
}

#else  // ---- non-camera boards: no-op stubs ---------------------------------

bool cameraCanStreamDisplayedPrinter() { return false; }
bool cameraDisplayedHasCameraTile() { return false; }
bool cameraStreamingDisplayed() { return true; }
void cameraBegin() {}
void cameraStop() {}
bool cameraActive() { return false; }
void cameraService() {}
bool cameraGetLatestFrame(const uint8_t**, size_t*, uint32_t*) { return false; }

#endif
