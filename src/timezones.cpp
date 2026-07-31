#include "timezones.h"

// POSIX TZ format: STDoffsetDST[offset],start[/time],end[/time]
// Example: "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"
//   CET-1    = standard time name + offset (sign inverted vs UTC)
//   CEST     = daylight time name
//   M3.5.0   = March, last (5) Sunday (0)
//   /02:00   = transition at 02:00

// Sorted by UTC offset (west to east), Windows-style display names
static const TimezoneRegion timezoneDatabase[] = {
  {"(UTC-10:00) Hawaii",                                         "HST10"},
  {"(UTC-09:00) Alaska",                                         "AKST9AKDT,M3.2.0/02:00,M11.1.0/02:00"},
  {"(UTC-08:00) Pacific Time (Los Angeles, Vancouver)",          "PST8PDT,M3.2.0/02:00,M11.1.0/02:00"},
  {"(UTC-07:00) Mountain Time (Denver, Edmonton)",               "MST7MDT,M3.2.0/02:00,M11.1.0/02:00"},
  {"(UTC-07:00) Arizona (Phoenix)",                              "MST7"},
  {"(UTC-06:00) Central Time (Chicago, Winnipeg)",               "CST6CDT,M3.2.0/02:00,M11.1.0/02:00"},
  {"(UTC-06:00) Mexico City",                                    "CST6CDT,M4.1.0/02:00,M10.5.0/02:00"},
  {"(UTC-05:00) Eastern Time (New York, Toronto)",               "EST5EDT,M3.2.0/02:00,M11.1.0/02:00"},
  {"(UTC-05:00) Colombia, Peru",                                 "COT5"},
  {"(UTC-04:00) Santiago",                                       "CLT4CLST,M9.2.0/00:00,M4.2.0/00:00"},
  {"(UTC-03:00) Buenos Aires, Sao Paulo",                        "BRT3"},
  {"(UTC) Coordinated Universal Time",                           "UTC0"},
  {"(UTC+00:00) Reykjavik",                                     "GMT0"},
  {"(UTC+00:00) Dublin, Edinburgh, London",                      "GMT0BST,M3.5.0/01:00,M10.5.0/02:00"},
  {"(UTC+00:00) Lisbon",                                         "WET0WEST,M3.5.0/01:00,M10.5.0/02:00"},
  {"(UTC+01:00) Amsterdam, Berlin, Rome, Stockholm, Vienna",     "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"},
  {"(UTC+01:00) Brussels, Copenhagen, Madrid, Paris",            "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"},
  {"(UTC+01:00) Belgrade, Budapest, Prague, Warsaw, Zagreb",     "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"},
  {"(UTC+01:00) West Central Africa",                            "WAT-1"},
  {"(UTC+02:00) Athens, Bucharest, Helsinki",                    "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00"},
  {"(UTC+02:00) Tallinn, Riga, Vilnius",                         "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00"},
  {"(UTC+02:00) Kyiv",                                           "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00"},
  {"(UTC+02:00) Cairo",                                          "EET-2"},
  {"(UTC+02:00) Jerusalem",                                      "IST-2IDT,M3.4.0/02:00,M10.2.0/02:00"},
  {"(UTC+02:00) Johannesburg, Harare",                           "SAST-2"},
  {"(UTC+02:00) Minsk",                                          "EET-2"},
  {"(UTC+03:00) Moscow, St. Petersburg",                         "MSK-3"},
  {"(UTC+03:00) Istanbul",                                       "TRT-3"},
  {"(UTC+03:00) Riyadh, Kuwait",                                 "AST-3"},
  {"(UTC+03:00) Nairobi",                                        "EAT-3"},
  {"(UTC+04:00) Dubai, Abu Dhabi",                               "GST-4"},
  {"(UTC+05:00) Karachi, Islamabad",                             "PKT-5"},
  {"(UTC+05:30) Mumbai, New Delhi, Kolkata",                     "IST-5:30"},
  {"(UTC+07:00) Bangkok, Hanoi, Jakarta",                        "ICT-7"},
  {"(UTC+08:00) Beijing, Shanghai, Hong Kong, Taipei",           "CST-8"},
  {"(UTC+08:00) Singapore, Kuala Lumpur",                        "SGT-8"},
  {"(UTC+08:00) Perth",                                          "AWST-8"},
  {"(UTC+09:00) Tokyo, Osaka",                                   "JST-9"},
  {"(UTC+09:00) Seoul",                                          "KST-9"},
  {"(UTC+09:30) Adelaide, Darwin",                               "ACST-9:30ACDT,M10.1.0/02:00,M4.1.0/03:00"},
  {"(UTC+10:00) Sydney, Melbourne, Brisbane",                    "AEST-10AEDT,M10.1.0/02:00,M4.1.0/03:00"},
  {"(UTC+12:00) Auckland, Wellington",                           "NZST-12NZDT,M9.5.0/02:00,M4.1.0/03:00"},
};

static const size_t TIMEZONE_COUNT = sizeof(timezoneDatabase) / sizeof(TimezoneRegion);

const TimezoneRegion* getSupportedTimezones(size_t* count) {
  if (count) *count = TIMEZONE_COUNT;
  return timezoneDatabase;
}

uint8_t resolveTimezoneIndex(const char* posixString) {
  if (posixString && posixString[0] != '\0') {
    for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
      if (strcmp(timezoneDatabase[i].posixString, posixString) == 0) return (uint8_t)i;
    }
  }
  // No match (empty value, or a string from a newer firmware). Fall back to CET
  // by looking the entry up rather than hardcoding its position - the database
  // is ordered by UTC offset, so any inserted region shifts every index after it.
  for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
    if (strcmp(timezoneDatabase[i].posixString, "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00") == 0)
      return (uint8_t)i;
  }
  return 0;
}

const char* getDefaultTimezoneForOffset(int gmtOffsetMinutes) {
  switch (gmtOffsetMinutes) {
    case 0:     return "GMT0BST,M3.5.0/01:00,M10.5.0/02:00";
    case 60:    return "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00";
    case 120:   return "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00";
    case 180:   return "MSK-3";
    case -300:  return "EST5EDT,M3.2.0/02:00,M11.1.0/02:00";
    case -360:  return "CST6CDT,M3.2.0/02:00,M11.1.0/02:00";
    case -420:  return "MST7MDT,M3.2.0/02:00,M11.1.0/02:00";
    case -480:  return "PST8PDT,M3.2.0/02:00,M11.1.0/02:00";
    case 540:   return "JST-9";
    case 480:   return "CST-8";
    case 600:   return "AEST-10AEDT,M10.1.0/02:00,M4.1.0/03:00";
    case 720:   return "NZST-12NZDT,M9.5.0/02:00,M4.1.0/03:00";
    case 330:   return "IST-5:30";
    case 240:   return "GST-4";
    case 300:   return "PKT-5";
    case -600:  return "HST10";
    default:    return nullptr;
  }
}
