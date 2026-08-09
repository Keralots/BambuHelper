#include "cloud_login.h"

#if HAS_CLOUD_LOGIN

#include "bambu_cloud.h"
#include "settings.h"

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

// ---------------------------------------------------------------------------
//  Session state
// ---------------------------------------------------------------------------
static CloudLoginState g_state = CLOUD_LOGIN_IDLE;
static String g_message;
static String g_jar;        // "name=value; name=value" carried between calls
static String g_csrf;       // bbl_csrf_token value, echoed in a header
static String g_tfaKey;
static String g_email;
static bool   g_neededTwoFactor = false;

CloudLoginState cloudLoginState()   { return g_state; }
const char*     cloudLoginMessage() { return g_message.c_str(); }

void cloudLoginReset() {
  g_state = CLOUD_LOGIN_IDLE;
  g_message = "";
  g_jar = "";
  g_csrf = "";
  g_tfaKey = "";
  g_email = "";
  g_neededTwoFactor = false;
}

// ---------------------------------------------------------------------------
//  Cookie jar
// ---------------------------------------------------------------------------

// Replace or append one "name=value" pair.
static void jarPut(const String& pair) {
  int eq = pair.indexOf('=');
  if (eq <= 0) return;
  // An empty value must never replace a good one. /api/auth/token answers some
  // sessions with `Set-Cookie: token=` and that used to wipe the real session
  // token the sign-in had just handed us.
  if (eq == (int)pair.length() - 1) return;
  String name = pair.substring(0, eq);

  int at = g_jar.indexOf(name + "=");
  if (at >= 0 && (at == 0 || g_jar[at - 1] == ' ')) {
    int end = g_jar.indexOf(';', at);
    if (end < 0) end = g_jar.length();
    else if (end + 2 <= (int)g_jar.length()) end += 2;   // eat "; " too
    g_jar = g_jar.substring(0, at) + g_jar.substring(end);
  }
  if (g_jar.length() > 0 && !g_jar.endsWith("; ")) g_jar += "; ";
  g_jar += pair;
}

// Take every cookie out of the collected Set-Cookie lines.
static void jarAbsorb(const String& setCookieLines) {
  int pos = 0;
  while (pos < (int)setCookieLines.length()) {
    int nl = setCookieLines.indexOf('\n', pos);
    if (nl < 0) nl = setCookieLines.length();
    String line = setCookieLines.substring(pos, nl);
    line.trim();
    int semi = line.indexOf(';');
    String pair = semi < 0 ? line : line.substring(0, semi);
    pair.trim();
    if (pair.indexOf('=') > 0) {
      jarPut(pair);
      if (pair.startsWith("bbl_csrf_token=")) g_csrf = pair.substring(15);
    }
    pos = nl + 1;
  }
}

static String jarValue(const char* name) {
  String needle = String(name) + "=";
  int at = g_jar.indexOf(needle);
  while (at > 0 && g_jar[at - 1] != ' ') {          // avoid matching a suffix
    at = g_jar.indexOf(needle, at + 1);
  }
  if (at < 0) return "";
  int start = at + needle.length();
  int end = g_jar.indexOf(';', start);
  if (end < 0) end = g_jar.length();
  return g_jar.substring(start, end);
}

// ---------------------------------------------------------------------------
//  Raw HTTPS
//
//  HTTPClient is unusable here: with duplicate response headers it keeps only
//  the last one (the append branch in the core's handleHeaderResponse is
//  commented out), and sign-in hands back two Set-Cookie headers at once.
// ---------------------------------------------------------------------------
struct CloudResponse {
  int    status = -1;
  String setCookies;
  String body;
};

// The account API answers the sign-in calls; the website host only carries the
// authenticator leg.
static const char* apiHost(CloudRegion region) {
  return region == REGION_CN ? "api.bambulab.cn" : "api.bambulab.com";
}
static const char* siteHost(CloudRegion region) {
  return region == REGION_CN ? "bambulab.cn" : "bambulab.com";
}

