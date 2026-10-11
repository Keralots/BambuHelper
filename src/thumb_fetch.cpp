#include "thumb_fetch.h"
#include <new>

#if HAS_CARD_THUMB

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include "bambu_state.h"
#include "bambu_cloud.h"
#include "settings.h"        // dispSettings, loadCloudToken
#include "display_ui.h"      // tft (sprite parent)
#include "display_card.h"    // cardSkinActive
#include "web_server.h"      // isOtaAutoInProgress

static const size_t   PNG_CAP        = 256 * 1024;   // downloaded PNG, PSRAM
static const uint32_t MIN_HEAP       = 90000;        // internal free before a fetch
static const uint32_t MIN_BLOCK      = 40000;        // largest internal block (TLS)
static const uint8_t  MAX_FAILS      = 5;
static const uint32_t WORKER_STACK   = 12288;

// ---------------------------------------------------------------------------
//  Worker. The job is owned by the worker from creation until it is handed
//  back through the mailbox; the loop frees it after collecting.
// ---------------------------------------------------------------------------
struct ThumbJob {
  char        token[1200];
  char        taskId[24];
  int         plate;
  uint8_t     slot;
  CloudRegion region;
  uint8_t*    png;
  size_t      len;
  bool        ok;
};

static portMUX_TYPE       g_mux     = portMUX_INITIALIZER_UNLOCKED;
static ThumbJob*          g_done    = nullptr;   // mailbox, guarded by g_mux
static bool               g_running = false;     // loop-side only

static bool fetchInto(ThumbJob* j) {
  static char url[640];   // one worker at a time
  if (!cloudFetchPlateThumbUrl(j->token, j->region, j->taskId, j->plate, url, sizeof(url)))
    return false;
  j->png = (uint8_t*)heap_caps_malloc(PNG_CAP, MALLOC_CAP_SPIRAM);
  if (!j->png) return false;
  if (!cloudDownload(url, j->png, PNG_CAP, &j->len)) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (j->len <= 33 || memcmp(j->png, sig, 8) != 0) return false;
  // Kept per printer until its job changes: give back the unused part of the cap.
  if (uint8_t* fit = (uint8_t*)heap_caps_realloc(j->png, j->len, MALLOC_CAP_SPIRAM)) j->png = fit;
  return true;
}

static void worker(void* arg) {
  ThumbJob* j = (ThumbJob*)arg;
  j->ok = fetchInto(j);                     // every client lives and dies in here
  memset(j->token, 0, sizeof(j->token));
  Serial.printf("THUMB: fetch %s (%u bytes)\n", j->ok ? "ok" : "failed", (unsigned)j->len);
  portENTER_CRITICAL(&g_mux);
  g_done = j;
  portEXIT_CRITICAL(&g_mux);
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
//  Loop-side state, one entry per printer slot: the job it belongs to, the
//  downloaded PNG and the rendered thumb. Rotating between printers keeps
//  every entry; only a different job (or Cards / plate preview off) drops one.
// ---------------------------------------------------------------------------
struct ThumbSlot {
  char     task[24];                     // job key ("" = unused)
  uint16_t plate;
  uint8_t  fails;
  unsigned long failAt, backoff;
  uint8_t* png;
  size_t   pngLen;
  lgfx::LGFX_Sprite* thumb;
  int16_t  size;
  uint16_t bg;
  bool     renderFailed;
};

static ThumbSlot g_slots[MAX_PRINTERS];
static uint32_t  g_thumbGen = 0;         // bumped on every render, any slot

static void resetSlot(ThumbSlot& t) {
  if (t.png) heap_caps_free(t.png);
  if (t.thumb) { t.thumb->deleteSprite(); delete t.thumb; }
  memset(&t, 0, sizeof(t));
}

static bool taskIdUsable(const char* t) {
  if (!t[0] || strcmp(t, "0") == 0) return false;
  for (const char* c = t; *c; c++) if (*c < '0' || *c > '9') return false;
  return true;
}

static void collect() {
  ThumbJob* j;
  portENTER_CRITICAL(&g_mux);
  j = g_done;
  g_done = nullptr;
  portEXIT_CRITICAL(&g_mux);
  if (!j) return;
  g_running = false;
  ThumbSlot& t = g_slots[j->slot];
  const bool current = strcmp(t.task, j->taskId) == 0 && t.plate == j->plate;
  if (current && j->ok) {
    t.png = j->png;                         // slot had none: fetches start only then
    t.pngLen = j->len;
    t.fails = 0;
    t.backoff = 0;
  } else {
    if (j->png) heap_caps_free(j->png);
    if (current) {                          // a real failure for this job
      t.fails++;
      t.failAt = millis();
      t.backoff = t.backoff ? std::min<unsigned long>(t.backoff * 2, 600000UL) : 30000UL;
    }
  }
  heap_caps_free(j);
}

void thumbService() {
  collect();

  // Keep each slot's entry in step with its printer's job.
  const bool on = cardSkinActive() && dispSettings.cardLeft == 2;
  for (uint8_t i = 0; i < MAX_PRINTERS; i++) {
    ThumbSlot& t = g_slots[i];
    const BambuState& s = printers[i].state;
    if (!on || !taskIdUsable(s.taskId)) {
      if (t.task[0]) resetSlot(t);
      continue;
    }
    if (strcmp(t.task, s.taskId) != 0 || t.plate != s.plateIdx) {
      resetSlot(t);
      strlcpy(t.task, s.taskId, sizeof(t.task));
      t.plate = s.plateIdx;
    }
  }
  if (!on || g_running) return;

  // Fetch only for the printer on screen.
  PrinterSlot& p = displayedPrinter();
  const uint8_t slot = (uint8_t)(&p - printers);
  ThumbSlot& t = g_slots[slot];
  if (!t.task[0] || t.png || t.fails >= MAX_FAILS) return;
  if (t.backoff && millis() - t.failAt < t.backoff) return;
  if (t.plate == 0) return;                 // pushall not in yet
  if (WiFi.status() != WL_CONNECTED || isOtaAutoInProgress()) return;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_HEAP ||
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < MIN_BLOCK) {
    t.failAt = millis();
    t.backoff = 5000;
    return;
  }

  ThumbJob* j = (ThumbJob*)heap_caps_calloc(1, sizeof(ThumbJob), MALLOC_CAP_SPIRAM);
  if (!j) return;
  if (!loadCloudToken(j->token, sizeof(j->token)) || !j->token[0]) {
    heap_caps_free(j);
    t.failAt = millis();                    // not signed in (yet): look again in a minute
    t.backoff = 60000;
    return;
  }
  strlcpy(j->taskId, t.task, sizeof(j->taskId));
  j->plate = t.plate;
  j->slot = slot;
  j->region = p.config.region;
  g_running = true;
  if (xTaskCreatePinnedToCore(worker, "thumb", WORKER_STACK, j, 1, nullptr, 1) != pdPASS) {
    g_running = false;
    memset(j->token, 0, sizeof(j->token));
    heap_caps_free(j);
    t.failAt = millis();
    t.backoff = 30000;
  }
}

