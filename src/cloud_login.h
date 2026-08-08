#ifndef CLOUD_LOGIN_H
#define CLOUD_LOGIN_H

#include <Arduino.h>
#include "config.h"
#include "bambu_state.h"

// ---------------------------------------------------------------------------
//  Bambu account sign-in, performed by the device itself
//
//  The obvious endpoint - api.bambulab.com/v1/user-service/user/login - is the
//  one path a Cloudflare WAF rule refuses from an ESP32 (measured: 403 with an
//  "Attention Required!" page, regardless of agent headers or cookies). The
//  website's own sign-in endpoints on bambulab.com are not filtered, so the
//  flow goes through those instead, exactly as a browser would:
//
//    GET  /api/csrf                -> bbl_csrf_token cookie
//    POST /api/sign-in/form        {account,password} or {account,code}
//    POST /api/sign-in/tfa         {tfaKey,tfaCode}
//    GET  /api/auth/token          -> the cloud token, plus a refresh token
//
//  Every call needs the CSRF cookie echoed back in an x-bbl-csrf-token header.
// ---------------------------------------------------------------------------

enum CloudLoginState : uint8_t {
  CLOUD_LOGIN_IDLE = 0,
  CLOUD_LOGIN_NEED_TFA,         // authenticator code (account has 2FA)
  CLOUD_LOGIN_NEED_EMAIL_CODE,  // code mailed to the account
  CLOUD_LOGIN_OK,
  CLOUD_LOGIN_FAILED
};

#if HAS_CLOUD_LOGIN

CloudLoginState cloudLoginState();
const char*     cloudLoginMessage();   // last error or progress line, for the UI

// Password sign-in. Returns false on any hard failure; the state says whether a
// second factor is still needed.
bool cloudLoginWithPassword(const char* email, const char* password);

// Mail a login code to the account. No password is sent or stored.
bool cloudLoginRequestEmailCode(const char* email);

// Second step: whichever code the current state is waiting for.
bool cloudLoginSubmitCode(const char* code);

// Sign in again with the saved password, for when the token expires. Only works
// on accounts without 2FA - anything else needs a human with a code.
bool cloudLoginRefreshStored();

// True when a stored password exists and the last sign-in needed no 2FA.
bool cloudLoginCanAutoRefresh();

void cloudLoginReset();

// Reachability self-test: fetch the CSRF cookie and post throwaway credentials.
// Reaching the "incorrect account or password" answer proves the whole path.
void cloudLoginSelfTest(String& out);

#else  // !HAS_CLOUD_LOGIN - flash-poor boards keep the paste-a-token path

inline CloudLoginState cloudLoginState()               { return CLOUD_LOGIN_IDLE; }
inline const char*     cloudLoginMessage()             { return ""; }
inline bool cloudLoginWithPassword(const char*, const char*) { return false; }
inline bool cloudLoginRequestEmailCode(const char*)    { return false; }
inline bool cloudLoginSubmitCode(const char*)          { return false; }
inline bool cloudLoginRefreshStored()                  { return false; }
inline bool cloudLoginCanAutoRefresh()                 { return false; }
inline void cloudLoginReset()                          {}
inline void cloudLoginSelfTest(String& out)            { out = "[]"; }

#endif // HAS_CLOUD_LOGIN

#endif // CLOUD_LOGIN_H
