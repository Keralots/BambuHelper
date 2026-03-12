#ifndef BAMBU_CLOUD_H
#define BAMBU_CLOUD_H

#include <Arduino.h>

enum CloudResult {
  CLOUD_OK,
  CLOUD_NEED_VERIFY,    // 2FA verification code required
  CLOUD_BAD_CREDS,      // wrong email or password
  CLOUD_NET_ERROR,      // network / TLS failure
  CLOUD_PARSE_ERROR     // unexpected API response
};

struct CloudPrinter {
  char serial[20];
  char name[24];
  char model[16];
};

// Step 1: Login with email + password.
// Returns CLOUD_NEED_VERIFY if Bambu requires a 2FA email code.
// Returns CLOUD_OK if login succeeded (no 2FA), token saved to NVS.
CloudResult cloudLogin(const char* email, const char* password);

// Step 2: Submit the 6-digit verification code from email.
// On success: saves token + userId to NVS, returns CLOUD_OK.
CloudResult cloudVerifyCode(const char* email, const char* code);

// Fetch list of printers bound to the account.
// token: the access token from NVS.
// out: array to fill, maxDevices: array capacity.
// Returns number of devices found (0 on error).
int cloudFetchDevices(const char* token, CloudPrinter* out, int maxDevices);

// Extract user ID from JWT token payload.
// Populates userId with "u_{uid}" format string.
bool cloudExtractUserId(const char* token, char* userId, size_t len);

#endif // BAMBU_CLOUD_H