// ---------------------------------------------------------------------------
//  Decode -> crop to the object -> scale. Runs on the loop task, only after
//  the worker's TLS is gone (the PNG exists only once the worker finished).
// ---------------------------------------------------------------------------
static uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool render(ThumbSlot& t, int16_t size, uint16_t bg) {
  const uint32_t w = be32(t.png + 16), h = be32(t.png + 20);     // IHDR
  if (w == 0 || h == 0 || w > 1024 || h > 1024) return false;

  lgfx::LGFX_Sprite big(&tft);
  big.setPsram(true);
  big.setColorDepth(16);
  if (!big.createSprite((int32_t)w, (int32_t)h)) return false;
  big.fillSprite(bg);
  const bool ok = big.drawPng(t.png, t.pngLen, 0, 0);
  big.releasePngMemory();
  if (!ok) { big.deleteSprite(); return false; }

  // Object bounds: every pixel the transparent plate render left non-bg.
  const uint16_t* px = (const uint16_t*)big.getBuffer();
  const uint16_t bgRaw = (uint16_t)((bg >> 8) | (bg << 8));      // sprite stores swapped RGB565
  int32_t x0 = (int32_t)w, y0 = (int32_t)h, x1 = -1, y1 = -1;
  for (uint32_t y = 0; y < h; y++) {
    const uint16_t* row = px + y * w;
    for (uint32_t x = 0; x < w; x++) {
      if (row[x] == bgRaw) continue;
      if ((int32_t)x < x0) x0 = x;
      if ((int32_t)x > x1) x1 = x;
      if ((int32_t)y < y0) y0 = y;
      y1 = y;
    }
  }
  if (x1 < 0) { x0 = 0; y0 = 0; x1 = w - 1; y1 = h - 1; }       // nothing found: whole image

  const float side = (float)std::max(x1 - x0 + 1, y1 - y0 + 1) * 1.08f + 2.0f;
  const float zoom = (float)size / side;

  if (!t.thumb) {
    t.thumb = new (std::nothrow) lgfx::LGFX_Sprite(&tft);
    if (!t.thumb) { big.deleteSprite(); return false; }
    t.thumb->setPsram(true);
    t.thumb->setColorDepth(16);
  }
  if (t.thumb->width() != size) {
    t.thumb->deleteSprite();
    if (!t.thumb->createSprite(size, size)) { big.deleteSprite(); return false; }
  }
  t.thumb->fillSprite(bg);
  big.setPivot((x0 + x1) / 2.0f, (y0 + y1) / 2.0f);
  big.pushRotateZoomWithAA(t.thumb, size / 2.0f, size / 2.0f, 0.0f, zoom, zoom);
  big.deleteSprite();

  t.size = size;
  t.bg = bg;
  g_thumbGen++;
  return true;
}

bool thumbGet(int16_t size, uint16_t bg, const uint16_t** px, uint32_t* gen) {
  ThumbSlot& t = g_slots[(uint8_t)(&displayedPrinter() - printers)];
  if (!t.png || t.renderFailed) return false;
  if (!t.thumb || t.size != size || t.bg != bg) {
    const unsigned long t0 = millis();
    if (!render(t, size, bg)) {
      t.renderFailed = true;                // bad PNG for this job: keep the fallback
      Serial.println("THUMB: render failed");
      return false;
    }
    Serial.printf("THUMB: rendered %dx%d in %lu ms\n", size, size, millis() - t0);
  }
  *px = (const uint16_t*)t.thumb->getBuffer();
  *gen = g_thumbGen;
  return true;
}

#endif // HAS_CARD_THUMB
