// =============================================================================
//  web_template.h - shared web-server template engine and the global WebServer
//  instance.
//
//  PROGMEM string literals (PAGE_HTML, PAGE_AP_HTML) live in include/web_pages.h
//  and are only included from src/web_template.cpp. Other translation units
//  (web_server.cpp etc.) talk to the engine through the small surface below.
// =============================================================================
#pragma once

#include <WebServer.h>

// Singleton web-server instance. Defined in src/web_server.cpp; used here and
// in src/web_template.cpp. Matches the existing extern pattern used elsewhere
// in the project (BambuState printers[], PrinterConfig printerConfigs[], etc.).
extern WebServer server;

// Stream the main configuration page with %TOKEN% substitution.
// Uses a 2 KB heap buffer + HTTP chunked transfer. Peak heap ~2 KB regardless
// of total page size.
void serveMainPage();

// Send the (small) AP-mode WiFi setup page. No placeholder substitution.
void serveApPage();