// `useSession` covers the website host's cookie ritual: send the jar and the
// CSRF header, and keep whatever Set-Cookie comes back. The account API needs
// none of it, and mixing the two has already cost one debugging round.
static bool cloudRequest(const char* host, const char* path, const char* method,
                         const char* body, bool useSession, CloudResponse& out,
                         size_t maxBody = 4096) {

  WiFiClientSecure* tls = new (std::nothrow) WiFiClientSecure();
  if (!tls) { out.status = -2; return false; }

  // All three bounds matter: the web server is single-threaded, so a handshake
  // that never returns takes the device offline, OTA endpoint included.
  tls->setTimeout(8);
  tls->setHandshakeTimeout(8);
  tls->setCACertBundle(rootca_crt_bundle_start);

  esp_task_wdt_reset();
  if (!tls->connect(host, 443)) {
    delete tls;
    out.status = -1;
    return false;
  }

  String req = String(method) + " " + path + " HTTP/1.1\r\n";
  req += "Host: ";        req += host; req += "\r\n";
  req += "User-Agent: bambu_network_agent/01.09.05.01\r\n";
  req += "Accept: application/json\r\n";
  req += "Content-Type: application/json\r\n";
  if (useSession && g_jar.length() > 0)  { req += "Cookie: ";           req += g_jar;  req += "\r\n"; }
  if (useSession && g_csrf.length() > 0) { req += "x-bbl-csrf-token: "; req += g_csrf; req += "\r\n"; }
  req += "Connection: close\r\n";
  if (body) { req += "Content-Length: "; req += strlen(body); req += "\r\n"; }
  req += "\r\n";
  if (body) req += body;
  tls->print(req);

  // --- status line + headers ---
  long  contentLength = -1;
  bool  chunked = false;
  uint32_t start = millis();
  while (millis() - start < 12000) {
    esp_task_wdt_reset();
    if (!tls->connected() && !tls->available()) break;
    if (!tls->available()) { delay(5); continue; }

    String line = tls->readStringUntil('\n');
    line.trim();
    if (out.status < 0 && line.startsWith("HTTP/")) {
      int sp = line.indexOf(' ');
      if (sp > 0) out.status = line.substring(sp + 1, sp + 4).toInt();
    } else if (line.startsWith("Set-Cookie:") || line.startsWith("set-cookie:")) {
      out.setCookies += line.substring(11);
      out.setCookies += '\n';
    } else if (line.startsWith("Content-Length:") || line.startsWith("content-length:")) {
      contentLength = line.substring(15).toInt();
    } else if (line.indexOf("chunked") > 0 &&
               (line.startsWith("Transfer-Encoding:") || line.startsWith("transfer-encoding:"))) {
      chunked = true;
    } else if (line.length() == 0) {
      break;   // end of headers
    }
  }

  // --- body ---
  if (chunked) {
    while (millis() - start < 12000) {
      esp_task_wdt_reset();
      String sizeLine = tls->readStringUntil('\n');
      sizeLine.trim();
      long chunkSize = strtol(sizeLine.c_str(), nullptr, 16);
      if (chunkSize <= 0) break;
      while (chunkSize > 0 && millis() - start < 12000) {
        char buf[129];
        size_t want = chunkSize < 128 ? chunkSize : 128;
        size_t got = tls->readBytes(buf, want);
        if (got == 0) break;
        buf[got] = '\0';
        if (out.body.length() < maxBody) out.body += buf;
        chunkSize -= got;
      }
      tls->readStringUntil('\n');   // trailing CRLF
    }
  } else if (contentLength > 0) {
    long remaining = contentLength;
    while (remaining > 0 && millis() - start < 12000) {
      esp_task_wdt_reset();
      char buf[129];
      size_t want = remaining < 128 ? remaining : 128;
      size_t got = tls->readBytes(buf, want);
      if (got == 0) break;
      buf[got] = '\0';
      if (out.body.length() < maxBody) out.body += buf;
      remaining -= got;
    }
  }

  tls->stop();
  delete tls;
  esp_task_wdt_reset();

  if (useSession) jarAbsorb(out.setCookies);
  return out.status > 0;
}

// ---------------------------------------------------------------------------
//  Flow steps
// ---------------------------------------------------------------------------

static CloudRegion currentRegion() {
  return printers[0].config.region;
}

// Every write to the site host is CSRF-guarded, so the cookie has to exist
// before the authenticator POST.
static bool ensureCsrf() {
  if (g_csrf.length() > 0) return true;
  CloudResponse r;
  cloudRequest(siteHost(currentRegion()), "/api/csrf", "GET", nullptr, true, r);
  if (g_csrf.length() == 0) {
    g_message = "Could not reach the Bambu sign-in service.";
    return false;
  }
  return true;
}

// Pull the error text the site returns, so the user sees Bambu's own wording
// ("Incorrect account or password.") instead of a status code.
static void setErrorFrom(const CloudResponse& r, const char* fallback) {
  JsonDocument doc;
  if (r.body.length() > 0 && !deserializeJson(doc, r.body) && doc["error"].is<const char*>()) {
    g_message = (const char*)doc["error"];
  } else {
    g_message = fallback;
  }
  g_state = CLOUD_LOGIN_FAILED;
}

// Bambu spells the token differently depending on which of its own endpoints
// answered, and sometimes wraps the payload in "data".
static String extractToken(const String& body) {
  JsonDocument doc;
  if (body.length() == 0 || deserializeJson(doc, body)) return "";

  const char* keys[] = { "accessToken", "token" };
  for (size_t i = 0; i < 2; i++) {
    if (doc[keys[i]].is<const char*>())         return (const char*)doc[keys[i]];
    if (doc["data"][keys[i]].is<const char*>()) return (const char*)doc["data"][keys[i]];
  }
  return "";
}

