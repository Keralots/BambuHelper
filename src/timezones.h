#ifndef TIMEZONES_H
#define TIMEZONES_H

#include <Arduino.h>

struct TimezoneRegion {
  const char* name;           // Display name, Windows-style (e.g., "(UTC+01:00) Amsterdam, Berlin, Rome")
  const char* posixString;    // POSIX TZ string for configTzTime()
};

// Get list of all supported timezones and count
const TimezoneRegion* getSupportedTimezones(size_t* count);

// Get default POSIX TZ string for old gmtOffsetMin value (migration)
const char* getDefaultTimezoneForOffset(int gmtOffsetMinutes);

// Resolve a POSIX TZ string back to its database index. Falls back to CET when
// the string is unknown. Never rely on a stored index - the database is sorted
// by UTC offset, so adding a region renumbers everything after it.
uint8_t resolveTimezoneIndex(const char* posixString);

#endif // TIMEZONES_H
