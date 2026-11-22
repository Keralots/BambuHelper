#include "portal_auth.h"

#include <Preferences.h>
#include <WebServer.h>
#include <time.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "config.h"
#include "web_template.h"   // extern WebServer server; webAssetCssVersion()
#include "wifi_manager.h"   // isAPMode()

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
static const char*    NS            = "bh_auth";
static const uint8_t  MAX_SESSIONS  = 6;
static const uint32_t SCREEN_PIN_MS = 120000;           // on-screen PIN lifetime
static const uint32_t SHOWPIN_MIN_GAP_MS = 10000;       // no re-roll more often than this
static const uint32_t SESSION_RAM_MS = 24UL * 3600000;  // "this browser session" tokens
static const uint8_t  FREE_TRIES    = 5;

struct Session {
  char     tok[33];
  uint32_t exp;      // unix seconds, persisted sessions only
  uint32_t bornMs;   // RAM-only sessions
  uint8_t  used;
  uint8_t  persist;
};
struct PersistedSession { char tok[33]; uint32_t exp; };

static uint8_t  g_mode = PA_OFF;
static uint8_t  g_salt[8];
static uint8_t  g_hash[32];
static bool     g_hasPin = false;
static Session  g_sess[MAX_SESSIONS];

static char     g_screenPin[7] = "";
static uint32_t g_screenPinBornMs = 0;
static bool     g_screenShow = false;

static uint8_t  g_fails = 0;
static uint32_t g_lockUntilMs = 0;

static bool clockOk() { return time(nullptr) > (time_t)NTP_SYNCED_EPOCH; }

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
static void hashPin(const char* pin, const uint8_t salt[8], uint8_t out[32]) {
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, salt, 8);
  mbedtls_sha256_update(&c, (const uint8_t*)pin, strlen(pin));
  mbedtls_sha256_finish(&c, out);
  mbedtls_sha256_free(&c);
}

static bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}

static bool validPinFormat(const String& p, uint8_t minLen, uint8_t maxLen) {
  if (p.length() < minLen || p.length() > maxLen) return false;
  for (size_t i = 0; i < p.length(); i++) if (!isdigit((unsigned char)p[i])) return false;
  return true;
}

static bool screenPinLive() {
  return g_screenPin[0] && (millis() - g_screenPinBornMs) < SCREEN_PIN_MS;
}

static bool ownPinMatches(const char* pin) {
  if (!g_hasPin) return false;
  uint8_t h[32];
  hashPin(pin, g_salt, h);
  return ctEqual(h, g_hash, 32);
}

static bool screenPinMatches(const char* pin) {
  if (!screenPinLive() || strlen(pin) != 6) return false;
  return ctEqual((const uint8_t*)pin, (const uint8_t*)g_screenPin, 6);
}

static bool lockedOut(uint32_t* secsLeft = nullptr) {
  if (g_fails < FREE_TRIES) return false;
  long left = (long)(g_lockUntilMs - millis());
  if (left <= 0) return false;
  if (secsLeft) *secsLeft = (uint32_t)(left + 999) / 1000;
  return true;
}

// 60 s after the 5th miss, doubling per further miss, capped at 32 min.
static void noteFailure() {
  if (g_fails < 255) g_fails++;
  if (g_fails >= FREE_TRIES) {
    uint8_t extra = g_fails - FREE_TRIES;
    if (extra > 5) extra = 5;
    g_lockUntilMs = millis() + (60000UL << extra);
  }
}

// ---------------------------------------------------------------------------
//  Persistence
// ---------------------------------------------------------------------------
static void saveSessions() {
  PersistedSession ps[MAX_SESSIONS];
  uint8_t n = 0;
  for (auto& s : g_sess)
    if (s.used && s.persist) { memcpy(ps[n].tok, s.tok, sizeof(ps[n].tok)); ps[n].exp = s.exp; n++; }
  Preferences p;
  p.begin(NS, false);
  if (n) p.putBytes("sess", ps, n * sizeof(PersistedSession));
  else   p.remove("sess");
  p.end();
}

static void saveConfig() {
  Preferences p;
  p.begin(NS, false);
  p.putUChar("mode", g_mode);
  if (g_hasPin) { p.putBytes("salt", g_salt, 8); p.putBytes("hash", g_hash, 32); }
  else          { p.remove("salt"); p.remove("hash"); }
  p.end();
}

