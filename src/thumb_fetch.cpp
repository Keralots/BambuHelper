#include "thumb_fetch.h"
#include <new>

#if HAS_CARD_THUMB

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include "bambu_state.h"
#include "bambu_cloud.h"
#include "settings.h"        // dispSettings, loadCloudToken
#include "display_ui.h"      // tft (sprite parent)
#include "display_card.h"    // cardSkinActive

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
  CloudRegion region;
  uint32_t    gen;
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
  return j->len > 33 && memcmp(j->png, sig, 8) == 0;
}

static void worker(void* arg) {
  ThumbJob* j = (ThumbJob*)arg;
  j->ok = fetchInto(j);                     // every client lives and dies in here
  memset(j->token, 0, sizeof(j->token));
  Serial.printf("THUMB: fetch %s (%u bytes), stack left %u\n", j->ok ? "ok" : "failed",
                (unsigned)j->len, (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  portENTER_CRITICAL(&g_mux);
  g_done = j;
  portEXIT_CRITICAL(&g_mux);
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
//  Loop-side state: the job key, the downloaded PNG and the rendered thumb.
// ---------------------------------------------------------------------------
static uint32_t g_gen = 1;               // bumped whenever the wanted job changes
static uint8_t  g_keySlot = 0xFF;
static char     g_keyTask[24];
static uint16_t g_keyPlate = 0;
static uint8_t  g_fails = 0;
static unsigned long g_failAt = 0, g_backoff = 0;

static uint8_t* g_png = nullptr;
static size_t   g_pngLen = 0;
static lgfx::LGFX_Sprite* g_thumb = nullptr;
static int16_t  g_thumbSize = 0;
static uint16_t g_thumbBg = 0;
static uint32_t g_thumbGen = 0;
static bool     g_renderFailed = false;

static void dropResult() {
  if (g_png) { heap_caps_free(g_png); g_png = nullptr; }
  g_pngLen = 0;
  if (g_thumb) { g_thumb->deleteSprite(); delete g_thumb; g_thumb = nullptr; }
  g_thumbSize = 0;
  g_renderFailed = false;
}

static void resetKey() {
  g_gen++;
  dropResult();
  g_keySlot = 0xFF;
  g_keyTask[0] = '\0';
  g_keyPlate = 0;
  g_fails = 0;
  g_backoff = 0;
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
  if (j->gen == g_gen && j->ok) {
    dropResult();
    g_png = j->png;
    g_pngLen = j->len;
    g_fails = 0;
  } else {
    if (j->png) heap_caps_free(j->png);
    if (j->gen == g_gen) {                  // a real failure for the current job
      g_fails++;
      g_failAt = millis();
      g_backoff = g_backoff ? std::min<unsigned long>(g_backoff * 2, 600000UL) : 30000UL;
    }
  }
  heap_caps_free(j);
}

void thumbService() {
  collect();

  PrinterSlot& p = displayedPrinter();
  const BambuState& s = p.state;
  const bool want = cardSkinActive() && dispSettings.cardLeft == 2 && taskIdUsable(s.taskId);
  if (!want) {
    if (g_keySlot != 0xFF) resetKey();
    return;
  }
  if (g_keySlot != rotState.displayIndex || strcmp(g_keyTask, s.taskId) != 0 ||
      g_keyPlate != s.plateIdx) {
    resetKey();
    g_keySlot = rotState.displayIndex;
    strlcpy(g_keyTask, s.taskId, sizeof(g_keyTask));
    g_keyPlate = s.plateIdx;
  }
  if (g_png || g_running || g_fails >= MAX_FAILS) return;
  if (g_backoff && millis() - g_failAt < g_backoff) return;
  if (s.plateIdx == 0) return;              // pushall not in yet
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_HEAP ||
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < MIN_BLOCK) {
    g_failAt = millis();
    g_backoff = 5000;
    return;
  }

  ThumbJob* j = (ThumbJob*)heap_caps_calloc(1, sizeof(ThumbJob), MALLOC_CAP_SPIRAM);
  if (!j) return;
  if (!loadCloudToken(j->token, sizeof(j->token)) || !j->token[0]) {
    heap_caps_free(j);
    g_failAt = millis();                    // not signed in (yet): look again in a minute
    g_backoff = 60000;
    return;
  }
  strlcpy(j->taskId, s.taskId, sizeof(j->taskId));
  j->plate = s.plateIdx;
  j->region = p.config.region;
  j->gen = g_gen;
  g_running = true;
  if (xTaskCreatePinnedToCore(worker, "thumb", WORKER_STACK, j, 1, nullptr, 1) != pdPASS) {
    g_running = false;
    memset(j->token, 0, sizeof(j->token));
    heap_caps_free(j);
    g_failAt = millis();
    g_backoff = 30000;
  }
}

// ---------------------------------------------------------------------------
//  Decode -> crop to the object -> scale. Runs on the loop task, only after
//  the worker's TLS is gone (the PNG exists only once the worker finished).
// ---------------------------------------------------------------------------
static uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool render(int16_t size, uint16_t bg) {
  const uint32_t w = be32(g_png + 16), h = be32(g_png + 20);     // IHDR
  if (w == 0 || h == 0 || w > 1024 || h > 1024) return false;

  lgfx::LGFX_Sprite big(&tft);
  big.setPsram(true);
  big.setColorDepth(16);
  if (!big.createSprite((int32_t)w, (int32_t)h)) return false;
  big.fillSprite(bg);
  const bool ok = big.drawPng(g_png, g_pngLen, 0, 0);
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

  if (!g_thumb) {
    g_thumb = new (std::nothrow) lgfx::LGFX_Sprite(&tft);
    if (!g_thumb) { big.deleteSprite(); return false; }
    g_thumb->setPsram(true);
    g_thumb->setColorDepth(16);
  }
  if (g_thumb->width() != size) {
    g_thumb->deleteSprite();
    if (!g_thumb->createSprite(size, size)) { big.deleteSprite(); return false; }
  }
  g_thumb->fillSprite(bg);
  big.setPivot((x0 + x1) / 2.0f, (y0 + y1) / 2.0f);
  big.pushRotateZoomWithAA(g_thumb, size / 2.0f, size / 2.0f, 0.0f, zoom, zoom);
  big.deleteSprite();

  g_thumbSize = size;
  g_thumbBg = bg;
  g_thumbGen++;
  return true;
}

bool thumbGet(int16_t size, uint16_t bg, const uint16_t** px, uint32_t* gen) {
  if (!g_png || g_renderFailed) return false;
  if (!g_thumb || g_thumbSize != size || g_thumbBg != bg) {
    const unsigned long t0 = millis();
    if (!render(size, bg)) {
      g_renderFailed = true;                // bad PNG for this job: keep the fallback
      Serial.println("THUMB: render failed");
      return false;
    }
    Serial.printf("THUMB: rendered %dx%d in %lu ms\n", size, size, millis() - t0);
  }
  *px = (const uint16_t*)g_thumb->getBuffer();
  *gen = g_thumbGen;
  return true;
}

#endif // HAS_CARD_THUMB
