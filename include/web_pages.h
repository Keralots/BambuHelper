// =============================================================================
//  web_pages.h - PROGMEM string literals for the BambuHelper web UI.
//
//  *** This header is included by EXACTLY ONE translation unit ***
//  (src/web_template.cpp). The `static const char[] PROGMEM` storage class
//  means each including TU gets its own copy, so multiple includes would
//  bloat flash. Keep it that way.
//
//  Two literals are defined here:
//    PAGE_AP_HTML  - small WiFi-setup page served in AP mode (no %TOKEN%
//                    substitution, sent verbatim by serveApPage()).
//    PAGE_HTML_1/_ERRORS/_2 - the full configuration page (shell + sidebar +
//                    one section block per nav entry), streamed back to back by
//                    serveMainPage() and run through resolvePlaceholder() by
//                    streamTemplate() in chunks of 2 KB. The middle piece is
//                    the Printer Errors section and only exists where the
//                    feature is compiled in - the esp32c3 has ~5 KB of app slot
//                    left and cannot carry markup it can never show.
//
//  Section blocks live as hidden <div id="sec-..."> elements inside the main
//  panel. Sidebar clicks toggle the `hidden` attribute - every section lives
//  in the DOM at all times, so user-edited input values persist across nav.
// =============================================================================
#pragma once