void portalAuthInit() {
  memset(g_sess, 0, sizeof(g_sess));
  Preferences p;
  if (!p.begin(NS, true)) return;   // namespace absent: everything stays off
  g_mode = p.getUChar("mode", PA_OFF);
  if (g_mode > PA_SCREEN_PIN) g_mode = PA_OFF;
  g_hasPin = p.getBytes("salt", g_salt, 8) == 8 && p.getBytes("hash", g_hash, 32) == 32;
  if (g_mode == PA_OWN_PIN && !g_hasPin) g_mode = PA_SCREEN_PIN;   // never lock with no way in
  PersistedSession ps[MAX_SESSIONS];
  size_t len = p.getBytesLength("sess");
  if (len && len % sizeof(PersistedSession) == 0 && len <= sizeof(ps)) {
    p.getBytes("sess", ps, len);
    for (size_t i = 0; i < len / sizeof(PersistedSession); i++) {
      g_sess[i].used = 1; g_sess[i].persist = 1; g_sess[i].exp = ps[i].exp;
      memcpy(g_sess[i].tok, ps[i].tok, sizeof(g_sess[i].tok));
      g_sess[i].tok[32] = '\0';
    }
  }
  p.end();
}

void portalAuthFactoryReset() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}

uint8_t portalAuthMode() { return g_mode; }

// ---------------------------------------------------------------------------
//  Sessions
// ---------------------------------------------------------------------------
static void clearSessions() {
  memset(g_sess, 0, sizeof(g_sess));
  saveSessions();
}

// remember: 0 = this browser session, else days. Returns the token.
static const char* newSession(uint16_t rememberDays) {
  Session* slot = nullptr;
  for (auto& s : g_sess) if (!s.used) { slot = &s; break; }
  if (!slot) {   // full: evict the oldest RAM session, else the soonest-expiring persisted one
    for (auto& s : g_sess)
      if (!s.persist && (!slot || (int32_t)(s.bornMs - slot->bornMs) < 0)) slot = &s;
    if (!slot) {
      slot = &g_sess[0];
      for (auto& s : g_sess) if (s.exp < slot->exp) slot = &s;
    }
  }
  memset(slot, 0, sizeof(*slot));
  for (int i = 0; i < 16; i++) sprintf(slot->tok + i * 2, "%02x", (unsigned)(esp_random() & 0xFF));
  slot->used = 1;
  slot->bornMs = millis();
  // Without a synced clock an expiry cannot be written down; keep it in RAM.
  if (rememberDays && clockOk()) {
    slot->persist = 1;
    slot->exp = (uint32_t)time(nullptr) + (uint32_t)rememberDays * 86400UL;
  }
  if (slot->persist) saveSessions();
  return slot->tok;
}

static Session* sessionFromCookie() {
  String c = server.header("Cookie");
  int i = c.indexOf("bhs=");
  if (i < 0 || (int)c.length() < i + 4 + 32) return nullptr;
  char tok[33];
  memcpy(tok, c.c_str() + i + 4, 32);
  tok[32] = '\0';
  for (auto& s : g_sess) {
    if (!s.used || !ctEqual((const uint8_t*)s.tok, (const uint8_t*)tok, 32)) continue;
    bool expired = s.persist ? (clockOk() && (uint32_t)time(nullptr) >= s.exp)
                             : (millis() - s.bornMs) >= SESSION_RAM_MS;
    if (expired) {
      bool wasPersist = s.persist;
      memset(&s, 0, sizeof(s));
      if (wasPersist) saveSessions();
      return nullptr;
    }
    return &s;
  }
  return nullptr;
}

static void sendSessionCookie(const char* tok, uint16_t rememberDays) {
  String v = "bhs=";
  v += tok;
  v += "; Path=/; HttpOnly; SameSite=Strict";
  if (rememberDays) { v += "; Max-Age="; v += String((uint32_t)rememberDays * 86400UL); }
  server.sendHeader("Set-Cookie", v);
}

// HTTP Basic with the own PIN as password (any user name), for curl.
// 1 = valid, 0 = absent, -1 = present but wrong.
static int basicAuthState() {
  if (g_mode != PA_OWN_PIN || !server.hasHeader("Authorization")) return 0;
  String h = server.header("Authorization");
  if (!h.startsWith("Basic ")) return 0;
  String b64 = h.substring(6);
  b64.trim();
  uint8_t dec[64];
  size_t olen = 0;
  if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &olen, (const uint8_t*)b64.c_str(), b64.length()) != 0)
    return -1;
  dec[olen] = '\0';
  const char* colon = strchr((const char*)dec, ':');
  if (!colon) return -1;
  return ownPinMatches(colon + 1) ? 1 : -1;
}