static bool storeToken(const String& token) {
  if (token.length() == 0) {
    g_message = "Signed in, but no token came back.";
    g_state = CLOUD_LOGIN_FAILED;
    return false;
  }

  saveCloudToken(token.c_str());
  if (g_email.length() > 0) saveCloudEmail(g_email.c_str());

  // A stored password is only useful for silent re-login, which 2FA rules out.
  if (g_neededTwoFactor) clearCloudPassword();

  g_state = CLOUD_LOGIN_OK;
  g_message = "Signed in.";
  Serial.println("CLOUD: sign-in complete, token stored");
  return true;
}

// The account endpoint answers {account,password} and {account,code} with the
// same envelope: either the token, or which second factor it wants next.
static bool handleLoginReply(const CloudResponse& r) {
  if (r.status != 200) {
    setErrorFrom(r, "Sign-in was refused.");
    return false;
  }

  String token = extractToken(r.body);
  if (token.length() > 0) return storeToken(token);

  JsonDocument doc;
  deserializeJson(doc, r.body);
  const char* loginType = doc["loginType"].as<const char*>();
  if (!loginType) loginType = "";
  const char* tfaKey = doc["tfaKey"].as<const char*>();
  if (!tfaKey) tfaKey = "";

  // Measured on a TOTP account: Bambu answers the challenge with a tfaKey and
  // an EMPTY loginType, so keying off loginType alone never enters this branch.
  // The handed-out tfaKey is the challenge; that is what decides.
  if (strcmp(loginType, "tfa") == 0 || (loginType[0] == '\0' && tfaKey[0] != '\0')) {
    g_tfaKey = tfaKey;
    g_neededTwoFactor = true;
    g_state = CLOUD_LOGIN_NEED_TFA;
    g_message = "Enter the 6-digit code from your authenticator app.";
    return true;
  }
  if (strcmp(loginType, "verifyCode") == 0) {
    g_neededTwoFactor = true;
    g_state = CLOUD_LOGIN_NEED_EMAIL_CODE;
    g_message = "Enter the code Bambu just emailed you.";
    return true;
  }

  // Describe the envelope without printing it: strings are reported by length
  // only, since any of them may be a credential. Numbers, booleans and the two
  // enum-ish routing fields are safe to show and are what actually explains a
  // refusal.
  String fields;
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (fields.length() > 0) fields += ' ';
    fields += kv.key().c_str();
    fields += '=';

    const char* key = kv.key().c_str();
    if (kv.value().isNull()) {
      fields += "null";
    } else if (kv.value().is<const char*>()) {
      if (strcmp(key, "loginType") == 0 || strcmp(key, "accessMethod") == 0) {
        fields += '"';
        fields += kv.value().as<const char*>();
        fields += '"';
      } else {
        fields += "len";
        fields += strlen(kv.value().as<const char*>());
      }
    } else {
      fields += kv.value().as<String>();
    }
  }
  Serial.printf("CLOUD: login reply had no token, len=%d, fields: %s\n",
                r.body.length(), fields.c_str());

  g_message = "Bambu accepted the sign-in but sent no token.";
  g_state = CLOUD_LOGIN_FAILED;
  return false;
}

bool cloudLoginWithPassword(const char* email, const char* password) {
  cloudLoginReset();
  g_email = email;

  JsonDocument body;
  body["account"]  = email;
  body["password"] = password;
  String payload;
  serializeJson(body, payload);

  CloudResponse r;
  if (!cloudRequest(apiHost(currentRegion()), "/v1/user-service/user/login", "POST",
                    payload.c_str(), false, r)) {
    g_message = "Could not reach the Bambu sign-in service.";
    g_state = CLOUD_LOGIN_FAILED;
    return false;
  }
  Serial.printf("CLOUD: user/login HTTP %d, len=%d\n", r.status, r.body.length());
  return handleLoginReply(r);
}

bool cloudLoginRequestEmailCode(const char* email) {
  cloudLoginReset();
  g_email = email;
  g_neededTwoFactor = true;      // this path never has a password to store

  JsonDocument body;
  body["email"] = email;
  body["type"]  = "codeLogin";
  String payload;
  serializeJson(body, payload);

  CloudResponse r;
  if (!cloudRequest(apiHost(currentRegion()), "/v1/user-service/user/sendemail/code",
                    "POST", payload.c_str(), false, r)) {
    g_message = "Could not reach the Bambu sign-in service.";
    g_state = CLOUD_LOGIN_FAILED;
    return false;
  }
  Serial.printf("CLOUD: sendemail/code HTTP %d\n", r.status);

  if (r.status != 200) {
    setErrorFrom(r, "Bambu refused to send a code to that address.");
    return false;
  }

  g_state = CLOUD_LOGIN_NEED_EMAIL_CODE;
  g_message = "Enter the code Bambu just emailed you.";
  return true;
}

