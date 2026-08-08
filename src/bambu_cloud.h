#ifndef BAMBU_CLOUD_H
#define BAMBU_CLOUD_H

#include <Arduino.h>
#include "bambu_state.h"

// Region-aware URL helpers
const char* getBambuBroker(CloudRegion region);
const char* getBambuApiBase(CloudRegion region);

// Extract user ID from JWT token payload.
// Populates userId with "u_{uid}" format string.
bool cloudExtractUserId(const char* token, char* userId, size_t len);

// Fetch userId from Bambu profile API (fallback for non-JWT tokens).
// Populates userId with "u_{uid}" format string.
bool cloudFetchUserId(const char* token, char* userId, size_t len, CloudRegion region);

// Fetch the raw device-bind JSON for the account behind `token`. The caller
// picks the fields it needs; the payload lists every printer bound to it.
bool cloudFetchDeviceList(const char* token, CloudRegion region, String& response);

#endif // BAMBU_CLOUD_H