// Two gates live in here. HAS_HMS_UI keeps the Printer Errors section in its
// own literal, so boards without the feature carry none of its markup;
// HAS_CLOUD_LOGIN does the same for the on-device sign-in block.
#include "config.h"

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  AP-mode page (minimal WiFi setup only)
// -----------------------------------------------------------------------------
static const char PAGE_AP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>BambuHelper Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0D1117;color:#E6EDF3;padding:16px;max-width:420px;margin:0 auto}
h1{color:#58A6FF;font-size:22px;margin-bottom:6px}
.sub{color:#8B949E;font-size:13px;margin-bottom:20px}
.card{background:#161B22;border:1px solid #30363D;border-radius:8px;padding:16px;margin-bottom:16px}
.card h2{color:#58A6FF;font-size:16px;margin-bottom:12px}
label{display:block;color:#8B949E;font-size:13px;margin-bottom:4px;margin-top:10px}
input[type=text],input[type=password]{width:100%;padding:8px 10px;border:1px solid #30363D;border-radius:6px;background:#0D1117;color:#E6EDF3;font-size:14px;outline:none}
input:focus{border-color:#58A6FF}
.btn{display:block;width:100%;padding:12px;border:none;border-radius:6px;font-size:15px;font-weight:600;cursor:pointer;margin-top:16px;text-align:center;background:#238636;color:#fff}
.btn:hover{background:#2EA043}
</style>
</head><body>
<h1>BambuHelper</h1>
<p class="sub">Initial Setup</p>
<div class="card">
  <h2>Connect to WiFi Network</h2>
  <p style="font-size:12px;color:#8B949E;margin-bottom:10px">Enter your WiFi credentials. After saving, the device will restart and connect to your network. You can then access the full settings at the device's IP address.</p>
  <label for="ssid">WiFi SSID</label>
  <input type="text" id="ssid" placeholder="Your WiFi network name">
  <label for="pass">WiFi Password</label>
  <input type="password" id="pass" placeholder="WiFi password">
  <div style="margin-top:6px"><input type="checkbox" id="showpass" onchange="document.getElementById('pass').type=this.checked?'text':'password'" style="vertical-align:middle"><label for="showpass" style="color:#8B949E;font-size:12px;margin:0 0 0 4px;display:inline">Show password</label></div>
  <button class="btn" onclick="saveWifi()">Save WiFi &amp; Restart</button>
  <div id="msg" role="status" aria-live="polite" aria-atomic="true" style="margin-top:10px;font-size:13px;text-align:center"></div>
</div>
<script>
function saveWifi(){
  var s=document.getElementById('ssid').value,p=document.getElementById('pass').value;
  if(!s){document.getElementById('msg').innerHTML='<span style="color:#F85149">Enter SSID</span>';return;}
  document.getElementById('msg').innerHTML='<span style="color:#58A6FF">Saving...</span>';
  var d=new URLSearchParams();d.append('ssid',s);d.append('pass',p);
  fetch('/save/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:d.toString()})
    .then(function(){document.body.innerHTML='<div style="text-align:center;padding-top:80px"><h2 style="color:#3FB950">WiFi Saved!</h2><p style="color:#8B949E;margin-top:10px">Restarting... Connect to your WiFi and open the device IP address in a browser.</p></div>';})
    .catch(function(e){document.getElementById('msg').style.color='#F85149';document.getElementById('msg').textContent='Connection error';console.warn('saveWifi:',e);});
}
</script>
</body></html>
)rawliteral";

// -----------------------------------------------------------------------------
//  Main page - shell + sidebar + one hidden section <div> block per nav entry.
//
//  Field IDs (audited against web_server.cpp, NOT the design handoff README):
//    Printer:  pname, ip, serial, code, connmode, region, cl_token, cl_serial,
//              cl_pname, dualp, gs0..gs5, lx0..lx1, px0..px2, is0..is1, amsv
//    Display:  bright, nighten, nstart, nend, nbright, ssbright, afterprint,
//              fmins, dack, fintm, kps, pong, abar, slbl, timem, fanmp, hidelp, invcol,
//              cydcls, cyd32e, rskin, rotation, tz, use24h, datefmt, clk_time, clk_date,
//              clk_size, clk_dsize, clk_hidedate, noz_max, bed_max, cht_max, pwr_max,
//              gsmooth, warn_thr, warn_clr,
//              clr_bg, clr_track, clr_pbar, clr_eta, clr_fin, clr_stok,
//              clr_pname, clr_txt, clr_txtd, clr_dorc, clr_doro, bulk_a/l/v,
//              prg/noz/bed/pfn/afn/afr/cfn/exh/cht/hbk/pwr/lyr + _a/_l/_v,
//              prg/noz/bed/pfn/afn/afr/cfn/exh/cht/hbk/pwr/lyr/clk/ams/nzr/nzl/dor + _lbl
//    Errors:   hmsen, hmssev, hmsauto, hmsonl, hmsm0..hmsm3 (composed into the
//              hmsmask arg)
//    Hardware: rotmode, rotinterval, btntype, btnpin, buzzen (DOUBLE Z!),
//              buzpin, buzqs, buzqe, buzclick, buzbeden, buzbedtemp, leden,
//              leddrv, ledpin, ledping, ledpinb, ledanode, ledbr, ledfxmd,
//              ledfxsec, ledfxbr, ledauto, ledpause, lederr, lederrsec,
//              ledcidl, ledcprn, ledcpau, ledcfin, ledcerr, batshow
//    WiFi:     ssid, pass, showpass2, netmode, net_ip, net_gw, net_sn,
//              net_dns, showip, importFile, otaFile
//    Power:    tsm_cur, tsm_tar, tsm_en, tsm_pt, tsm_ip, tsm_dm (radio), tsm_pi,
//              tsm_ao, tsm_ad, tsm_aod, tsm_slot
//    Diag:     dbglog
// -----------------------------------------------------------------------------
static const char PAGE_HTML_1[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BambuHelper</title>
<link rel="stylesheet" href="/app.css?v=%CSSVER%">
<script>
/* Apply theme before first paint to avoid dark-flash:
   localStorage override wins; otherwise follow OS prefers-color-scheme;
   default stays dark (the <html data-theme="dark"> fallback). */
(function(){try{var t=localStorage.getItem('bh-theme');if(t!=='light'&&t!=='dark'){t=(window.matchMedia&&window.matchMedia('(prefers-color-scheme: light)').matches)?'light':'dark';}document.documentElement.setAttribute('data-theme',t);}catch(e){}})();
</script>
</head>
<body>

<!-- ============ Top bar ============ -->
<div class="topbar">
  <button class="hamburger" id="hamburger" aria-label="Toggle menu" type="button">
    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 6h18M3 12h18M3 18h18"/></svg>
  </button>
  <div class="brand">
    <div class="mark">B</div>
    <span>BambuHelper</span>
    <span class="version-pill">%FW_VER%</span>
  </div>
  <div class="section-title" id="sectionTitle">Printer Settings</div>
  <div class="topbar-actions">
    <span class="status-dot" id="topStatusDot" title="Printer 1 connection"><span id="topStatusText">-</span></span>
    %TOPBAR_DOTS%
    <button class="icon-btn" id="themeToggle" aria-label="Toggle theme" type="button">
      <svg id="iconSun" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="display:none"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41"/></svg>
      <svg id="iconMoon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>
    </button>
  </div>
</div>

<div class="app">
<!-- ============ Sidebar ============ -->
<aside class="sidebar" id="sidebar">
  <h4>Configuration</h4>
  <button class="nav-item" type="button" data-section="printer" aria-current="true"><span>Printer</span></button>
  <button class="nav-item" type="button" data-section="display"><span>Display</span></button>
%HMS_NAV%
  <button class="nav-item" type="button" data-section="hardware"><span>Hardware</span></button>
  <button class="nav-item" type="button" data-section="advanced"><span>Advanced</span></button>
  <h4>Network</h4>
  <button class="nav-item" type="button" data-section="wifi"><span>WiFi &amp; System</span></button>
  <button class="nav-item" type="button" data-section="power"><span>Power</span></button>
  <h4>Support</h4>
  <button class="nav-item" type="button" data-section="diag"><span>Diagnostics</span></button>
  <div class="sidebar-footer">
    <div>BambuHelper</div>
    <div style="margin-top:2px">%BOARD% &middot; %FW_VER%</div>
  </div>
</aside>

<!-- ============ Main panel ============ -->
<main class="main" id="mainPanel">

<!-- ===== Section 1: Printer ===== -->
<div class="section" id="sec-printer">
  <div class="section-intro">
    <h2>Printer Settings</h2>
    <p>Configure up to %MAXP% printers. Each slot is independent - pick LAN or Bambu Cloud per slot.</p>
  </div>

  <div class="card">
    <div class="card-head card-head-tabs">
      <h3>Active slot</h3>
      <div class="slot-tabs" id="printerTabs">
        <button class="tab-btn active" id="tab0" type="button" onclick="selectPrinterTab(0)">Printer 1</button>
        %PRINTER_TABS%
      </div>
    </div>
    <p class="card-desc">Pick which slot you are editing. Settings on screen reflect the selected slot only.</p>
    <div id="printerStatus" class="%STATUS_CLASS%" role="status" aria-live="polite">%STATUS_TEXT%</div>
    <div id="liveStats"></div>
  </div>

  <div class="card">
    <div class="card-head">
      <div>
        <h3>Connection</h3>
        <p>LAN mode is fastest and stays on your network. Bambu Cloud works for printers anywhere on the internet.</p>
      </div>
    </div>

    <label class="field-label" for="connmode">Connection mode</label>
    <select id="connmode" onchange="toggleConnMode()">
      <option value="local" %MODE_LOCAL%>LAN Mode</option>
      <option value="cloud_all" %MODE_CLOUD_ALL%>Bambu Cloud (All printers)</option>
    </select>

    <div id="localFields" style="margin-top:var(--sp-3)">
      <div class="row">
        <div class="field">
          <label for="pname">Printer name</label>
          <input type="text" id="pname" value="%PNAME%" placeholder="My P1S" maxlength="23">
          <div class="hint">Shown on the device display. Up to 23 characters.</div>
        </div>
        <div class="field">
          <label for="ip">Printer IP address</label>
          <input type="text" id="ip" class="mono" value="%IP%" placeholder="192.168.1.xxx">
          <div class="hint">Bambu printers accept only ~3 LAN connections at once (other monitors, Panda Touch, Home Assistant, Bambu Studio). When full the printer stops answering and looks offline - free a slot or use Cloud mode.</div>
        </div>
      </div>
      <div class="row">
        <div class="field">
          <label for="serial">Serial number</label>
          <input type="text" id="serial" class="mono" value="%SERIAL%" placeholder="01P00A000000000" maxlength="19" style="text-transform:uppercase">
          <div class="hint">Required - used for the MQTT topic.</div>
        </div>
        <div class="field">
          <label for="code">LAN access code</label>
          <input type="text" id="code" class="mono" placeholder="Leave blank to keep current" maxlength="8">
          <div class="hint">Find it on the printer: Settings -&gt; Network -&gt; LAN Mode.</div>
        </div>
      </div>
      <div class="field" style="margin-top:var(--sp-2)">
        <button type="button" class="btn btn-ghost btn-sm" id="lan_scanBtn" onclick="scanLan('lan')">Scan local network</button>
        <select id="lan_devsel" onchange="pickLanDevice('lan')" style="display:none;margin-top:var(--sp-2);width:100%"></select>
        <div class="hint">Finds printers on the same Wi-Fi and fills serial + IP. Same subnet only.</div>
      </div>
    </div>

    <div id="cloudFields" style="display:none;margin-top:var(--sp-3)">
      <div class="row">
        <div class="field">
          <label for="region">Server region</label>
          <!-- US and EU reach the same broker and the same API, so they are one
               choice here; either stored value keeps this option selected. -->
          <select id="region">
            <option value="us" %REGION_US%%REGION_EU%>Europe / Americas</option>
            <option value="cn" %REGION_CN%>China (CN)</option>
          </select>
        </div>
        <div class="field">
          <label>Cloud status</label>
          <div class="hstack" style="height:36px;gap:var(--sp-3)">
            <span id="cloudStatus" style="font-size:13px;color:var(--text-mid)">%CLOUD_STATUS%</span>
            <button type="button" class="btn btn-danger btn-sm" id="cloudLogoutBtn" onclick="cloudLogout()" title="Removes the token, the account email and any stored password">Sign out</button>
          </div>
        </div>
      </div>

)rawliteral"
#if HAS_CLOUD_LOGIN
R"rawliteral(
      <div class="field" style="margin-top:var(--sp-3)">
        <label for="cl_method">Account access</label>
        <select id="cl_method" onchange="clSetAuthMethod(this.value)">
          <option value="signin">Sign in with your Bambu account</option>
          <option value="token">Paste an access token</option>
        </select>
      </div>

      <div id="cl_signinWrap" style="margin-top:var(--sp-3)">
        <p class="small text-dim" style="margin:0 0 var(--sp-3);line-height:1.6">The device signs in and fetches the token itself. This page has no password and runs over plain HTTP, so what you type here crosses your network unencrypted - use it on a network you trust.</p>

        <div class="seg" style="margin-bottom:var(--sp-3)">
          <button type="button" id="cl-mode-pass-btn" aria-pressed="true" onclick="clSetLoginMode('password')">Password</button>
          <button type="button" id="cl-mode-code-btn" aria-pressed="false" onclick="clSetLoginMode('code')">Email code</button>
        </div>

        <div class="field">
          <label for="cl_email">Bambu account email</label>
          <input type="email" id="cl_email" placeholder="you@example.com" autocomplete="username">
        </div>

        <div id="cl_passWrap">
          <div class="field" style="margin-top:var(--sp-3)">
            <label for="cl_pass">Password</label>
            <input type="password" id="cl_pass" autocomplete="current-password">
          </div>
          <label class="hstack" style="gap:var(--sp-2);margin-top:var(--sp-2);font-size:12.5px;color:var(--text-mid)">
            <input type="checkbox" id="cl_savePass">
            <span>Remember the password so the device can renew the token by itself. Only works on accounts without two-factor authentication - with 2FA on the password is discarded and you sign in again when the token expires.</span>
          </label>
        </div>

        <div class="hstack" style="gap:var(--sp-3);margin-top:var(--sp-3);flex-wrap:wrap">
          <button type="button" class="btn btn-primary btn-sm" id="cl_signinBtn" onclick="cloudSignIn()">Sign in</button>
          <span id="cl_loginMsg" class="small text-dim" role="status" aria-live="polite"></span>
        </div>

        <div id="cl_codeWrap" style="display:none;margin-top:var(--sp-3);padding:var(--sp-3);background:var(--bg-sub);border:1px solid var(--line-soft);border-radius:var(--radius-s)">
          <label for="cl_code" id="cl_codeLabel">Verification code</label>
          <div class="hstack" style="gap:var(--sp-3);margin-top:var(--sp-2);flex-wrap:wrap">
            <input type="text" id="cl_code" class="mono" inputmode="numeric" maxlength="8" style="max-width:140px" placeholder="000000">
            <button type="button" class="btn btn-primary btn-sm" onclick="cloudSubmitCode()">Verify</button>
          </div>
        </div>
      </div>
)rawliteral"
#endif
R"rawliteral(
      <div id="cl_tokenWrap" style="margin-top:var(--sp-3)">
        <div class="banner">
          <span class="dot" style="background:var(--info)"></span>
          <div>
            <strong>Token-based access.</strong>
            <div class="small text-dim" style="margin-top:4px">A token expires after ~90 days, and sooner if you "log out everywhere" or change your password. Paste a fresh one when that happens.</div>
          </div>
        </div>

        <details>
          <summary>How to get your access token</summary>
          <div>
            <ol style="margin:0;padding-left:20px;font-size:12.5px;color:var(--text-mid);line-height:1.7">
              <li>Open <a href="https://bambulab.com" target="_blank" style="color:var(--accent)">bambulab.com</a> and log in.</li>
              <li>Press <span class="mono" style="border:1px solid var(--line);border-radius:4px;padding:1px 5px;font-size:11px;background:var(--bg-sub)">F12</span> to open DevTools.</li>
              <li>Go to <strong>Application</strong> (Chrome/Edge) or <strong>Storage</strong> (Firefox).</li>
              <li>Expand <strong>Cookies</strong> -&gt; click <strong>bambulab.com</strong>.</li>
              <li>Find the cookie named <span class="mono">token</span> and copy its value.</li>
              <li>Paste it below and save.</li>
            </ol>
            <p style="margin:var(--sp-3) 0 0;font-size:12.5px;color:var(--text-mid);line-height:1.6">You do not have to do this by hand: sign in above, or let the <strong>Companion Tool</strong> push a token from your PC. <a href="https://keralots.github.io/BambuHelper/cloud-token.html" target="_blank" style="color:var(--accent)">All three ways, step by step</a></p>
          </div>
        </details>

        <div class="field" style="margin-top:var(--sp-3)">
          <label for="cl_token">Access token</label>
          <textarea id="cl_token" rows="3" placeholder="Paste your Bambu Cloud token here..."></textarea>
        </div>
      </div>

      <!-- The pickers come before the serial field on purpose: put them after it
           and people start typing a serial by hand without seeing them. -->
      <div class="field" style="margin-top:var(--sp-4)">
        <label>Pick your printer</label>
        <div class="hstack" style="gap:var(--sp-2);flex-wrap:wrap">
          <button type="button" class="btn btn-ghost btn-sm" id="cl_acctBtn" onclick="loadAccountPrinters()">My printers</button>
          <button type="button" class="btn btn-ghost btn-sm" id="cl_scanBtn" onclick="scanLan('cloud')">Scan local network</button>
        </div>
        <select id="cl_acctsel" onchange="pickAccountPrinter()" style="display:none;margin-top:var(--sp-2);width:100%"></select>
        <select id="cl_devsel" onchange="pickLanDevice('cloud')" style="display:none;margin-top:var(--sp-2);width:100%"></select>
        <div class="hint"><strong>My printers</strong> lists everything bound to your account - needs a working token or sign-in. <strong>Scan local network</strong> finds printers on the same Wi-Fi. Picking one fills in the fields below.</div>
      </div>

      <div class="row" style="margin-top:var(--sp-3)">
        <div class="field">
          <label for="cl_serial">Printer serial number</label>
          <input type="text" id="cl_serial" class="mono" value="%SERIAL%" placeholder="01P00A000000000" maxlength="19">
          <div class="hint">Must match exactly (UPPERCASE). A wrong serial connects but shows no data.</div>
        </div>
        <div class="field">
          <label for="cl_pname">Printer name</label>
          <input type="text" id="cl_pname" value="%PNAME%" placeholder="My Printer" maxlength="23">
        </div>
      </div>
    </div>

    <div class="action-bar">
      <button type="button" class="btn btn-primary" onclick="savePrinter()">Save Printer Settings</button>
      <button type="button" class="btn btn-danger" onclick="clearPrinter()">Clear Printer</button>
    </div>
  </div>

  <div class="card">
    <div class="card-head">
      <div>
        <h3>Gauge Layout</h3>
        <p id="glDesc">Per-printer display slots. The standard 2x3 grid is always shown while printing. On 240x320 and 320x480 boards the extra rows below only render when the matching grid mode (<em>Landscape 8 slots</em> / <em>Portrait 9 slots</em>) is enabled under <strong>Advanced</strong>. The last pair is the two gauges on the Ready and Print complete screens - handy for watching chamber temperature while a part cools. That pair is not used when <em>Keep print status screen after completion</em> is on, since the print grid stays up instead. Set any slot to <em>Empty</em> to hide it.</p>
      </div>
      <button type="button" class="btn btn-ghost btn-sm" style="white-space:nowrap" onclick="resetGaugeLayout()">Reset to default</button>
    </div>
%AMSV_ROW%
    <div class="row-divider" style="margin-top:var(--sp-3)" id="topRowDivider">&#9650; Print screen - top row</div>
    <div class="gauge-grid">
      <div class="cell"><label>Top-left</label><select id="gs0" class="gauge-slot-sel"></select></div>
      <div class="cell"><label>Top-center</label><select id="gs1" class="gauge-slot-sel"></select></div>
      <div class="cell"><label>Top-right</label><select id="gs2" class="gauge-slot-sel"></select></div>
    </div>
    <div id="bottomRowGroup">
      <div class="row-divider">&#9660; Print screen - bottom row</div>
      <div class="gauge-grid">
        <div class="cell"><label>Bot-left</label><select id="gs3" class="gauge-slot-sel"></select></div>
        <div class="cell"><label>Bot-center</label><select id="gs4" class="gauge-slot-sel"></select></div>
        <div class="cell"><label>Bot-right</label><select id="gs5" class="gauge-slot-sel"></select></div>
      </div>
    </div>
%EXTRAS_SECTIONS%
    <div class="row-divider" id="idlePairDivider">&#9670; Ready / Print complete</div>
    <div class="gauge-grid">
      <div class="cell"><label>Left gauge</label><select id="is0" class="gauge-slot-sel"></select></div>
      <div class="cell"><label>Right gauge</label><select id="is1" class="gauge-slot-sel"></select></div>
    </div>
    <div class="action-bar">
      <button type="button" class="btn btn-primary" onclick="saveGaugeLayout()">Save Gauge Layout</button>
    </div>
  </div>

  <div class="card">
    <div class="card-head">
      <div>
        <h3>Chamber light</h3>
        <p>Control this printer's chamber light. Automation runs per-printer; manual buttons toggle it now. Works in both LAN and Cloud mode (X1, P-series, H2). Dual-bar printers (H2C/H2D) switch both bars together.</p>
      </div>
      <span id="lightStateLbl" class="status-pill status-na">Light: -</span>
    </div>
    <label class="check-row">
      <input type="checkbox" id="loff_fin" value="1">
      <label for="loff_fin">Turn off after a successful print</label>
    </label>
    <label class="check-row">
      <input type="checkbox" id="loff_fail" value="1">
      <label for="loff_fail">Turn off after a failed or cancelled print</label>
    </label>
    <label class="check-row">
      <input type="checkbox" id="lon_start" value="1">
      <label for="lon_start">Turn on when a print starts</label>
    </label>
    <div class="field"><label for="ldelay">Off delay (applies to both off rules)</label>
      <div class="hstack"><input type="number" id="ldelay" min="0" max="60" value="5">
      <span class="text-dim small">min &middot; 0 = immediate</span></div>
    </div>
    <div class="action-bar">
      <button type="button" class="btn btn-ghost btn-sm" onclick="setLight('on')">Light On</button>
      <button type="button" class="btn btn-ghost btn-sm" onclick="setLight('off')">Light Off</button>
      <button type="button" class="btn btn-primary" onclick="saveLightConfig()">Save Light Settings</button>
    </div>
  </div>
</div>

<!-- ===== Section 2: Display ===== -->
<div class="section" id="sec-display" hidden>
  <div class="section-intro">
    <h2>Display</h2>
    <p>Tune brightness, the clock, what happens after a print finishes, and how gauges look.</p>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Screen</h3></div></div>
    <div class="field">
      <label for="rotation">Screen orientation</label>
      <select id="rotation">
        <option value="0" %ROT0%>0&deg; (default)</option>
        <option value="1" %ROT1%>90&deg;</option>
        <option value="2" %ROT2%>180&deg;</option>
        <option value="3" %ROT3%>270&deg;</option>
      </select>
    </div>
    <label class="check-row">
      <input type="checkbox" id="abar" value="1" %ABAR% onchange="toggleSetting('abar',this.checked)">
      <label for="abar">Animated progress bar (shimmer effect)</label>
    </label>
    <div class="field">
      <label for="timem">Time display</label>
      <select id="timem" onchange="toggleSetting('timem',this.value)">
        <option value="0" %TIMEM0%>Finish time / ETA (default) - "ETA: 17:45"</option>
        <option value="1" %TIMEM1%>Remaining time - "Remaining: 2h 05m"</option>
        <option value="2" %TIMEM2%>Both - "17:45 &middot; 2h05m"</option>
      </select>
      <span class="text-dim small">applies immediately</span>
    </div>
    <label class="check-row">
      <input type="checkbox" id="fanmp" value="1" %FMP% onchange="toggleSetting('fanmp',this.checked)">
      <label for="fanmp">Match printer fan % (10% steps - applies on next printer update)</label>
    </label>
    <label class="check-row">
      <input type="checkbox" id="hidelp" value="1" %HIDELP% onchange="toggleSetting('hidelp',this.checked);applyHideReadoutToPowerDM()">
      <label for="hidelp">Hide layer/power line in status bar</label>
    </label>
    <div class="help-text" style="padding-left:28px">Frees width for the filament name. Use when you already show layer count and/or power as gauges. Applies to the print screen only (not the finish summary).</div>
%AMST_ROW%
%INVCOL_ROW%
%CYD_PANEL_ROW%
%ROUND_SKIN_ROW%
  </div>

  <div class="card" style="%BL_DISP%">
    <div class="card-head"><div><h3>Brightness</h3></div></div>
    <div class="field">
      <label class="hstack" style="justify-content:space-between" for="bright"><span>Daytime brightness</span><span class="mono text-dim" id="brightVal">%BRIGHT%</span></label>
      <input type="range" id="bright" min="10" max="255" step="5" value="%BRIGHT%"
             oninput="document.getElementById('brightVal').textContent=this.value;sendBrightness(this.value)">
    </div>
    <label class="check-row">
      <input type="checkbox" id="nighten" value="1" %NIGHTEN% onchange="document.getElementById('nightFields').style.display=this.checked?'block':'none';toggleSetting('nighten',this.checked)">
      <label for="nighten">Night mode (scheduled dimming)</label>
    </label>
    <div id="nightFields" style="display:%NIGHTDISP%;padding:var(--sp-3);background:var(--bg-sub);border:1px solid var(--line-soft);border-radius:var(--radius-s);margin-top:var(--sp-3)">
      <div class="row">
        <div class="field"><label for="nstart">Dim from</label><select id="nstart">%NIGHT_START_OPTS%</select></div>
        <div class="field"><label for="nend">Dim until</label><select id="nend">%NIGHT_END_OPTS%</select></div>
      </div>
      <div class="field" style="margin-bottom:0">
        <label class="hstack" style="justify-content:space-between" for="nbright"><span>Night brightness</span><span class="mono text-dim" id="nbrightVal">%NBRIGHT%</span></label>
        <input type="range" id="nbright" min="0" max="255" step="5" value="%NBRIGHT%"
               oninput="document.getElementById('nbrightVal').textContent=this.value">
        <div class="hint">Recommended: 20-50 for night use. Requires NTP time sync.</div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>After a print completes</h3></div></div>
    <div class="field">
      <label for="afterprint">When the print finishes</label>
      <select id="afterprint" onchange="toggleAfterPrint()">
        <option value="0" %AP_CLOCK0%>Switch to clock/screensaver immediately</option>
        <option value="1" %AP_F1%>Show finish screen for 1 minute</option>
        <option value="3" %AP_F3%>Show finish screen for 3 minutes</option>
        <option value="5" %AP_F5%>Show finish screen for 5 minutes</option>
        <option value="10" %AP_F10%>Show finish screen for 10 minutes</option>
        <option value="custom" %AP_CUSTOM%>Custom duration</option>
        <option value="keepon" %AP_KEEPON%>Keep finish screen visible</option>
      </select>
    </div>
    <div id="customMinsWrap" class="field" style="display:%CUSTOM_DISP%">
      <label for="fmins">Custom minutes</label>
      <input type="number" id="fmins" min="1" max="999" value="%FMINS%" style="max-width:120px">
    </div>
    <div id="afterFinWrap" class="field">
      <label for="afterfin">After the finish screen</label>
      <select id="afterfin" onchange="toggleAfterPrint()">
        <option value="clock" %AF_CLOCK%>Show clock / screensaver</option>
        <option value="off" %AF_OFF%>Turn display and LED off</option>
      </select>
      <div class="hint">Turning the display off needs a button or touchscreen to wake the device. A new print wakes it automatically.</div>
    </div>
    <label class="check-row">
      <input type="checkbox" id="dack" value="1" %DACK% onchange="toggleSetting('dack',this.checked)">
      <label for="dack">Wait for door open before timeout</label>
    </label>
    <label class="check-row">
      <input type="checkbox" id="fintm" value="1" %FINTM% onchange="toggleSetting('fintm',this.checked)">
      <label for="fintm">Show the completion time on the finish screen</label>
    </label>
    <div class="help-text" style="padding-left:28px">Adds the clock time the print ended, e.g. <span class="mono">Print Complete! @ 14:32</span>. On 240 px wide screens the longer line is drawn in a smaller font - turn this off to keep the headline large.</div>
    <label class="check-row">
      <input type="checkbox" id="kps" value="1" %KPS% onchange="toggleSetting('kps',this.checked)">
      <label for="kps">Keep print status screen after completion</label>
    </label>
    <div class="help-text" style="padding-left:28px">Show last print stats instead of the finish screen. Drying screen still takes priority.</div>
    <div style="%GLOW_DISP%">
      <div class="field" style="margin-top:var(--sp-4)">
        <label for="glowm">Edge glow light effect</label>
        <select id="glowm" onchange="toggleGlowFields();toggleSetting('glowm',this.value)">
          <option value="0" %GLOWM0%>Off</option>
          <option value="1" %GLOWM1%>Single color</option>
          <option value="2" %GLOWM2%>Rainbow</option>
        </select>
        <div class="hint">Animated light around the screen border when a print completes. A failed print always glows red.</div>
      </div>
      <div id="glowFields" style="display:%GLOWF_DISP%">
        <div class="field" id="glowClrWrap" style="display:%GLOWC_DISP%">
          <label for="glow_clr">Glow color</label>
          <input type="color" id="glow_clr" value="%GLOW_CLR%">
        </div>
        <div class="field">
          <label for="glows">Animation style</label>
          <select id="glows" onchange="toggleSetting('glows',this.value)">
            <option value="0" %GLOWS0%>Sweep (light travels around the border)</option>
            <option value="1" %GLOWS1%>Pulse (whole border breathes)</option>
            <option value="2" %GLOWS2%>Storm (shattered, flickering shards)</option>
          </select>
        </div>
        <div class="field">
          <label for="glowd">Duration</label>
          <select id="glowd" onchange="toggleSetting('glowd',this.value)">
            <option value="0" %GLOWD0%>One-minute burst</option>
            <option value="1" %GLOWD1%>Until dismissed (button, touch or door)</option>
            <option value="2" %GLOWD2%>Burst + short reminder every 5 minutes</option>
          </select>
        </div>
        <button type="button" class="btn btn-ghost btn-sm" onclick="glowTestNow()">Test effect on device (5 s)</button>
      </div>
    </div>
    <div class="field" style="margin-top:var(--sp-4);%BL_DISP%">
      <label class="hstack" style="justify-content:space-between" for="ssbright"><span>Screensaver brightness</span><span class="mono text-dim" id="ssbrightVal">%SSBRIGHT%</span></label>
      <input type="range" id="ssbright" min="0" max="255" step="5" value="%SSBRIGHT%"
             oninput="document.getElementById('ssbrightVal').textContent=this.value">
      <div class="hint">Brightness when clock/screensaver is active. Set to 0 to turn off backlight.</div>
    </div>
    <label class="check-row" id="pong-row" style="%PONG_DISP%">
      <input type="checkbox" id="pong" value="1" %PONG% onchange="toggleSetting('pong',this.checked)">
      <label for="pong">Breakout clock (animated game as screensaver)</label>
    </label>
    <div class="help-text" style="padding-left:28px">Without a physical button, clock is always shown instead of turning the display off.</div>
  </div>

  <details class="card card-collapsible">
    <summary>
      <div><h3>Clock</h3><p>Timezone, format, and color of the clock screen.</p></div>
    </summary>
    <div class="card-body">
      <div class="field">
        <label for="tz">Timezone (DST switches automatically)</label>
        <select id="tz"></select>
      </div>
      <label class="check-row">
        <input type="checkbox" id="use24h" value="1" %USE24H% onchange="toggleSetting('use24h',this.checked)">
        <label for="use24h">24-hour time format</label>
      </label>
      <div class="field" style="margin-top:var(--sp-3)">
        <label for="datefmt">Date format</label>
        <select id="datefmt">
          <option value="0" %DATEFMT0%>DD.MM.YYYY (31.12.2025)</option>
          <option value="1" %DATEFMT1%>DD-MM-YYYY (31-12-2025)</option>
          <option value="2" %DATEFMT2%>MM/DD/YYYY (12/31/2025)</option>
          <option value="3" %DATEFMT3%>YYYY-MM-DD (2025-12-31)</option>
          <option value="4" %DATEFMT4%>DD MMM YYYY (31 Dec 2025)</option>
          <option value="5" %DATEFMT5%>MMM DD, YYYY (Dec 31, 2025)</option>
        </select>
      </div>
      <div class="row">
        <div class="field">
          <label for="clk_time">Time color</label>
          <div class="hstack"><input type="color" id="clk_time" value="%CLK_TIME%"><span class="mono small text-dim">%CLK_TIME%</span></div>
        </div>
        <div class="field">
          <label for="clk_date">Date color</label>
          <div class="hstack"><input type="color" id="clk_date" value="%CLK_DATE%"><span class="mono small text-dim">%CLK_DATE%</span></div>
        </div>
      </div>
      <div class="field">
        <label for="clk_size">Time size</label>
        <select id="clk_size">
          <option value="0" %CLKSZ0%>Auto (default)</option>
          <option value="1" %CLKSZ1%>Normal</option>
          <option value="2" %CLKSZ2%>Medium</option>
          <option value="3" %CLKSZ3%>Large (falls back if screen too narrow)</option>
        </select>
      </div>
      <div class="field">
        <label for="clk_dsize">Date size</label>
        <select id="clk_dsize">
          <option value="0" %CLKDS0%>Auto (match time size)</option>
          <option value="1" %CLKDS1%>Normal</option>
          <option value="2" %CLKDS2%>Medium</option>
          <option value="3" %CLKDS3%>Large (falls back if too wide)</option>
        </select>
      </div>
      <label class="check-row">
        <input type="checkbox" id="clk_hidedate" value="1" %CLK_HIDEDATE% onchange="toggleSetting('clkhd',this.checked)">
        <label for="clk_hidedate">Hide date (time only)</label>
      </label>
    </div>
  </details>

  <details class="card card-collapsible">
    <summary>
      <div><h3>Gauge Appearance</h3><p>Pick a preset or paint individual gauges, and rename any gauge label. Bulk pickers update the form only - click Apply to save.</p></div>
    </summary>
    <div class="card-body">
      <div class="swatch-row">
        <button type="button" class="swatch" onclick="applyTheme('default')"><span class="blob" style="background:#00FF00"></span> Default</button>
        <button type="button" class="swatch" onclick="applyTheme('mono_green')"><span class="blob" style="background:#7AC74F"></span> Mono Green</button>
        <button type="button" class="swatch" onclick="applyTheme('neon')"><span class="blob" style="background:#FF36E5"></span> Neon</button>
        <button type="button" class="swatch" onclick="applyTheme('warm')"><span class="blob" style="background:#E0623A"></span> Warm</button>
        <button type="button" class="swatch" onclick="applyTheme('ocean')"><span class="blob" style="background:#2DB8C4"></span> Ocean</button>
        <button type="button" class="swatch" onclick="applyTheme('paper')"><span class="blob" style="background:#E8E8E8"></span> Paper</button>
      </div>

      <div class="bulk-color-row">
        <label for="bulk_a"><span>All arcs</span><input type="color" id="bulk_a" value="#00FF00" oninput="bulkSet('a',this.value)"></label>
        <label for="bulk_l"><span>All labels</span><input type="color" id="bulk_l" value="#00FF00" oninput="bulkSet('l',this.value)"></label>
        <label for="bulk_v"><span>All values</span><input type="color" id="bulk_v" value="#FFFFFF" oninput="bulkSet('v',this.value)"></label>
      </div>

      <div class="bulk-color-row">
        <label for="clr_bg"><span>Background</span><input type="color" id="clr_bg" value="%CLR_BG%"></label>
        <label for="clr_track"><span>Track</span><input type="color" id="clr_track" value="%CLR_TRACK%"></label>
        <label for="clr_pbar"><span>Progress Bar</span><input type="color" id="clr_pbar" value="%CLR_PBAR%"></label>
      </div>

      <label class="check-row">
        <input type="checkbox" id="slbl" value="1" %SLBL% onchange="toggleSetting('slbl',this.checked);applyLabelMaxlen()">
        <label for="slbl">Smaller gauge labels (allows longer custom labels)</label>
      </label>
      <div class="help-text" style="padding-left:28px">Smaller font for every gauge label; raises the name limit to 12 characters (8 when off). Longer names only fully show on larger displays - small screens trim what doesn't fit.</div>

      <div class="gauge-color-list">
        <div class="subhead">Per-gauge fine tuning</div>
        <div class="gauge-color-row"><span class="name">Progress</span><div class="alv"><span>Arc</span><input type="color" id="prg_a" value="%PRG_A%"></div><div class="alv"><span>Label</span><input type="color" id="prg_l" value="%PRG_L%"></div><div class="alv"><span>Value</span><input type="color" id="prg_v" value="%PRG_V%"></div><input type="text" class="lbl" id="prg_lbl" maxlength="12" value="%PRG_LBL%" placeholder="Progress"></div>
        <div class="gauge-color-row"><span class="name">Nozzle</span><div class="alv"><span>Arc</span><input type="color" id="noz_a" value="%NOZ_A%"></div><div class="alv"><span>Label</span><input type="color" id="noz_l" value="%NOZ_L%"></div><div class="alv"><span>Value</span><input type="color" id="noz_v" value="%NOZ_V%"></div><input type="text" class="lbl" id="noz_lbl" maxlength="12" value="%NOZ_LBL%" placeholder="Nozzle"></div>
        <div class="gauge-color-row"><span class="name">Bed</span><div class="alv"><span>Arc</span><input type="color" id="bed_a" value="%BED_A%"></div><div class="alv"><span>Label</span><input type="color" id="bed_l" value="%BED_L%"></div><div class="alv"><span>Value</span><input type="color" id="bed_v" value="%BED_V%"></div><input type="text" class="lbl" id="bed_lbl" maxlength="12" value="%BED_LBL%" placeholder="Bed"></div>
        <div class="gauge-color-row"><span class="name">Part Fan</span><div class="alv"><span>Arc</span><input type="color" id="pfn_a" value="%PFN_A%"></div><div class="alv"><span>Label</span><input type="color" id="pfn_l" value="%PFN_L%"></div><div class="alv"><span>Value</span><input type="color" id="pfn_v" value="%PFN_V%"></div><input type="text" class="lbl" id="pfn_lbl" maxlength="12" value="%PFN_LBL%" placeholder="Part"></div>
        <div class="gauge-color-row"><span class="name">Aux Fan</span><div class="alv"><span>Arc</span><input type="color" id="afn_a" value="%AFN_A%"></div><div class="alv"><span>Label</span><input type="color" id="afn_l" value="%AFN_L%"></div><div class="alv"><span>Value</span><input type="color" id="afn_v" value="%AFN_V%"></div><input type="text" class="lbl" id="afn_lbl" maxlength="12" value="%AFN_LBL%" placeholder="Aux"></div>
        <div class="gauge-color-row gauge-x2d"><span class="name">Aux Fan Right (X2D)</span><div class="alv"><span>Arc</span><input type="color" id="afr_a" value="%AFR_A%"></div><div class="alv"><span>Label</span><input type="color" id="afr_l" value="%AFR_L%"></div><div class="alv"><span>Value</span><input type="color" id="afr_v" value="%AFR_V%"></div><input type="text" class="lbl" id="afr_lbl" maxlength="12" value="%AFR_LBL%" placeholder="R.Aux"></div>
        <div class="gauge-color-row"><span class="name">Chamber Fan</span><div class="alv"><span>Arc</span><input type="color" id="cfn_a" value="%CFN_A%"></div><div class="alv"><span>Label</span><input type="color" id="cfn_l" value="%CFN_L%"></div><div class="alv"><span>Value</span><input type="color" id="cfn_v" value="%CFN_V%"></div><input type="text" class="lbl" id="cfn_lbl" maxlength="12" value="%CFN_LBL%" placeholder="Chamber"></div>
        <div class="gauge-color-row"><span class="name">Exhaust Fan</span><div class="alv"><span>Arc</span><input type="color" id="exh_a" value="%EXH_A%"></div><div class="alv"><span>Label</span><input type="color" id="exh_l" value="%EXH_L%"></div><div class="alv"><span>Value</span><input type="color" id="exh_v" value="%EXH_V%"></div><input type="text" class="lbl" id="exh_lbl" maxlength="12" value="%EXH_LBL%" placeholder="Exhaust"></div>
        <div class="gauge-color-row"><span class="name">Chamber Temp</span><div class="alv"><span>Arc</span><input type="color" id="cht_a" value="%CHT_A%"></div><div class="alv"><span>Label</span><input type="color" id="cht_l" value="%CHT_L%"></div><div class="alv"><span>Value</span><input type="color" id="cht_v" value="%CHT_V%"></div><input type="text" class="lbl" id="cht_lbl" maxlength="12" value="%CHT_LBL%" placeholder="Chamber"></div>
        <div class="gauge-color-row"><span class="name">Heatbreak Fan</span><div class="alv"><span>Arc</span><input type="color" id="hbk_a" value="%HBK_A%"></div><div class="alv"><span>Label</span><input type="color" id="hbk_l" value="%HBK_L%"></div><div class="alv"><span>Value</span><input type="color" id="hbk_v" value="%HBK_V%"></div><input type="text" class="lbl" id="hbk_lbl" maxlength="12" value="%HBK_LBL%" placeholder="HBreak"></div>
        <div class="gauge-color-row"><span class="name">Power</span><div class="alv"><span>Arc</span><input type="color" id="pwr_a" value="%PWR_A%"></div><div class="alv"><span>Label</span><input type="color" id="pwr_l" value="%PWR_L%"></div><div class="alv"><span>Value</span><input type="color" id="pwr_v" value="%PWR_V%"></div><input type="text" class="lbl" id="pwr_lbl" maxlength="12" value="%PWR_LBL%" placeholder="Power"></div>
        <div class="gauge-color-row"><span class="name">Layer</span><div class="alv"><span>Arc</span><input type="color" id="lyr_a" value="%LYR_A%"></div><div class="alv"><span>Label</span><input type="color" id="lyr_l" value="%LYR_L%"></div><div class="alv"><span>Value</span><input type="color" id="lyr_v" value="%LYR_V%"></div><input type="text" class="lbl" id="lyr_lbl" maxlength="12" value="%LYR_LBL%" placeholder="Layer"></div>
        <div class="gauge-label-row"><span class="name">Clock</span><input type="text" class="lbl" id="clk_lbl" maxlength="12" value="%CLK_LBL%" placeholder="Clock"></div>
        <div class="gauge-label-row"><span class="name">AMS</span><input type="text" class="lbl" id="ams_lbl" maxlength="12" value="%AMS_LBL%" placeholder="AMS" title="Shown as 'Name 1'..'Name 4'"></div>
        <div class="gauge-label-row"><span class="name">Nozzle R</span><input type="text" class="lbl" id="nzr_lbl" maxlength="12" value="%NZR_LBL%" placeholder="Nozzle R" title="Dual-nozzle right; empty = Nozzle name + R"></div>
        <div class="gauge-label-row"><span class="name">Nozzle L</span><input type="text" class="lbl" id="nzl_lbl" maxlength="12" value="%NZL_LBL%" placeholder="Nozzle L" title="Dual-nozzle left; empty = Nozzle name + L"></div>
        <div class="gauge-color-row door-color-row"><span class="name">Door</span><div class="alv"><span>Closed</span><input type="color" id="clr_dorc" value="%CLR_DORC%"></div><div class="alv"><span>Open</span><input type="color" id="clr_doro" value="%CLR_DORO%"></div><input type="text" class="lbl" id="dor_lbl" maxlength="12" value="%DOR_LBL%" placeholder="empty = icon only" title="Status-bar door label. Leave empty to show just the padlock icon."></div>
      </div>

      <!-- Screen text that belongs to no gauge. Kept as its own list so the
           label rows above keep their :last-child border rule. Warning, pause
           and error colors are deliberately absent - an alert must not be
           paintable in a reassuring color. -->
      <div class="gauge-color-list">
        <div class="subhead">Accent colors</div>
        <div class="accent-color-row"><span class="name">Finish time</span><span class="hint">ETA / Remaining line on every screen</span><input type="color" id="clr_eta" value="%CLR_ETA%"></div>
        <div class="accent-color-row"><span class="name">Print complete</span><span class="hint">Finished-screen headline and tick</span><input type="color" id="clr_fin" value="%CLR_FIN%"></div>
        <div class="accent-color-row"><span class="name">Status OK</span><span class="hint">Printing / Ready / Done badge and connected dot</span><input type="color" id="clr_stok" value="%CLR_STOK%"></div>
        <div class="accent-color-row"><span class="name">Printer name</span><span class="hint">Ready headline and every screen header</span><input type="color" id="clr_pname" value="%CLR_PNAME%"></div>
        <div class="accent-color-row"><span class="name">Text</span><span class="hint">Readout values, file name, layer count</span><input type="color" id="clr_txt" value="%CLR_TXT%"></div>
        <div class="accent-color-row"><span class="name">Muted text</span><span class="hint">Secondary lines and "no value" placeholders</span><input type="color" id="clr_txtd" value="%CLR_TXTD%"></div>
      </div>

      <div class="action-bar">
        <button type="button" class="btn btn-ghost btn-sm" onclick="randomGaugeColors()">Random</button>
        <button type="button" class="btn btn-ghost btn-sm" onclick="resetGaugeColors()">Reset to defaults</button>
        <button type="button" class="btn btn-ghost btn-sm" onclick="clearGaugeLabels()">Clear labels</button>
      </div>
    </div>
  </details>

  <div class="action-bar" style="border:none;padding:0;margin:0">
    <button type="button" class="btn btn-primary" onclick="applyDisplay()">Apply Display Settings</button>
  </div>
</div>

)rawliteral";

#if HAS_HMS_WEB_UI
static const char PAGE_HTML_ERRORS[] PROGMEM = R"rawliteral(
<!-- ===== Section 3: Printer Errors ===== -->
<div class="section" id="sec-errors" hidden>
  <div class="section-intro">
    <h2>Printer Errors</h2>
    <p>What the device does when the printer reports an HMS code or a print error. These settings are global - one buzzer, one LED and one screen serve every printer slot.</p>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Reported now</h3><p>Everything the printers are currently reporting, including codes that stand permanently and never raise an alert. Codes Bambu publishes no description for are left out - the printer's own screen and Bambu Studio do not show them either.</p></div></div>
    <div class="card-body"><div id="hmsLive"><span class="text-dim">Loading...</span></div></div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Reporting</h3><p>An active error replaces the status badge with a red ERR. Tap the button to read the details.</p></div></div>
    <div class="card-body">
      <label class="check-row">
        <input type="checkbox" id="hmsen" value="1" %HMS_EN% onchange="toggleSetting('hmsen',this.checked);toggleHmsFields()">
        <label for="hmsen">Report printer errors</label>
      </label>
      <div class="help-text" style="padding-left:28px">Off hides the badge, the error screen and every alert below. Codes keep being read, so turning this back on takes effect immediately.</div>

      <div id="hmsFields" style="display:%HMS_DISP%">
        <label class="check-row" style="margin-top:var(--sp-3)">
          <input type="checkbox" id="hmssev" value="1" %HMS_SEV% onchange="toggleSetting('hmssev',this.checked)">
          <label for="hmssev">Include low-priority codes</label>
        </label>
        <div class="help-text" style="padding-left:28px">Off shows only fatal and serious codes. On adds the common ones - advisories the printer expects you to notice but not to act on immediately. Codes already active when the device connects never raise an alert either way - they are the printer's normal state, not news.</div>

        <div class="field" style="margin-top:var(--sp-4)">
          <label for="hmsauto">Show the error screen automatically</label>
          <select id="hmsauto" onchange="toggleSetting('hmsauto',this.value)">
            <option value="0" %HMSA0%>Never - badge only</option>
            <option value="1" %HMSA1%>Briefly, then go back</option>
            <option value="2" %HMSA2%>Until dismissed (button or touch)</option>
          </select>
        </div>

        <!-- The check-rows are siblings of the caption, not children of the
             .field: ".field > label" forces display:block and would break the
             check-row flex layout, dropping each caption under its box. -->
        <div class="field" style="margin:var(--sp-4) 0 0"><label>Alert on a new error</label></div>
        <label class="check-row">
          <input type="checkbox" id="hmsm0" value="1" %HMSM0% onchange="applyHmsMask()">
          <label for="hmsm0">Edge glow</label>
        </label>
        <label class="check-row">
          <input type="checkbox" id="hmsm1" value="1" %HMSM1% onchange="applyHmsMask()">
          <label for="hmsm1">Buzzer</label>
        </label>
        <label class="check-row">
          <input type="checkbox" id="hmsm2" value="1" %HMSM2% onchange="applyHmsMask()">
          <label for="hmsm2">Status LED</label>
        </label>
        <label class="check-row">
          <input type="checkbox" id="hmsm3" value="1" %HMSM3% onchange="applyHmsMask()">
          <label for="hmsm3">Wake a sleeping screen</label>
        </label>
        <div class="hint" style="padding-left:28px">Without "wake", the error screen never comes up on its own while the display is asleep - you see the badge when you wake it yourself.</div>

        <!-- Hidden on boards that carry the sentence table: there is nothing to
             look up, so the switch would do nothing. -->
        <div id="hmsOnlRow" style="display:%HMS_ONL_DISP%">
          <div class="field" style="margin:var(--sp-4) 0 0"><label>Error text</label></div>
          <label class="check-row">
            <input type="checkbox" id="hmsonl" value="1" %HMS_ONL% onchange="toggleSetting('hmsonl',this.checked);hmsLookupChanged()">
            <label for="hmsonl">Look up error text on this page</label>
          </label>
          <div class="help-text" style="padding-left:28px">This board stores the text for print errors, but not for the thousands of HMS codes. With this on, <strong>this page</strong> - not the device - fetches that missing text once per visit from keralots.github.io. The device never contacts it. Off keeps the code, severity, module and wiki link on every row, and print-error rows keep their text either way.</div>
        </div>
      </div>
    </div>
  </div>
</div>

)rawliteral";
#endif  // HAS_HMS_WEB_UI

static const char PAGE_HTML_2[] PROGMEM = R"rawliteral(
<!-- ===== Section 4: Hardware ===== -->
<div class="section" id="sec-hardware" hidden>
  <div class="section-intro">
    <h2>Hardware</h2>
    <p>Board-specific configuration. Available options depend on which display device you flashed.</p>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Printer rotation</h3><p>How the device cycles between configured printer slots.</p></div></div>
    <div class="field">
      <label for="rotmode">Rotation mode</label>
      <select id="rotmode">
        <option value="0" %RMODE_OFF%>Off (show selected printer only)</option>
        <option value="1" %RMODE_AUTO%>Auto-rotate (cycle all connected)</option>
        <option value="2" %RMODE_SMART%>Smart (prioritize printing / drying)</option>
      </select>
      <div class="hint">Smart mode shows the printing or drying printer. Rotates only when both are active.</div>
    </div>
    <div class="field">
      <label for="rotinterval">Rotation interval</label>
      <div class="hstack" style="gap:var(--sp-2)"><input type="number" id="rotinterval" min="10" max="600" value="%ROT_INTERVAL%" style="max-width:120px"><span class="text-dim small">seconds</span></div>
    </div>
    <label class="check-row">
      <input type="checkbox" id="rotsplit" value="1" %ROT_SPLIT_CHK% onchange="toggleSetting('rotsplit',this.checked)">
      <label for="rotsplit">Split screen when two printers are printing</label>
    </label>
    <div class="help-text" style="padding-left:28px">Shows both active printers at once (top/bottom), overriding rotation while two are printing or drying.</div>
    <label class="check-row">
      <input type="checkbox" id="rotsplitf" value="1" %ROT_SPLITF_CHK% onchange="toggleSetting('rotsplitf',this.checked)">
      <label for="rotsplitf">Always show split screen (testing)</label>
    </label>
    <div class="help-text" style="padding-left:28px">Forces the split view of the first two configured printers regardless of activity, so you can test the layout without two live prints.</div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>External button</h3><p>Optional physical button. Switches between printers and wakes the display from sleep.</p></div></div>
    <div class="field">
      <label for="btntype">Button type</label>
      <select id="btntype" onchange="toggleBtnPin()">
        <option value="0" %BTN_OFF%>Disabled</option>
        <option value="1" %BTN_PUSH%>Push Button (active LOW)</option>
        <option value="2" %BTN_TOUCH%>TTP223 Touch (active HIGH)</option>
        <option value="3" %BTN_SCREEN%>Touchscreen</option>
      </select>
    </div>
    <div class="field" id="btnPinRow">
      <label for="btnpin">Button GPIO pin</label>
      <input type="number" id="btnpin" class="mono" min="1" max="48" value="%BTN_PIN%" style="max-width:120px">
    </div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Buzzer</h3><p>Passive buzzer. Beeps on print complete and errors.</p></div></div>
    <div class="field">
      <label for="buzzen">Buzzer</label>
      <select id="buzzen" onchange="toggleBuzPin()">
        <option value="0" %BUZ_OFF%>Disabled</option>
        <option value="1" %BUZ_ON%>Enabled</option>
      </select>
    </div>
    <div id="buzFields" style="display:none">
      <div class="field" id="buzPinRow">
        <label for="buzpin">Buzzer GPIO pin</label>
        <input type="number" id="buzpin" class="mono" min="1" max="48" value="%BUZ_PIN%" style="max-width:120px">
      </div>
      <div id="buzEs8311Info" class="help-text" style="display:none">Built-in I2S speaker. No GPIO configuration needed.</div>
      <div class="field">
        <label>Quiet hours (optional)</label>
        <div class="hstack" style="gap:var(--sp-2)">
          <input type="number" id="buzqs" class="mono" min="0" max="23" value="%BUZ_QS%" style="max-width:70px" placeholder="22">
          <span class="text-dim small">to</span>
          <input type="number" id="buzqe" class="mono" min="0" max="23" value="%BUZ_QE%" style="max-width:70px" placeholder="7">
          <span class="text-dim small">(0-0 = off)</span>
        </div>
      </div>
      <label class="check-row" id="buzClickRow" style="display:none">
        <input type="checkbox" id="buzclick" %BUZ_CLICK%>
        <label for="buzclick">Click sound when button pressed</label>
      </label>
      <label class="check-row">
        <input type="checkbox" id="buzbeden" %BUZ_BED_ALERT%>
        <label for="buzbeden">Sound alert when bed cools after print</label>
      </label>
      <div class="field" id="buzBedTempRow">
        <label for="buzbedtemp">Bed-cool threshold</label>
        <div class="hstack" style="gap:var(--sp-2)"><input type="number" id="buzbedtemp" class="mono" min="20" max="80" value="%BUZ_BED_TEMP%" style="max-width:90px"><span class="text-dim small">&deg;C</span></div>
      </div>
      <button type="button" id="buzTestBtn" class="btn btn-ghost btn-sm" onclick="testBuzzer()">Test finished sound</button>
    </div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Status LED</h3><p>A single PWM-dimmed LED via NPN transistor or MOSFET, a three-pin RGB LED, or one WS2812 pixel. CYD: GPIO 22 on P3 connector.</p></div></div>
    <div class="field">
      <label for="leden">Status LED</label>
      <select id="leden" onchange="toggleLed();ledPreviewSend()">
        <option value="0" %LED_OFF%>Disabled</option>
        <option value="1" %LED_ON%>Enabled</option>
      </select>
    </div>
    <div id="ledFields" style="display:none">
      <div class="field">
        <label for="leddrv">Wiring</label>
        <select id="leddrv" onchange="toggleLed();ledPreviewSend()">
          <option value="0" %LED_DRV_S%>Single colour (one pin)</option>
          <option value="1" %LED_DRV_R%>RGB LED (three pins)</option>
          %LED_DRV_PIXEL_OPT%
        </select>
      </div>
      <div class="row">
        <div class="field">
          <label for="ledpin"><span id="ledpinLbl">LED GPIO pin</span></label>
          <input type="number" id="ledpin" class="mono" min="1" max="48" value="%LED_PIN%" onchange="ledPreviewSend()" style="max-width:120px">
        </div>
        <div class="field">
          <label class="hstack" style="justify-content:space-between" for="ledbr"><span>Brightness</span><span class="mono text-dim" id="ledbrVal">%LED_BR%</span></label>
          <input type="range" id="ledbr" min="0" max="255" step="5" value="%LED_BR%"
                 oninput="document.getElementById('ledbrVal').textContent=this.value;ledPreviewSend()">
        </div>
      </div>
      <div class="row" id="ledRgbPins" style="display:none">
        <div class="field">
          <label for="ledping">Green GPIO pin</label>
          <input type="number" id="ledping" class="mono" min="1" max="48" value="%LED_PIN_G%" onchange="ledPreviewSend()" style="max-width:120px">
        </div>
        <div class="field">
          <label for="ledpinb">Blue GPIO pin</label>
          <input type="number" id="ledpinb" class="mono" min="1" max="48" value="%LED_PIN_B%" onchange="ledPreviewSend()" style="max-width:120px">
        </div>
      </div>
      <label class="check-row" id="ledAnodeRow" style="display:none">
        <input type="checkbox" id="ledanode" %LED_ANODE% onchange="ledPreviewSend()">
        <label for="ledanode">Common anode (shared leg on 3V3 - tick if the colours come out inverted)</label>
      </label>
%LED_ONBOARD_ROW%
      <div id="ledColors" style="display:none">
        <details open>
          <summary>Colour per printer state</summary>
          <div class="row">
            <div class="field"><label for="ledcidl">Idle</label><input type="color" id="ledcidl" value="%LED_C_IDLE%" oninput="ledPreviewSend(this.value)"></div>
            <div class="field"><label for="ledcprn">Printing</label><input type="color" id="ledcprn" value="%LED_C_PRINT%" oninput="ledPreviewSend(this.value)"></div>
          </div>
          <div class="row">
            <div class="field"><label for="ledcpau">Paused</label><input type="color" id="ledcpau" value="%LED_C_PAUSE%" oninput="ledPreviewSend(this.value)"></div>
            <div class="field"><label for="ledcfin">Finished</label><input type="color" id="ledcfin" value="%LED_C_FIN%" oninput="ledPreviewSend(this.value)"></div>
          </div>
          <div class="field"><label for="ledcerr">Error</label><input type="color" id="ledcerr" value="%LED_C_ERR%" oninput="ledPreviewSend(this.value)"></div>
        </details>
      </div>
      <details open>
        <summary>Print-finished effect</summary>
        <div>
          <div class="row">
            <div class="field">
              <label for="ledfxmd">Effect</label>
              <select id="ledfxmd" onchange="toggleLedFx()">
                <option value="0" %LED_FX_OFF%>Off</option>
                <option value="1" %LED_FX_BREATH%>Breathing pulse</option>
                <option value="2" %LED_FX_HB%>Heartbeat</option>
              </select>
            </div>
            <div class="field" id="ledFxParams">
              <label for="ledfxsec">Duration (5-600 seconds)</label>
              <div class="hstack" style="gap:var(--sp-2)"><input type="number" id="ledfxsec" min="5" max="600" value="%LED_FX_SEC%" style="max-width:120px"><span class="text-dim small">seconds</span></div>
            </div>
          </div>
          <div class="field">
            <label class="hstack" style="justify-content:space-between" for="ledfxbr"><span>Peak brightness</span><span class="mono text-dim" id="ledfxbrVal">%LED_FX_BR%</span></label>
            <input type="range" id="ledfxbr" min="0" max="255" step="5" value="%LED_FX_BR%"
                   oninput="document.getElementById('ledfxbrVal').textContent=this.value">
          </div>
          <button type="button" class="btn btn-ghost btn-sm" onclick="ledTestEffect()">Test effect</button>
        </div>
      </details>
      <label class="check-row" style="margin-top:var(--sp-3)">
        <input type="checkbox" id="ledauto" %LED_AUTO%>
        <label for="ledauto">LED on only while printing</label>
      </label>
      <label class="check-row">
        <input type="checkbox" id="ledpause" %LED_PAUSE%>
        <label for="ledpause">Slow pulse during pause</label>
      </label>
      <label class="check-row">
        <input type="checkbox" id="lederr" %LED_ERR% onchange="toggleLedErr()">
        <label for="lederr">Fast strobe on error</label>
      </label>
      <div class="field" id="ledErrParams" style="margin-top:var(--sp-2)">
        <label for="lederrsec">Strobe auto-off (0 = never, else 5-600 seconds)</label>
        <div class="hstack" style="gap:var(--sp-2)"><input type="number" id="lederrsec" min="0" max="600" value="%LED_ERR_SEC%" style="max-width:120px"><span class="text-dim small">seconds</span></div>
      </div>
    </div>
  </div>

%BAT_TOGGLE_ROW%

  <div class="card">
    <div class="card-head">
      <div><h3>Detected hardware</h3></div>
      <span class="mono small text-dim">%BOARD%</span>
    </div>
    <dl class="kv" id="hwInfo">
      <dt>Board</dt><dd>%BOARD_NAME%</dd>
      <dt>Display</dt><dd>%BOARD_PANEL%</dd>
      <dt>Free heap</dt><dd id="hwHeap">-</dd>
      <dt>Flash</dt><dd id="hwFlash">-</dd>
      <dt>PSRAM</dt><dd id="hwPsram">-</dd>
      <dt>MAC</dt><dd id="hwMac">-</dd>
      <dt>Firmware</dt><dd class="mono">%FW_VER%</dd>
    </dl>
  </div>

  <div class="action-bar" style="border:none;padding:0;margin:0">
    <button type="button" class="btn btn-primary" onclick="saveRotation()">Save Hardware Settings</button>
  </div>
</div>

<!-- ===== Section 5: Advanced ===== -->
<div class="section" id="sec-advanced" hidden>
  <div class="section-intro">
    <h2>Advanced</h2>
    <p>Extended display layouts and operations that need extra care. Most users do not need anything here.</p>
  </div>

%EXTENDED_MODES_CARD%

  <div class="card">
    <div class="card-head"><div><h3>Clock screen info</h3><p>Show each configured printer's name and LAN IP in a small footer on the idle/clock screen. Handy when you run several units side by side.</p></div></div>
    <label class="check-row">
      <input type="checkbox" id="clkinfo" value="1" %CLK_INFO% onchange="toggleSetting('clkinfo',this.checked)">
      <label for="clkinfo">Show printer name &amp; IP on clock screen</label>
    </label>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Gauge scales</h3><p>Full-scale value each arc represents. Lower a scale so the arc sweeps fuller for your printer's normal range. Defaults suit any Bambu printer.</p></div></div>
    <div class="field"><label for="noz_max">Nozzle full-scale</label><div class="hstack" style="gap:var(--sp-2)"><input type="number" id="noz_max" min="100" max="400" value="%NOZ_MAX%" style="max-width:120px"><span class="text-dim small">&deg;C &middot; default 300</span></div></div>
    <div class="field"><label for="bed_max">Bed full-scale</label><div class="hstack" style="gap:var(--sp-2)"><input type="number" id="bed_max" min="40" max="150" value="%BED_MAX%" style="max-width:120px"><span class="text-dim small">&deg;C &middot; default 120</span></div></div>
    <div class="field"><label for="cht_max">Chamber / AMS temp full-scale</label><div class="hstack" style="gap:var(--sp-2)"><input type="number" id="cht_max" min="30" max="120" value="%CHT_MAX%" style="max-width:120px"><span class="text-dim small">&deg;C &middot; default 60</span></div></div>
    <div class="field"><label for="pwr_max">Power gauge full-scale</label><div class="hstack" style="gap:var(--sp-2)"><input type="number" id="pwr_max" min="100" max="5000" step="50" value="%PWR_MAX%" style="max-width:120px"><span class="text-dim small">W &middot; default 1000</span></div></div>
    <button type="button" class="btn btn-primary" onclick="applyDisplay()">Apply Display Settings</button>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Gauge behavior</h3><p>How temperature arcs animate, and an optional warning color when a gauge runs hot.</p></div></div>
    <div class="field"><label for="gsmooth">Arc smoothing</label>
      <select id="gsmooth">
        <option value="0" %GSMOOTH_OFF%>Off (snap instantly)</option>
        <option value="1" %GSMOOTH_SLOW%>Slow (~2s)</option>
        <option value="2" %GSMOOTH_NORM%>Normal (~1s)</option>
        <option value="3" %GSMOOTH_FAST%>Fast (~0.4s)</option>
      </select>
      <span class="text-dim small">default Normal</span>
    </div>
    <div class="field"><label for="warn_thr">Warning threshold</label>
      <div class="hstack" style="gap:var(--sp-2)"><input type="number" id="warn_thr" min="0" max="100" step="5" value="%WARN_THR%" style="max-width:120px"><span class="text-dim small">% of scale &middot; default 0 (off)</span></div>
    </div>
    <div class="field"><label for="warn_clr">Warning color</label>
      <div class="hstack"><input type="color" id="warn_clr" value="%WARN_CLR%"><span class="mono small text-dim">%WARN_CLR%</span><span class="text-dim small">&middot; default red</span></div>
    </div>
    <p class="hint">Nozzle, bed and chamber arcs (plus their value text) switch to the warning color once the reading reaches this share of the gauge's full scale.</p>
    <button type="button" class="btn btn-primary" onclick="applyDisplay()">Apply Display Settings</button>
  </div>

  <div class="card" style="border-color:rgba(220, 69, 56, 0.30)">
    <div class="card-head"><div><h3 style="color:var(--danger)">Danger zone</h3><p>Destructive operations and experimental settings. Unlock to reveal.</p></div></div>
    <label class="check-row">
      <input type="checkbox" id="dangerUnlock" onchange="toggleDangerUnlock(this.checked)">
      <label for="dangerUnlock">Show advanced operations</label>
    </label>
    <div id="dangerOps" style="display:none;margin-top:var(--sp-3)">
%DUALP_ADVANCED%
      <div class="hstack" style="gap:var(--sp-2);flex-wrap:wrap;margin-top:var(--sp-3)">
        <button type="button" class="btn btn-ghost" onclick="rebootDevice()">Reboot</button>
        <button type="button" class="btn btn-danger" onclick="factoryReset()">Factory Reset...</button>
      </div>
      <p class="hint" style="margin-top:var(--sp-2)">Reboot does not change settings. Factory reset wipes WiFi, printers, gauge layout, everything.</p>
    </div>
  </div>
</div>

<!-- ===== Section 6: WiFi & System ===== -->
<div class="section" id="sec-wifi" hidden>
  <div class="section-intro">
    <h2>WiFi &amp; System</h2>
    <p>Network credentials, settings backup, firmware updates, factory reset.</p>
  </div>

  <div class="card">
    <div class="card-head">
      <div><h3>WiFi</h3></div>
      <span class="status-dot" id="wifiTopStat"><span id="wifiTopText">-</span></span>
    </div>
    <div class="field">
      <label for="ssid">SSID</label>
      <input type="text" id="ssid" value="%SSID%" placeholder="Your WiFi name">
    </div>
    <div class="field">
      <label for="pass">Password</label>
      <input type="password" id="pass" placeholder="Leave blank to keep current">
    </div>
    <label class="check-row">
      <input type="checkbox" id="showpass2" onchange="document.getElementById('pass').type=this.checked?'text':'password'">
      <label for="showpass2">Show password</label>
    </label>
    <div class="field" style="margin-top:var(--sp-3)">
      <label for="netmode">IP assignment</label>
      <select id="netmode" onchange="toggleStatic()">
        <option value="dhcp" %NET_DHCP%>DHCP (automatic)</option>
        <option value="static" %NET_STATIC%>Static IP</option>
      </select>
    </div>
    <div id="staticFields" style="display:none;padding:var(--sp-3);background:var(--bg-sub);border:1px solid var(--line-soft);border-radius:var(--radius-s);margin-top:var(--sp-3)">
      <div class="row">
        <div class="field"><label for="net_ip">IP address</label><input type="text" id="net_ip" class="mono" value="%NET_IP%" placeholder="192.168.1.100"></div>
        <div class="field"><label for="net_gw">Gateway</label><input type="text" id="net_gw" class="mono" value="%NET_GW%" placeholder="192.168.1.1"></div>
      </div>
      <div class="row">
        <div class="field"><label for="net_sn">Subnet mask</label><input type="text" id="net_sn" class="mono" value="%NET_SN%" placeholder="255.255.255.0"></div>
        <div class="field"><label for="net_dns">DNS server</label><input type="text" id="net_dns" class="mono" value="%NET_DNS%" placeholder="8.8.8.8"></div>
      </div>
    </div>
    <label class="check-row">
      <input type="hidden" name="has_showip" value="1">
      <input type="checkbox" id="showip" value="1" %SHOWIP% onchange="toggleSetting('showip',this.checked)">
      <label for="showip">Show IP on startup (1.5 s)</label>
    </label>
    <label class="check-row">
      <input type="checkbox" id="mdns_en" value="1" %MDNS_EN%>
      <label for="mdns_en">Enable .local name (mDNS)</label>
    </label>
    <div class="field">
      <label for="mdns_host">Hostname</label>
      <input type="text" id="mdns_host" class="mono" value="%MDNS_HOST%" placeholder="bambuhelper" maxlength="31">
      <div class="hint">Reach the device at <span class="mono">name.local</span> in a browser.</div>
    </div>
    <dl class="kv" id="wifiInfo" style="margin-top:var(--sp-3)">
      <dt>Signal</dt><dd id="wifiRssi">-</dd>
      <dt>Uptime</dt><dd id="wifiUptime">-</dd>
    </dl>
    <div class="action-bar">
      <button type="button" class="btn btn-primary" onclick="saveWifi()">Save WiFi &amp; Restart</button>
    </div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Settings backup</h3><p>Export or import all settings as JSON. Useful before reflashing.</p></div></div>
    <div class="banner banner-warn"><span class="dot"></span><div><strong>Cloud token is not included.</strong><div class="small text-dim" style="margin-top:2px">You will need to paste a fresh token after importing.</div></div></div>
    <button type="button" class="btn btn-ghost" onclick="exportSettings()">Export backup</button>
    <div class="field" style="margin-top:var(--sp-3)">
      <label for="importFile">Import backup</label>
      <input type="file" id="importFile" accept=".json">
    </div>
    <div class="action-bar">
      <button type="button" class="btn btn-primary" onclick="importSettings()">Import &amp; Restart</button>
    </div>
    <div id="importStatus" role="status" aria-live="polite" style="margin-top:var(--sp-2);font-size:13px"></div>
  </div>

  <div class="card">
    <div class="card-head">
      <div><h3>Firmware update</h3></div>
      <div class="hstack" style="gap:var(--sp-3);align-items:center">
        <a href="https://github.com/Keralots/BambuHelper/releases" target="_blank" class="small" style="color:var(--accent)">Release notes</a>
        <span class="mono small text-dim">current: %FW_VER%</span>
      </div>
    </div>
)rawliteral"
#ifdef ENABLE_OTA_AUTO
R"rawliteral(
    <div class="seg" style="margin-bottom:var(--sp-4)">
      <button type="button" id="tab-auto-btn" aria-pressed="true" onclick="switchFwTab('auto')">Online update</button>
      <button type="button" id="tab-manual-btn" aria-pressed="false" onclick="switchFwTab('manual')">Manual update</button>
    </div>
    <div id="fw-tab-auto">
      <p class="small text-dim" style="margin-bottom:var(--sp-3)">Check for and install BambuHelper display device firmware updates directly from GitHub.</p>
      <div class="hstack" style="margin-bottom:var(--sp-3);gap:var(--sp-3);flex-wrap:wrap">
        <button type="button" class="btn btn-ghost" onclick="checkForUpdates()">Check for Updates</button>
        <span id="updateResult" class="small text-dim" role="status" aria-live="polite"></span>
      </div>
      <div id="updateInfo" style="display:none;padding:var(--sp-3);background:var(--bg-sub);border:1px solid var(--line-soft);border-radius:var(--radius-s)">
        <div class="hstack" style="justify-content:space-between;flex-wrap:wrap;gap:var(--sp-2)">
          <div>
            <strong id="updateVer" style="color:var(--success);font-size:14px"></strong>
            <span id="updateDate" class="text-dim small" style="margin-left:8px"></span>
          </div>
          <div class="hstack" style="gap:var(--sp-2);flex-wrap:wrap">
            <button id="installBtn" type="button" class="btn btn-primary btn-sm" onclick="installUpdate()">Install update</button>
            <a id="updateLink" href="#" target="_blank" class="btn btn-ghost btn-sm">Manual download</a>
          </div>
        </div>
        <div id="autoOtaWrap" style="display:none;margin-top:var(--sp-3)">
          <div style="background:var(--line);border-radius:4px;height:16px;overflow:hidden">
            <div id="autoOtaBar" style="background:var(--accent);height:100%;width:0%;transition:width 0.4s"></div>
          </div>
          <div id="autoOtaStatus" role="status" aria-live="polite" class="small text-dim" style="text-align:center;margin-top:4px">Starting...</div>
          <div class="hint" style="text-align:center;color:var(--warn)">&#9888; Do not power off or close this page</div>
        </div>
      </div>
    </div>
    <div id="fw-tab-manual" style="display:none">
      <p class="small text-dim" style="margin-bottom:var(--sp-3)">Upload a .bin file to update BambuHelper display device firmware. Settings are preserved. Device restarts automatically.</p>
)rawliteral"
#else
R"rawliteral(
    <p class="small text-dim" style="margin-bottom:var(--sp-3)">Upload a .bin file to update BambuHelper display device firmware. Settings are preserved. Device restarts automatically.</p>
)rawliteral"
#endif
R"rawliteral(
      <div class="field">
        <input type="file" id="otaFile" accept=".bin">
        <div class="hint" style="color:var(--warn)">&#9888; Do not power off or close this page during the upload.</div>
      </div>
      <div id="otaProgress" style="display:none;margin-top:var(--sp-3)">
        <div style="background:var(--line);border-radius:4px;height:20px;overflow:hidden">
          <div id="otaBar" style="background:var(--accent);height:100%;width:0%;transition:width 0.3s"></div>
        </div>
        <div id="otaPct" class="text-mid small" style="text-align:center;margin-top:4px">0%</div>
      </div>
      <div id="otaStatus" role="status" aria-live="polite" class="small" style="margin-top:var(--sp-2)"></div>
      <div class="action-bar">
        <button type="button" class="btn btn-primary" onclick="startOta()">Upload firmware</button>
      </div>
)rawliteral"
#ifdef ENABLE_OTA_AUTO
R"rawliteral(
    </div>
)rawliteral"
#endif
R"rawliteral(
    <div id="rollbackWrap" style="display:none;margin-top:var(--sp-4);padding-top:var(--sp-3);border-top:1px solid var(--line-soft)">
      <div class="hstack" style="justify-content:space-between;flex-wrap:wrap;gap:var(--sp-2);align-items:center">
        <div>
          <strong style="font-size:13px">Previous firmware</strong>
          <div id="rollbackInfo" class="small text-dim"></div>
        </div>
        <button type="button" class="btn btn-ghost btn-sm" onclick="otaRollback()">Boot other slot</button>
      </div>
      <div id="rollbackStatus" role="status" aria-live="polite" class="small" style="margin-top:var(--sp-2)"></div>
    </div>
  </div>

  <p class="hint" style="margin-top:var(--sp-3)">Reboot and factory reset have moved to the <strong>Advanced</strong> section.</p>
</div>

<!-- ===== Section 7: Power Monitoring ===== -->
<div class="section" id="sec-power" hidden>
  <div class="section-intro">
    <h2>Power Monitoring</h2>
    <p>Show live power consumption from Tasmota, Shelly, or TP-Link Kasa smart plugs. Configure auto power-off and energy tariff per plug.</p>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Tariff &amp; currency</h3><p>Shared across both plugs. Drives the "cost so far" stat.</p></div></div>
    <div class="row">
      <div class="field"><label for="tsm_cur">Currency symbol</label><input type="text" id="tsm_cur" placeholder="&euro;" maxlength="6" style="max-width:120px"></div>
      <div class="field"><label for="tsm_tar">Tariff per kWh</label>
        <div class="hstack" style="gap:var(--sp-2)"><input type="number" id="tsm_tar" min="0" max="10" step="0.001" value="0" style="max-width:140px"><span class="text-dim small">currency / kWh</span></div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="card-head card-head-tabs">
      <h3>Plug slot</h3>
      <div class="slot-tabs" id="powerTabBar">
        <button type="button" class="power-tab-btn active" id="ptab0" onclick="selectPowerTab(0)">Plug 1</button>
        %POWER_TAB_2%
      </div>
    </div>
    <p class="card-desc">Each plug pairs with one printer slot. Settings on screen reflect the selected plug.</p>
    <label class="check-row">
      <input type="checkbox" id="tsm_en" value="1" onchange="applyPowerEnableState()">
      <label for="tsm_en">Enable power monitoring for this plug</label>
    </label>
    <div id="plugDeps1">
    <div class="row" id="tsm_pt_row" style="margin-top:var(--sp-3)">
      <div class="field">
        <label for="tsm_pt">Power plug type</label>
        <select id="tsm_pt" onchange="onPlugTypeChange()"><option value="0">Tasmota</option><option value="1">Shelly (Gen2/Gen3)</option><option value="2">TP-Link Kasa (KP115/legacy)</option><option value="3">Shelly Power Strip Gen4</option></select>
      </div>
      <div class="field" id="tsm_outlet_field" style="display:none">
        <label for="tsm_po">Power strip outlet</label>
        <select id="tsm_po"><option value="0">Output 0</option><option value="1">Output 1</option><option value="2">Output 2</option><option value="3">Output 3</option></select>
      </div>
    </div>
    <div class="help-text" id="tsm_shelly_hint" style="display:none">Shelly Gen2/Gen3 (same RPC API), and the plug must not be password-protected (digest auth is not supported). Shelly reports live watts and a cumulative Total, but does <strong>not</strong> report Today's / Yesterday's energy, so those stay blank.</div>
    <div class="help-text" id="tsm_kasa_hint" style="display:none">TP-Link Kasa plugs using the legacy local protocol on TCP port 9999, including KP115 and HS110. No TP-Link credentials or cloud connection are used. Newer KLAP/Matter models are not supported. Kasa reports live watts, relay state, and cumulative Total, but not Today's energy.</div>
    <div class="help-text" id="tsm_shellystrip_hint" style="display:none">Shelly Power Strip Gen4 - same RPC API as Gen2/Gen3, but exposes multiple outlets at one IP. Pick which outlet above tracks/controls this printer slot. Not password-protected (digest auth is not supported). Reports live watts and a cumulative Total, but not Today's / Yesterday's energy.</div>
    <div class="row" style="margin-top:var(--sp-3)">
      <div class="field"><label for="tsm_ip">Plug IP address</label><input type="text" id="tsm_ip" class="mono" placeholder="192.168.1.x" maxlength="15"></div>
      <div class="field"><label for="tsm_pi">Poll interval</label><select id="tsm_pi">%TSM_PI_OPTIONS%</select></div>
    </div>
    %POWER_SLOT_BLOCK%
    <div class="field">
      <label>Display mode</label>
      <div class="vstack" style="gap:8px;margin-top:4px">
        <label class="hstack" style="gap:8px;cursor:pointer"><input type="radio" name="tsm_dm" value="0"><span>Alternate: layer count / watts (every 4 s)</span></label>
        <label class="hstack" style="gap:8px;cursor:pointer"><input type="radio" name="tsm_dm" value="1"><span>Always show watts</span></label>
        <label class="hstack" style="gap:8px;cursor:pointer"><input type="radio" name="tsm_dm" value="2"><span>Always show layer count</span></label>
      </div>
      <div class="help-text" style="margin-top:6px">Keeps the layer count in the status bar and never swaps to watts - use this when power already has its own gauge.</div>
      <div id="dmHiddenNote" class="help-text" style="margin-top:6px;display:none">Disabled because <b>Hide layer/power line in status bar</b> is on (Display tab). The whole status-bar readout is hidden, so this has no effect.</div>
    </div>
    </div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Auto power-off</h3></div></div>
    <div id="plugDeps2">
    <label class="check-row">
      <input type="checkbox" id="tsm_ao" value="1">
      <label for="tsm_ao">Auto power-off after print</label>
    </label>
    <div class="help-text" style="padding-left:28px">Powers off the plug after the print finishes <strong>and</strong> the nozzle drops below 50 &deg;C. New prints reset the timer.</div>
    <div class="field" style="margin-top:var(--sp-3)">
      <label for="tsm_ad">Auto-off delay</label>
      <div class="hstack" style="gap:var(--sp-2)"><input type="number" id="tsm_ad" min="1" max="240" value="10" style="max-width:100px"><span class="text-dim small">minutes</span></div>
    </div>
    <label class="check-row">
      <input type="checkbox" id="tsm_aod" value="1">
      <label for="tsm_aod">Cancel auto-off if door is opened</label>
    </label>
    <div class="help-text" style="padding-left:28px">If the printer door opens during the delay, the auto-off is cancelled for this print (you are at the printer). A new print re-arms it. Ignored on printers without a door sensor (P1/A1).</div>
    </div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Button power control</h3></div></div>
    <label class="check-row">
      <input type="checkbox" id="btnpwr" value="1" %BTN_PWR% onchange="toggleSetting('btnpwr',this.checked)">
      <label for="btnpwr">Double-click device button to turn the plug on/off</label>
    </label>
    <div class="help-text" style="padding-left:28px">Double- or triple-click the device button (or touchscreen) to open a full-screen confirmation for the printer on screen, then hold to toggle its plug (red warning if it is printing). Only active when a plug is configured for the shown printer; while armed it adds a short delay to single-tap printer switching.</div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Live stats</h3></div><span class="mono small text-dim">updates 5 s</span></div>
    <dl class="kv">
      <dt>Status</dt><dd><span id="ptStatusDot" class="text-dim">(offline)</span></dd>
      <dt>This print</dt><dd id="ptThis">-</dd>
      <dt id="ptTodayLabel">Today</dt><dd id="ptToday">-</dd>
      <dt>Total</dt><dd id="ptTotal">-</dd>
      <dt>Now</dt><dd id="ptWatts">-</dd>
    </dl>
    <div class="action-bar">
      <button type="button" class="btn btn-success-solid" id="btnPowerOn" onclick="powerControl(1)" style="display:none;min-width:140px">Power On</button>
      <button type="button" class="btn btn-danger-solid" id="btnPowerOff" onclick="powerControl(0)" style="display:none;min-width:140px">Power Off</button>
    </div>
  </div>

  <div class="action-bar" style="border:none;padding:0;margin:0">
    <button type="button" class="btn btn-primary" onclick="savePower()">Save Power Settings</button>
  </div>
  <div id="powerStatus" role="status" aria-live="polite" style="margin-top:var(--sp-2);font-size:13px"></div>
</div>

<!-- ===== Section 8: Diagnostics ===== -->
<div class="section" id="sec-diag" hidden>
  <div class="section-intro">
    <h2>Diagnostics</h2>
    <p>Live device state and verbose serial logging. Useful when something is wrong before you file an issue.</p>
  </div>

  <div class="card">
    <div class="card-head">
      <div><h3>Live state</h3></div>
      <span class="mono small text-dim">updates 5 s</span>
    </div>
    <div id="diagInfo">Loading...</div>
  </div>

  <div class="card">
    <div class="card-head"><div><h3>Verbose serial logging (USB)</h3><p>Streams MQTT and printer events over USB serial. Disable when not actively debugging - it impacts performance.</p></div></div>
    <label class="check-row">
      <input type="checkbox" id="dbglog" onchange="toggleDebug(this.checked)" %DBGLOG%>
      <label for="dbglog">Enable verbose serial logging</label>
    </label>
    <div class="help-text" style="padding-left:28px">Use USB serial monitor (115200 baud) for live logs.</div>
  </div>
</div>

</main>
</div>

<div class="scrim" id="scrim"></div>
<div class="toast" id="toast" role="status" aria-live="polite">Saved!</div>

<script>var DEV={board:'%BOARD%',fw:'%FW_VER%',flashMb:'%FLASHMB%',otaSlot:'%OTASLOT%',round:'%ISROUND%',es8311:'%ES8311_AUDIO%',hmsFull:'%HMSFULL%'};</script>
<script src="/app.js?v=%JSVER%"></script>
</body>
</html>
)rawliteral";