bool cloudLoginSubmitCode(const char* code) {
  const CloudLoginState keep = g_state;   // so a mistyped code can be retried
  if (keep != CLOUD_LOGIN_NEED_TFA && keep != CLOUD_LOGIN_NEED_EMAIL_CODE) {
    g_message = "Nothing is waiting for a code - start again.";
    return false;
  }

  // The authenticator leg is the one Bambu keeps on the website host, session
  // cookies and all; the emailed code goes back to the account endpoint.
  const bool tfa = (keep == CLOUD_LOGIN_NEED_TFA);
  if (tfa && !ensureCsrf()) {
    g_state = keep;
    return false;
  }

  JsonDocument body;
  if (tfa) {
    body["tfaKey"]  = g_tfaKey;
    body["tfaCode"] = code;
  } else {
    body["account"] = g_email;
    body["code"]    = code;
  }
  String payload;
  serializeJson(body, payload);

  const char* host = tfa ? siteHost(currentRegion()) : apiHost(currentRegion());
  const char* path = tfa ? "/api/sign-in/tfa" : "/v1/user-service/user/login";

  CloudResponse r;
  if (!cloudRequest(host, path, "POST", payload.c_str(), tfa, r)) {
    g_message = "Could not reach the Bambu sign-in service.";
    g_state = CLOUD_LOGIN_FAILED;
    return false;
  }
  Serial.printf("CLOUD: %s HTTP %d, len=%d\n", path, r.status, r.body.length());

  if (r.status != 200) {
    setErrorFrom(r, "That code was not accepted.");
    g_state = keep;
    return false;
  }

  if (!tfa) {
    bool ok = handleLoginReply(r);
    if (!ok) g_state = keep;
    return ok;
  }

  // The site answers the authenticator leg with a session cookie, and only
  // sometimes echoes the token in the body.
  String token = extractToken(r.body);
  if (token.length() == 0) token = jarValue("token");
  if (!storeToken(token)) {
    g_state = keep;
    return false;
  }
  return true;
}

bool cloudLoginCanAutoRefresh() {
  char pw[128];
  return loadCloudPassword(pw, sizeof(pw));
}

bool cloudLoginRefreshStored() {
  char email[96];
  char pw[128];
  if (!loadCloudEmail(email, sizeof(email)) || !loadCloudPassword(pw, sizeof(pw))) {
    return false;
  }

  Serial.println("CLOUD: refreshing token with the stored password");
  bool ok = cloudLoginWithPassword(email, pw);
  memset(pw, 0, sizeof(pw));

  if (ok && g_state != CLOUD_LOGIN_OK) {
    // A code prompt cannot be answered without a human, so a 2FA account can
    // never refresh silently. Drop the password rather than keep asking.
    Serial.println("CLOUD: account asks for 2FA - stored password cannot refresh it");
    clearCloudPassword();
    return false;
  }
  return ok && g_state == CLOUD_LOGIN_OK;
}

// ---------------------------------------------------------------------------
//  Self-test - proves the path without touching a real account
// ---------------------------------------------------------------------------
void cloudLoginSelfTest(String& out) {
  cloudLoginReset();

  // Throwaway credentials: reaching "Incorrect account or password." proves the
  // account endpoint answers this device. A multi-kilobyte HTML body instead
  // means Cloudflare stepped in - historically rate limiting, so wait it out
  // before reading anything more into it.
  CloudResponse login;
  bool ok1 = cloudRequest(apiHost(currentRegion()), "/v1/user-service/user/login", "POST",
                          "{\"account\":\"probe@bambuhelper.invalid\","
                          "\"password\":\"not-a-real-password\"}", false, login, 1024);

  // The authenticator leg needs this cookie, so it is worth reporting too.
  CloudResponse csrf;
  bool ok2 = cloudRequest(siteHost(currentRegion()), "/api/csrf", "GET", nullptr, true, csrf);

  out = "{\"login_http\":";
  out += ok1 ? login.status : -1;
  out += ",\"login_len\":";
  out += login.body.length();
  out += ",\"login_body\":\"";
  for (size_t i = 0; i < login.body.length() && i < 200; i++) {
    char c = login.body[i];
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c >= 0x20) out += c;
  }
  out += "\",\"csrf_http\":";
  out += ok2 ? csrf.status : -1;
  out += ",\"have_csrf\":";
  out += g_csrf.length() > 0 ? "true" : "false";
  out += "}";

  cloudLoginReset();
}

#endif // HAS_CLOUD_LOGIN
