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

// Plate thumbnail of a cloud task: GET /v1/iot-service/api/user/task/<id> and
// copy context.plates[index == plateIdx].thumbnail.url (a presigned S3 URL,
// valid ~24 h, no auth) into url. CA-verified only. Safe to call from a worker
// task: every client is local to the call.
bool cloudFetchPlateThumbUrl(const char* token, CloudRegion region, const char* taskId,
                             int plateIdx, char* url, size_t urlLen);

// GET an unauthenticated HTTPS URL into buf (no redirects, CA-verified only).
// Fails when the body is missing a Content-Length or exceeds cap.
bool cloudDownload(const char* url, uint8_t* buf, size_t cap, size_t* len);

#endif // BAMBU_CLOUD_H
