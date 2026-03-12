#include "bambu_cloud.h"
#include "settings.h"
#include "config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

// Make an HTTPS request. Caller must free *client after use.
// Returns HTTP status code, or -1 on error. Response body in `response`.
static int httpsRequest(const char* method, const char* url,
                        const char* body, const char* authToken,
                        String& response) {
  WiFiClientSecure* tls = new (std::nothrow) WiFiClientSecure();
  if (!tls) return -1;
  tls->setInsecure();
  tls->setTimeout(10);

  HTTPClient http;
  if (!http.begin(*tls, url)) {
    delete tls;
    return -1;
  }

  // Browser-like headers to avoid Cloudflare 403 blocks
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
  http.addHeader("Accept", "application/json, text/plain, */*");
  http.addHeader("Accept-Language", "en-US,en;q=0.9");
  http.addHeader("Accept-Encoding", "gzip, deflate");
  if (authToken && strlen(authToken) > 0) {
    String auth = "Bearer ";
    auth += authToken;
    http.addHeader("Authorization", auth);
  }

  int httpCode;
  if (strcmp(method, "GET") == 0) {
    httpCode = http.GET();
  } else {
    httpCode = http.POST(body ? body : "");
  }

  if (httpCode > 0) {
    response = http.getString();
  }

  http.end();
  delete tls;
  return httpCode;
}

// Base64url decode (JWT uses base64url, not standard base64)
static String base64UrlDecode(const char* input, size_t len) {
  // Convert base64url to standard base64
  String b64;
  b64.reserve(len + 4);
  for (size_t i = 0; i < len; i++) {
    char c = input[i];
    if (c == '-') c = '+';
    else if (c == '_') c = '/';
    b64 += c;
  }
  // Add padding
  while (b64.length() % 4 != 0) b64 += '=';

  // Decode
  size_t outLen = 0;
  unsigned char* decoded = nullptr;

  // Use mbedtls base64 decode (available on ESP32)
  mbedtls_base64_decode(nullptr, 0, &outLen, (const unsigned char*)b64.c_str(), b64.length());
  if (outLen == 0) return "";
  decoded = (unsigned char*)malloc(outLen + 1);
  if (!decoded) return "";
  if (mbedtls_base64_decode(decoded, outLen, &outLen, (const unsigned char*)b64.c_str(), b64.length()) != 0) {
    free(decoded);
    return "";
  }
  decoded[outLen] = '\0';
  String result((char*)decoded);
  free(decoded);
  return result;
}

// ---------------------------------------------------------------------------
//  Extract userId from JWT token
// ---------------------------------------------------------------------------
bool cloudExtractUserId(const char* token, char* userId, size_t len) {
  // JWT format: header.payload.signature
  const char* dot1 = strchr(token, '.');
  if (!dot1) return false;
  const char* payloadStart = dot1 + 1;
  const char* dot2 = strchr(payloadStart, '.');
  if (!dot2) return false;

  size_t payloadLen = dot2 - payloadStart;
  String decoded = base64UrlDecode(payloadStart, payloadLen);
  if (decoded.length() == 0) return false;

  JsonDocument doc;
  if (deserializeJson(doc, decoded)) return false;

  // Try common uid field names
  const char* uid = nullptr;
  if (doc["uid"].is<const char*>()) uid = doc["uid"];
  else if (doc["sub"].is<const char*>()) uid = doc["sub"];
  else if (doc["user_id"].is<const char*>()) uid = doc["user_id"];

  if (!uid) return false;

  snprintf(userId, len, "u_%s", uid);
  return true;
}