// ---------------------------------------------------------------------------
//  Gate
// ---------------------------------------------------------------------------
bool portalAuthOk() {
  if (g_mode == PA_OFF || isAPMode()) return true;
  if (sessionFromCookie()) return true;
  return !lockedOut() && basicAuthState() == 1;
}

bool portalAuthGuard() {
  if (g_mode == PA_OFF || isAPMode()) return true;
  if (sessionFromCookie()) return true;
  uint32_t left = 0;
  if (!lockedOut(&left)) {
    int b = basicAuthState();
    if (b == 1) { g_fails = 0; return true; }
    if (b == -1) noteFailure();
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(401, "text/plain", "Sign-in required");
  return false;
}

// ---------------------------------------------------------------------------
//  On-screen PIN
// ---------------------------------------------------------------------------
const char* portalAuthScreenPin() {
  if (!g_screenShow) return nullptr;
  if (!screenPinLive() || g_mode == PA_OFF) { g_screenShow = false; return nullptr; }
  return g_screenPin;
}

void portalAuthDismissScreen() { g_screenShow = false; }

static void retireScreenPin() {
  g_screenPin[0] = '\0';
  g_screenShow = false;
}

// ---------------------------------------------------------------------------
//  Endpoints
// ---------------------------------------------------------------------------
static void handleShowPin() {
  if (g_mode == PA_OFF || isAPMode()) { server.send(400, "text/plain", "Security is off"); return; }
  uint32_t left = 0;
  if (lockedOut(&left)) {
    server.send(429, "text/plain", String("Too many attempts. Try again in ") + left + " s.");
    return;
  }
  // Re-asking within a few seconds re-shows the same PIN instead of rolling a
  // new one, so a double click does not invalidate what is on screen.
  if (!screenPinLive() || millis() - g_screenPinBornMs >= SHOWPIN_MIN_GAP_MS) {
    snprintf(g_screenPin, sizeof(g_screenPin), "%06u", (unsigned)(esp_random() % 1000000UL));
    g_screenPinBornMs = millis();
  }
  g_screenShow = true;
  server.send(200, "text/plain", "OK");
}

static uint16_t rememberArg() {
  long d = server.hasArg("remember") ? server.arg("remember").toInt() : 0;
  return (d == 7 || d == 30) ? (uint16_t)d : 0;
}

static void handleLogin() {
  if (g_mode == PA_OFF || isAPMode()) { server.send(200, "text/plain", "OK"); return; }
  uint32_t left = 0;
  if (lockedOut(&left)) {
    server.send(429, "text/plain", String("Too many attempts. Try again in ") + left + " s.");
    return;
  }
  String pin = server.arg("pin");
  pin.trim();
  bool viaScreen = screenPinMatches(pin.c_str());
  bool ok = viaScreen || (g_mode == PA_OWN_PIN && ownPinMatches(pin.c_str()));
  if (!ok) {
    noteFailure();
    server.send(401, "text/plain", lockedOut(&left)
                ? String("Wrong PIN. Locked for ") + left + " s."
                : String("Wrong PIN."));
    return;
  }
  g_fails = 0;
  if (viaScreen) retireScreenPin();
  uint16_t days = rememberArg();
  sendSessionCookie(newSession(days), days);
  server.send(200, "text/plain", "OK");
}

static void handleLogout() {
  if (Session* s = sessionFromCookie()) {
    bool wasPersist = s->persist;
    memset(s, 0, sizeof(*s));
    if (wasPersist) saveSessions();
  }
  server.sendHeader("Set-Cookie", "bhs=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  server.send(200, "text/plain", "OK");
}

static void handleGetConfig() {
  if (!portalAuthGuard()) return;
  String j = "{\"mode\":";
  j += g_mode;
  j += ",\"hasPin\":";
  j += g_hasPin ? "true" : "false";
  j += ",\"signedIn\":";
  j += sessionFromCookie() ? "true" : "false";
  j += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void handleSetConfig() {
  if (!portalAuthGuard()) return;
  long m = server.arg("mode").toInt();
  if (m < PA_OFF || m > PA_SCREEN_PIN) { server.send(400, "text/plain", "Bad mode"); return; }
  String pin = server.arg("pin");
  pin.trim();
  bool pinChanged = false;
  if (pin.length()) {
    if (!validPinFormat(pin, 4, 8)) { server.send(400, "text/plain", "PIN must be 4-8 digits."); return; }
    for (int i = 0; i < 8; i++) g_salt[i] = (uint8_t)(esp_random() & 0xFF);
    hashPin(pin.c_str(), g_salt, g_hash);
    g_hasPin = true;
    pinChanged = true;
  }
  if (m == PA_OWN_PIN && !g_hasPin) { server.send(400, "text/plain", "Set a PIN first."); return; }
  const uint8_t prev = g_mode;
  g_mode = (uint8_t)m;
  saveConfig();
  retireScreenPin();
  g_fails = 0;

  if (g_mode == PA_OFF) {
    clearSessions();
  } else if (prev == PA_OFF || pinChanged) {
    // Turning it on or changing the PIN signs every other browser out. The
    // browser doing it gets a fresh 30-day session so it is not locked out.
    clearSessions();
    sendSessionCookie(newSession(30), 30);
  }
  server.send(200, "text/plain", "OK");
}

void portalAuthRegisterRoutes() {
  server.on("/auth/login",   HTTP_POST, handleLogin);
  server.on("/auth/showpin", HTTP_POST, handleShowPin);
  server.on("/auth/logout",  HTTP_POST, handleLogout);
  server.on("/auth/config",  HTTP_GET,  handleGetConfig);
  server.on("/auth/config",  HTTP_POST, handleSetConfig);
}

// ---------------------------------------------------------------------------
//  Sign-in page (served at / instead of the portal)
// ---------------------------------------------------------------------------
static const char LOGIN_HTML_1[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en" data-theme="dark"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BambuHelper - Sign in</title>
<link rel="stylesheet" href="/app.css?v=)rawliteral";

static const char LOGIN_HTML_2[] PROGMEM = R"rawliteral(">
<script>(function(){try{var t=localStorage.getItem('bh-theme');if(t!=='light'&&t!=='dark'){t=(window.matchMedia&&window.matchMedia('(prefers-color-scheme: light)').matches)?'light':'dark';}document.documentElement.setAttribute('data-theme',t);}catch(e){}})();</script>
</head><body>
<div style="max-width:380px;margin:12vh auto 0;padding:0 16px">
  <div class="card">
    <div class="card-head"><div><h3>BambuHelper</h3><p id="lead"></p></div></div>
    <div class="field"><label for="pin">PIN</label>
      <input type="password" id="pin" inputmode="numeric" autocomplete="off" maxlength="8" autofocus></div>
    <div class="field"><label for="rem">Remember this browser</label>
      <select id="rem"><option value="0">Until the browser closes</option><option value="7">7 days</option><option value="30" selected>30 days</option></select></div>
    <div class="action-bar" style="gap:var(--sp-2);flex-wrap:wrap">
      <button type="button" class="btn btn-primary" onclick="signIn()">Sign in</button>
      <button type="button" class="btn btn-ghost" id="showBtn" onclick="showPin()">Show PIN on device</button>
    </div>
    <div id="msg" role="status" aria-live="polite" class="small" style="margin-top:var(--sp-2)"></div>
  </div>
</div>
<script>
var MODE=)rawliteral";

static const char LOGIN_HTML_3[] PROGMEM = R"rawliteral(;
function el(i){return document.getElementById(i);}
function msg(t,bad){var m=el('msg');m.textContent=t;m.style.color=bad?'var(--danger)':'';}
function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(function(r){return r.text().then(function(t){return{ok:r.ok,t:t};});});}
function showPin(){post('/auth/showpin','').then(function(r){if(r.ok){msg('A PIN is now on the device screen for 2 minutes.');el('pin').focus();}else msg(r.t,true);}).catch(function(){msg('Device not reachable.',true);});}
function signIn(){var p=el('pin').value.trim();if(!p){msg('Enter the PIN.',true);return;}
  post('/auth/login','pin='+encodeURIComponent(p)+'&remember='+el('rem').value).then(function(r){if(r.ok)location.reload();else{msg(r.t,true);el('pin').select();}}).catch(function(){msg('Device not reachable.',true);});}
el('pin').addEventListener('keydown',function(e){if(e.key==='Enter')signIn();});
el('lead').textContent=MODE==1?'Enter your portal PIN. Forgot it? Show a one-time PIN on the device.':'Press "Show PIN on device", then type the PIN from the screen.';
</script></body></html>)rawliteral";

void portalAuthSendLoginPage() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(LOGIN_HTML_1);
  server.sendContent(webAssetCssVersion());
  server.sendContent_P(LOGIN_HTML_2);
  server.sendContent(String(g_mode));
  server.sendContent_P(LOGIN_HTML_3);
  server.sendContent("");   // terminate the chunked response
}
