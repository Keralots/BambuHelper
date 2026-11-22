#ifndef PORTAL_AUTH_H
#define PORTAL_AUTH_H

#include <Arduino.h>

// Portal access control (WiFi & System > Security).
//   PA_OFF        - portal open to the LAN (default, pre-existing behaviour)
//   PA_OWN_PIN    - user-chosen 4-8 digit PIN; the on-screen PIN also works as recovery
//   PA_SCREEN_PIN - a fresh 6-digit PIN is shown on the device for each sign-in
// AP mode (first setup) is always open. Settings live in their own NVS
// namespace, so backups never carry them and factory reset clears them.
enum PortalAuthMode : uint8_t { PA_OFF = 0, PA_OWN_PIN = 1, PA_SCREEN_PIN = 2 };

void portalAuthInit();
void portalAuthRegisterRoutes();   // /auth/* endpoints
uint8_t portalAuthMode();

// True when the request may proceed. Protected handlers go through
// portalAuthGuard(), which also answers 401 and counts a wrong Basic PIN.
bool portalAuthOk();
bool portalAuthGuard();

void portalAuthSendLoginPage();
void portalAuthFactoryReset();

// On-screen PIN: non-null while it should be displayed.
const char* portalAuthScreenPin();
void portalAuthDismissScreen();     // hide it; the PIN itself stays valid until expiry

#endif // PORTAL_AUTH_H