// ---------------------------------------------------------------------------
//  Process login/verify response
// ---------------------------------------------------------------------------
static CloudResult processLoginResponse(int httpCode, const String& response,
                                         const char* email) {
  if (httpCode < 0) return CLOUD_NET_ERROR;

  JsonDocument doc;
  if (deserializeJson(doc, response)) return CLOUD_PARSE_ERROR;

  // Check if 2FA is required
  // Bambu API returns loginType or similar indicator
  if (doc["loginType"].is<const char*>()) {
    const char* lt = doc["loginType"];
    if (strcmp(lt, "verifyCode") == 0 || strcmp(lt, "tfa") == 0) {
      return CLOUD_NEED_VERIFY;
    }
  }

  // Check for error response
  if (httpCode == 400 || httpCode == 401 || httpCode == 403) {
    return CLOUD_BAD_CREDS;
  }

  // Look for token in response
  const char* accessToken = nullptr;
  if (doc["accessToken"].is<const char*>()) {
    accessToken = doc["accessToken"];
  } else if (doc["data"].is<JsonObject>() && doc["data"]["accessToken"].is<const char*>()) {
    accessToken = doc["data"]["accessToken"];
  }

  if (!accessToken || strlen(accessToken) == 0) {
    // Might be a 2FA trigger response without explicit loginType
    if (httpCode == 200) return CLOUD_NEED_VERIFY;
    return CLOUD_PARSE_ERROR;
  }

  // Success — save token
  saveCloudToken(accessToken);
  saveCloudEmail(email);

  // Extract and store userId
  char uid[32] = {0};
  cloudExtractUserId(accessToken, uid, sizeof(uid));
  // userId is stored per-printer in handleSave, but we log it here
  Serial.printf("CLOUD: Login OK, userId=%s\n", uid);

  return CLOUD_OK;
}

// ---------------------------------------------------------------------------
//  Login with email + password
// ---------------------------------------------------------------------------
CloudResult cloudLogin(const char* email, const char* password) {
  Serial.printf("CLOUD: Login attempt for %s\n", email);

  String url = String(BAMBU_API_BASE) + "/v1/user-service/user/login";

  JsonDocument body;
  body["account"] = email;
  body["password"] = password;
  String bodyStr;
  serializeJson(body, bodyStr);

  String response;
  int httpCode = httpsRequest("POST", url.c_str(), bodyStr.c_str(), nullptr, response);

  Serial.printf("CLOUD: Login HTTP %d, len=%d\n", httpCode, response.length());
  if (httpCode == 403) {
    Serial.println("CLOUD: 403 — likely Cloudflare block. First 200 chars:");
    Serial.println(response.substring(0, 200));
  }
  return processLoginResponse(httpCode, response, email);
}

// ---------------------------------------------------------------------------
//  Verify 2FA code
// ---------------------------------------------------------------------------
CloudResult cloudVerifyCode(const char* email, const char* code) {
  Serial.printf("CLOUD: Verify 2FA for %s code=%s\n", email, code);

  String url = String(BAMBU_API_BASE) + "/v1/user-service/user/login";

  JsonDocument body;
  body["account"] = email;
  body["code"] = code;
  String bodyStr;
  serializeJson(body, bodyStr);

  String response;
  int httpCode = httpsRequest("POST", url.c_str(), bodyStr.c_str(), nullptr, response);

  Serial.printf("CLOUD: Verify HTTP %d, len=%d\n", httpCode, response.length());
  return processLoginResponse(httpCode, response, email);
}

// ---------------------------------------------------------------------------
//  Fetch device list
// ---------------------------------------------------------------------------
int cloudFetchDevices(const char* token, CloudPrinter* out, int maxDevices) {
  String url = String(BAMBU_API_BASE) + "/v1/iot-service/api/user/bind";

  String response;
  int httpCode = httpsRequest("GET", url.c_str(), nullptr, token, response);

  Serial.printf("CLOUD: Devices HTTP %d, len=%d\n", httpCode, response.length());

  if (httpCode != 200) return 0;

  // Parse with filter for memory efficiency
  JsonDocument filter;
  filter["message"] = true;
  JsonObject df = filter["data"][0].to<JsonObject>();
  df["dev_id"] = true;
  df["name"] = true;
  df["dev_product_name"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) {
    Serial.println("CLOUD: Failed to parse device list");
    return 0;
  }

  JsonArray devices = doc["data"].as<JsonArray>();
  if (devices.isNull()) return 0;

  int count = 0;
  for (JsonObject dev : devices) {
    if (count >= maxDevices) break;
    strlcpy(out[count].serial, dev["dev_id"] | "", sizeof(out[count].serial));
    strlcpy(out[count].name, dev["name"] | "", sizeof(out[count].name));
    strlcpy(out[count].model, dev["dev_product_name"] | "", sizeof(out[count].model));
    count++;
  }

  Serial.printf("CLOUD: Found %d devices\n", count);
  return count;
}
