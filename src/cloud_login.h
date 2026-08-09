#ifndef CLOUD_LOGIN_H
#define CLOUD_LOGIN_H

#include <Arduino.h>
#include "config.h"
#include "bambu_state.h"

// ---------------------------------------------------------------------------
//  Bambu account sign-in, performed by the device itself
//
//  The account endpoint hands the token back in the response body, so no
//  browser session is involved:
//
//    POST api.bambulab.com/v1/user-service/user/login
//         {account,password}  or  {account,code}
//      -> {"accessToken":...}                    signed in
//      -> {"loginType":"verifyCode"}             a code was mailed out
//      -> {"loginType":"tfa","tfaKey":...}       authenticator app
//    POST api.bambulab.com/v1/user-service/user/sendemail/code
//         {email,type:"codeLogin"}               mail a code without a password
//
//  Only the authenticator leg lives on the website host, which is where Bambu
//  put it, and that one call needs the CSRF cookie echoed back in an
//  x-bbl-csrf-token header:
//
//    GET  bambulab.com/api/csrf    -> bbl_csrf_token cookie
//    POST bambulab.com/api/sign-in/tfa  {tfaKey,tfaCode}
//
//  The site's own /api/sign-in/form + /api/auth/token pair is deliberately NOT
//  used: it establishes a session but answers a non-slicer client with empty
//  token fields.
// ---------------------------------------------------------------------------

// Anything longer than these truncates when it is read back out of NVS, and a
// silently shortened credential fails forever without ever looking like the
// wrong length. The sign-in path refuses instead.
#define CLOUD_EMAIL_MAX     95
#define CLOUD_PASSWORD_MAX  127
#define CLOUD_TOKEN_MAX     1200

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

// True when that message is a complaint rather than an instruction. A refused
// code leaves the flow waiting on the same step, so the state cannot say this.
bool            cloudLoginLastFailed();

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

// Reachability self-test: post throwaway credentials to the account endpoint
// and fetch the CSRF cookie the authenticator leg needs. Reaching the
// "incorrect account or password" answer proves the whole path.
void cloudLoginSelfTest(String& out);

#else  // !HAS_CLOUD_LOGIN - flash-poor boards keep the paste-a-token path

inline CloudLoginState cloudLoginState()               { return CLOUD_LOGIN_IDLE; }
inline const char*     cloudLoginMessage()             { return ""; }
inline bool            cloudLoginLastFailed()          { return false; }
inline bool cloudLoginWithPassword(const char*, const char*) { return false; }
inline bool cloudLoginRequestEmailCode(const char*)    { return false; }
inline bool cloudLoginSubmitCode(const char*)          { return false; }
inline bool cloudLoginRefreshStored()                  { return false; }
inline bool cloudLoginCanAutoRefresh()                 { return false; }
inline void cloudLoginReset()                          {}
inline void cloudLoginSelfTest(String& out)            { out = "[]"; }

#endif // HAS_CLOUD_LOGIN

#endif // CLOUD_LOGIN_H
