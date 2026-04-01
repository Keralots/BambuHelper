#include "web_server.h"
#include "settings.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "wifi_manager.h"
#include "display_ui.h"
#include "config.h"
#include "button.h"
#include "buzzer.h"
#include "timezones.h"
#include "tasmota.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#ifdef ENABLE_OTA_AUTO
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

static WebServer server(80);

// ---------------------------------------------------------------------------
//  AP-mode page (minimal WiFi setup only)
// ---------------------------------------------------------------------------
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
  <h2>Connect to WiFi</h2>
  <p style="font-size:12px;color:#8B949E;margin-bottom:10px">Enter your WiFi credentials. After saving, the device will restart and connect to your network. You can then access the full settings at the device's IP address.</p>
  <label for="ssid">WiFi SSID</label>
  <input type="text" id="ssid" placeholder="Your WiFi network name">
  <label for="pass">WiFi Password</label>
  <input type="password" id="pass" placeholder="WiFi password">
  <div style="margin-top:6px"><input type="checkbox" id="showpass" onchange="document.getElementById('pass').type=this.checked?'text':'password'" style="vertical-align:middle"><label for="showpass" style="color:#8B949E;font-size:12px;margin:0 0 0 4px;display:inline">Show password</label></div>
  <button class="btn" onclick="saveWifi()">Save &amp; Connect</button>
  <div id="msg" style="margin-top:10px;font-size:13px;text-align:center"></div>
</div>
<script>
function saveWifi(){
  var s=document.getElementById('ssid').value,p=document.getElementById('pass').value;
  if(!s){document.getElementById('msg').innerHTML='<span style="color:#F85149">Enter SSID</span>';return;}
  document.getElementById('msg').innerHTML='<span style="color:#58A6FF">Saving...</span>';
  var d=new URLSearchParams();d.append('ssid',s);d.append('pass',p);
  fetch('/save/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:d.toString()})
    .then(function(){document.body.innerHTML='<div style="text-align:center;padding-top:80px"><h2 style="color:#3FB950">WiFi Saved!</h2><p style="color:#8B949E;margin-top:10px">Restarting... Connect to your WiFi and open the device IP in a browser.</p></div>';})
    .catch(function(e){document.getElementById('msg').style.color='#F85149';document.getElementById('msg').textContent='Connection error';console.warn('saveWifi:',e);});
}
</script>
</body></html>
)rawliteral";

// ---------------------------------------------------------------------------
//  Main page (full settings with collapsible sections)
// ---------------------------------------------------------------------------
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BambuHelper Setup</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: #0D1117; color: #E6EDF3; padding: 16px;
    max-width: 520px; margin: 0 auto;
  }
  h1 { color: #58A6FF; font-size: 22px; margin-bottom: 6px; }
  .subtitle { color: #8B949E; font-size: 13px; margin-bottom: 20px; }
  label { display: block; color: #8B949E; font-size: 13px; margin-bottom: 4px; margin-top: 10px; }
  input[type=text], input[type=password], input[type=number], select {
    width: 100%; padding: 8px 10px; border: 1px solid #30363D;
    border-radius: 6px; background: #0D1117; color: #E6EDF3;
    font-size: 14px; outline: none;
  }
  input:focus, select:focus { border-color: #58A6FF; }
  input[type=range] { width: 100%; margin-top: 6px; }
  .check-row { display: flex; align-items: center; gap: 8px; margin-top: 10px; }
  .check-row input { width: auto; }
  .check-row label { margin: 0; }
  .btn {
    display: block; width: 100%; padding: 10px; border: none;
    border-radius: 6px; font-size: 15px; font-weight: 600;
    cursor: pointer; margin-top: 16px; text-align: center;
  }
  .btn-primary { background: #238636; color: #fff; }
  .btn-primary:hover { background: #2EA043; }
  .btn-blue { background: #1F6FEB; color: #fff; }
  .btn-blue:hover { background: #388BFD; }
  .btn-danger { background: #DA3633; color: #fff; font-size: 13px; padding: 8px; }
  .btn-danger:hover { background: #F85149; }
  .status {
    padding: 8px 12px; border-radius: 6px; margin-bottom: 12px;
    font-size: 13px; font-weight: 600;
  }
  .status-ok { background: #0D1117; border-left: 3px solid #3FB950; color: #3FB950; }
  .status-off { background: #0D1117; border-left: 3px solid #F85149; color: #F85149; }
  .status-na { background: #0D1117; border-left: 3px solid #8B949E; color: #8B949E; }
  #liveStats { margin-top: 10px; font-size: 12px; color: #8B949E; }
  .stat-row { display: flex; justify-content: space-between; padding: 2px 0; }
  .stat-val { color: #E6EDF3; }
  .toast {
    position: fixed; top: 16px; left: 50%; transform: translateX(-50%);
    background: #238636; color: #fff; padding: 10px 20px; border-radius: 8px;
    font-size: 14px; font-weight: 600; display: none; z-index: 99;
  }
  .gauge-section { margin-top: 14px; padding: 10px; background: #0D1117; border-radius: 6px; }
  .gauge-section h3 { font-size: 13px; color: #E6EDF3; margin-bottom: 8px; }
  .color-row { display: flex; align-items: center; gap: 8px; margin: 6px 0; }
  .color-row label { margin: 0; min-width: 55px; font-size: 12px; }
  .color-row input[type=color] {
    width: 36px; height: 28px; border: 1px solid #30363D; border-radius: 4px;
    background: #0D1117; cursor: pointer; padding: 1px;
  }
  .theme-bar { display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 10px; }
  .theme-btn {
    padding: 6px 12px; border: 1px solid #30363D; border-radius: 6px;
    background: #0D1117; color: #8B949E; font-size: 12px; cursor: pointer;
  }
  .theme-btn:hover { border-color: #58A6FF; color: #E6EDF3; }
  .global-colors { display: flex; gap: 16px; margin-bottom: 8px; }
  .global-colors .color-row { margin: 0; }

  /* Collapsible sections */
  .section { margin-bottom: 12px; }
  .section-header {
    display: flex; justify-content: space-between; align-items: center;
    background: #161B22; border: 1px solid #30363D; border-radius: 8px;
    padding: 12px 16px; cursor: pointer; user-select: none;
  }
  .section-header h2 { color: #58A6FF; font-size: 16px; margin: 0; }
  .section-header .arrow {
    transition: transform 0.3s ease; font-size: 12px; color: #8B949E;
  }
  .section-header .arrow.open { transform: rotate(90deg); }
  .section-content {
    max-height: 0; overflow: hidden; opacity: 0;
    transition: max-height 0.4s ease, opacity 0.3s ease;
  }
  .section-content.open { max-height: 5000px; opacity: 1; }
  .section-body {
    background: #161B22; border: 1px solid #30363D; border-top: none;
    border-radius: 0 0 8px 8px; padding: 16px;
  }
  .section.open .section-header { border-radius: 8px 8px 0 0; }
</style>
</head>
<body>
<h1>BambuHelper</h1>
<p class="subtitle">Bambu Lab Printer Monitor</p>
<div id="toast" class="toast">Applied!</div>

<!-- ===== Section 1: Printer Settings ===== -->
<div class="section" id="s-printer">
  <div class="section-header" onclick="toggleSection('printer')">
    <h2>Printer Settings</h2>
    <span class="arrow" id="arr-printer">&#9654;</span>
  </div>
  <div class="section-content" id="sec-printer">
    <div class="section-body">
      <div style="display:flex;gap:8px;margin-bottom:12px">
        <button class="tab-btn" id="tab0" onclick="selectPrinterTab(0)"
                style="flex:1;padding:8px;border:1px solid #30363D;border-radius:6px;background:#238636;color:#fff;cursor:pointer;font-weight:600">Printer 1</button>
        <button class="tab-btn" id="tab1" onclick="selectPrinterTab(1)"
                style="flex:1;padding:8px;border:1px solid #30363D;border-radius:6px;background:#0D1117;color:#8B949E;cursor:pointer">Printer 2</button>
      </div>
      <div id="printerStatus" class="%STATUS_CLASS%">%STATUS_TEXT%</div>
      <label for="connmode">Connection Mode</label>
      <select id="connmode" onchange="toggleConnMode()">
        <option value="local" %MODE_LOCAL%>LAN Mode</option>
        <option value="cloud_all" %MODE_CLOUD_ALL%>Bambu Cloud (All printers)</option>
      </select>

      <div id="localFields">
        <label for="pname">Printer Name</label>
        <input type="text" id="pname" value="%PNAME%" placeholder="My P1S" maxlength="23">
        <label for="ip">Printer IP Address</label>
        <input type="text" id="ip" value="%IP%" placeholder="192.168.1.xxx">
        <label for="serial">Serial Number</label>
        <input type="text" id="serial" value="%SERIAL%" placeholder="01P00A000000000" maxlength="19" style="text-transform:uppercase">
        <label for="code">LAN Access Code</label>
        <input type="text" id="code" placeholder="Leave blank to keep current" maxlength="8">
      </div>

      <div id="cloudFields" style="display:none">
        <p style="font-size:12px;color:#8B949E;margin:10px 0">Connect any printer via Bambu Cloud (no LAN mode needed).<br>Token valid ~3 months. Your password is NOT stored.</p>
        <label for="region">Server Region</label>
        <select id="region">
          <option value="us" %REGION_US%>Americas (US)</option>
          <option value="eu" %REGION_EU%>Europe (EU)</option>
          <option value="cn" %REGION_CN%>China (CN)</option>
        </select>
        <div id="cloudStatus" style="margin-top:8px;font-size:13px;color:#8B949E">%CLOUD_STATUS%</div>
        <div style="margin-top:10px">
          <p style="font-size:12px;color:#8B949E;margin-bottom:8px">
            <b>How to get your token:</b><br>
            1. Open <a href="https://bambulab.com" style="color:#58A6FF" target="_blank">bambulab.com</a> and log in<br>
            2. Press <b>F12</b> to open DevTools<br>
            3. Go to <b>Application</b> (Chrome/Edge) or <b>Storage</b> (Firefox) tab<br>
            4. Expand <b>Cookies</b> &rarr; click <b>bambulab.com</b><br>
            5. Find and copy the <b>token</b> cookie value<br>
            <a href="https://github.com/Keralots/BambuHelper#getting-a-cloud-token" style="color:#58A6FF" target="_blank">Detailed instructions</a>
          </p>
          <label for="cl_token">Access Token</label>
          <textarea id="cl_token" rows="3" style="width:100%;padding:8px;border:1px solid #30363D;border-radius:6px;background:#0D1117;color:#E6EDF3;font-size:11px;font-family:monospace;resize:vertical" placeholder="Paste your Bambu Cloud token here..."></textarea>
        </div>
        <label for="cl_serial">Printer Serial Number</label>
        <input type="text" id="cl_serial" value="%SERIAL%" placeholder="01P00A000000000" maxlength="19">
        <label for="cl_pname">Printer Name</label>
        <input type="text" id="cl_pname" value="%PNAME%" placeholder="My Printer" maxlength="23">
        <p style="font-size:11px;color:#8B949E;margin-top:6px">Find your serial in Bambu Handy or on the printer's label.</p>
        <button type="button" class="btn btn-danger" style="margin-top:10px;font-size:12px;padding:6px"
                id="cloudLogoutBtn" onclick="cloudLogout()">Clear Token</button>
      </div>

      <div id="liveStats"></div>
      <button type="button" class="btn btn-primary" onclick="savePrinter()">Save Printer Settings</button>
    </div>
  </div>
</div>

<!-- ===== Section 2: Display ===== -->
<div class="section" id="s-display">
  <div class="section-header" onclick="toggleSection('display')">
    <h2>Display</h2>
    <span class="arrow" id="arr-display">&#9654;</span>
  </div>
  <div class="section-content" id="sec-display">
    <div class="section-body">
      <h3 style="color:#58A6FF;font-size:14px;margin-bottom:10px">Brightness</h3>
      <label for="bright">Brightness: <span id="brightVal">%BRIGHT%</span></label>
      <input type="range" id="bright" min="10" max="255" step="5" value="%BRIGHT%"
             oninput="document.getElementById('brightVal').textContent=this.value;sendBrightness(this.value)">
      <div style="margin-top:12px">
        <div class="check-row">
          <input type="checkbox" id="nighten" value="1" %NIGHTEN% onchange="document.getElementById('nightFields').style.display=this.checked?'block':'none';toggleSetting('nighten',this.checked)">
          <label for="nighten">Night mode (scheduled dimming)</label>
        </div>
        <div id="nightFields" style="display:%NIGHTDISP%;padding:10px;background:#0D1117;border:1px solid #30363D;border-radius:6px;margin-top:6px">
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:8px">
            <div>
              <label for="nstart" style="font-size:12px">Dim from</label>
              <select id="nstart">%NIGHT_START_OPTS%</select>
            </div>
            <div>
              <label for="nend" style="font-size:12px">Dim until</label>
              <select id="nend">%NIGHT_END_OPTS%</select>
            </div>
          </div>
          <label for="nbright" style="font-size:12px">Night brightness: <span id="nbrightVal">%NBRIGHT%</span></label>
          <input type="range" id="nbright" min="0" max="255" step="5" value="%NBRIGHT%"
                 oninput="document.getElementById('nbrightVal').textContent=this.value">
          <p style="font-size:11px;color:#8B949E;margin-top:4px">Recommended: 20-50 for night use. Requires NTP time sync.</p>
        </div>
      </div>

      <div style="margin-top:16px;padding-top:12px;border-top:1px solid #30363D">
        <h3 style="color:#58A6FF;font-size:14px;margin-bottom:10px">After Print Completes</h3>
        <label for="afterprint">When print finishes</label>
        <select id="afterprint" onchange="toggleAfterPrint()">
          <option value="0" %AP_CLOCK0%>Go to clock/screensaver immediately</option>
          <option value="1" %AP_F1%>Show finish screen for 1 minute</option>
          <option value="3" %AP_F3%>Show finish screen for 3 minutes</option>
          <option value="5" %AP_F5%>Show finish screen for 5 minutes</option>
          <option value="10" %AP_F10%>Show finish screen for 10 minutes</option>
          <option value="custom" %AP_CUSTOM%>Custom duration...</option>
          <option value="keepon" %AP_KEEPON%>Keep finish screen on</option>
        </select>
        <div id="customMinsWrap" style="display:%CUSTOM_DISP%;margin-top:6px">
          <label for="fmins" style="font-size:12px">Minutes</label>
          <input type="number" id="fmins" min="1" max="999" value="%FMINS%">
        </div>
        <div class="check-row" style="margin-top:8px">
          <input type="checkbox" id="dack" value="1" %DACK% onchange="toggleSetting('dack',this.checked)">
          <label for="dack">Wait for door open before timeout</label>
        </div>
        <label for="ssbright" style="margin-top:12px;font-size:12px">Screensaver brightness: <span id="ssbrightVal">%SSBRIGHT%</span></label>
        <input type="range" id="ssbright" min="0" max="255" step="5" value="%SSBRIGHT%"
               oninput="document.getElementById('ssbrightVal').textContent=this.value">
        <p style="font-size:11px;color:#8B949E;margin-top:4px">Brightness when clock/screensaver is active. Set to 0 to turn off backlight.</p>
        <div class="check-row" id="pong-row">
          <input type="checkbox" id="pong" value="1" %PONG% onchange="toggleSetting('pong',this.checked)">
          <label for="pong">Breakout clock (animated game as screensaver)</label>
        </div>
        <p style="font-size:11px;color:#8B949E;margin-top:4px">Without a physical button, clock is always shown instead of turning display off.</p>
      </div>

      <div style="margin-top:16px;padding-top:12px;border-top:1px solid #30363D">
        <h3 style="color:#58A6FF;font-size:14px;margin-bottom:10px">Screen</h3>
        <label for="rotation">Screen Rotation</label>
        <select id="rotation">
          <option value="0" %ROT0%>0&deg; (default)</option>
          <option value="1" %ROT1%>90&deg;</option>
          <option value="2" %ROT2%>180&deg;</option>
          <option value="3" %ROT3%>270&deg;</option>
        </select>
        <div class="check-row">
          <input type="checkbox" id="abar" value="1" %ABAR% onchange="toggleSetting('abar',this.checked)">
          <label for="abar">Animated progress bar (shimmer effect)</label>
        </div>
        <div class="check-row">
          <input type="checkbox" id="slbl" value="1" %SLBL% onchange="toggleSetting('slbl',this.checked)">
          <label for="slbl">Smaller gauge labels</label>
        </div>
      </div>

      <div style="margin-top:16px;padding-top:12px;border-top:1px solid #30363D">
        <h3 style="color:#58A6FF;font-size:14px;margin-bottom:10px">Clock Settings</h3>
        <label for="tz">Timezone (DST switches automatically)</label>
        <select id="tz">
%TIMEZONE_OPTIONS%
        </select>
        <div class="check-row">
          <input type="checkbox" id="use24h" value="1" %USE24H% onchange="toggleSetting('use24h',this.checked)">
          <label for="use24h">24-hour time format</label>
        </div>
        <label for="datefmt" style="margin-top:10px">Date format</label>
        <select id="datefmt">
          <option value="0" %DATEFMT0%>DD.MM.YYYY (31.12.2025)</option>
          <option value="1" %DATEFMT1%>DD-MM-YYYY (31-12-2025)</option>
          <option value="2" %DATEFMT2%>MM/DD/YYYY (12/31/2025)</option>
          <option value="3" %DATEFMT3%>YYYY-MM-DD (2025-12-31)</option>
          <option value="4" %DATEFMT4%>DD MMM YYYY (31 Dec 2025)</option>
          <option value="5" %DATEFMT5%>MMM DD, YYYY (Dec 31, 2025)</option>
        </select>
      </div>

      <div style="margin-top:16px;padding-top:12px;border-top:1px solid #30363D">
        <h3 style="color:#58A6FF;font-size:14px;margin-bottom:10px">Gauge Colors</h3>
        <div class="theme-bar">
          <button type="button" class="theme-btn" onclick="applyTheme('default')">Default</button>
          <button type="button" class="theme-btn" onclick="applyTheme('mono_green')">Mono Green</button>
          <button type="button" class="theme-btn" onclick="applyTheme('neon')">Neon</button>
          <button type="button" class="theme-btn" onclick="applyTheme('warm')">Warm</button>
          <button type="button" class="theme-btn" onclick="applyTheme('ocean')">Ocean</button>
        </div>

        <div class="global-colors">
          <div class="color-row">
            <label>Background</label>
            <input type="color" id="clr_bg" value="%CLR_BG%">
          </div>
          <div class="color-row">
            <label>Track</label>
            <input type="color" id="clr_track" value="%CLR_TRACK%">
          </div>
        </div>

        <div class="gauge-section"><h3>Progress</h3><div class="color-row">
          <label>Arc</label><input type="color" id="prg_a" value="%PRG_A%">
          <label>Label</label><input type="color" id="prg_l" value="%PRG_L%">
          <label>Value</label><input type="color" id="prg_v" value="%PRG_V%">
        </div></div>
        <div class="gauge-section"><h3>Nozzle</h3><div class="color-row">
          <label>Arc</label><input type="color" id="noz_a" value="%NOZ_A%">
          <label>Label</label><input type="color" id="noz_l" value="%NOZ_L%">
          <label>Value</label><input type="color" id="noz_v" value="%NOZ_V%">
        </div></div>
        <div class="gauge-section"><h3>Bed</h3><div class="color-row">
          <label>Arc</label><input type="color" id="bed_a" value="%BED_A%">
          <label>Label</label><input type="color" id="bed_l" value="%BED_L%">
          <label>Value</label><input type="color" id="bed_v" value="%BED_V%">
        </div></div>
        <div class="gauge-section"><h3>Part Fan</h3><div class="color-row">
          <label>Arc</label><input type="color" id="pfn_a" value="%PFN_A%">
          <label>Label</label><input type="color" id="pfn_l" value="%PFN_L%">
          <label>Value</label><input type="color" id="pfn_v" value="%PFN_V%">
        </div></div>
        <div class="gauge-section"><h3>Aux Fan</h3><div class="color-row">
          <label>Arc</label><input type="color" id="afn_a" value="%AFN_A%">
          <label>Label</label><input type="color" id="afn_l" value="%AFN_L%">
          <label>Value</label><input type="color" id="afn_v" value="%AFN_V%">
        </div></div>
        <div class="gauge-section"><h3>Chamber Fan</h3><div class="color-row">
          <label>Arc</label><input type="color" id="cfn_a" value="%CFN_A%">
          <label>Label</label><input type="color" id="cfn_l" value="%CFN_L%">
          <label>Value</label><input type="color" id="cfn_v" value="%CFN_V%">
        </div></div>
      </div>

      <button type="button" class="btn btn-blue" onclick="applyDisplay()">Apply Display Settings</button>
    </div>
  </div>
</div>

<!-- ===== Section 3: Hardware & Multi-Printer ===== -->
<div class="section" id="s-rotate">
  <div class="section-header" onclick="toggleSection('rotate')">
    <h2>Hardware &amp; Multi-Printer</h2>
    <span class="arrow" id="arr-rotate">&#9654;</span>
  </div>
  <div class="section-content" id="sec-rotate">
    <div class="section-body">
      <label for="rotmode">Display Rotation Mode</label>
      <select id="rotmode">
        <option value="0" %RMODE_OFF%>Off (show selected printer only)</option>
        <option value="1" %RMODE_AUTO%>Auto-rotate (cycle all connected)</option>
        <option value="2" %RMODE_SMART%>Smart (prioritize printing)</option>
      </select>
      <label for="rotinterval">Rotation interval (seconds)</label>
      <input type="number" id="rotinterval" min="10" max="600" value="%ROT_INTERVAL%">
      <p style="font-size:11px;color:#8B949E;margin-top:4px">Smart mode shows the printing printer. Rotates only when both are printing.</p>

      <div style="margin-top:16px;padding-top:12px;border-top:1px solid #30363D">
        <label for="btntype">Physical Button</label>
        <select id="btntype" onchange="toggleBtnPin()">
          <option value="0" %BTN_OFF%>Disabled</option>
          <option value="1" %BTN_PUSH%>Push Button (active LOW)</option>
          <option value="2" %BTN_TOUCH%>TTP223 Touch (active HIGH)</option>
          <option value="3" %BTN_SCREEN%>Touchscreen (XPT2046)</option>
        </select>
        <div id="btnPinRow">
          <label for="btnpin">Button GPIO Pin</label>
          <input type="number" id="btnpin" min="1" max="48" value="%BTN_PIN%">
          <p style="font-size:11px;color:#8B949E;margin-top:4px">Button switches between printers. Wakes display from sleep.</p>
        </div>
      </div>

      <div style="margin-top:16px;padding-top:12px;border-top:1px solid #30363D">
        <label for="buzzen">Buzzer (optional)</label>
        <select id="buzzen" onchange="toggleBuzPin()">
          <option value="0" %BUZ_OFF%>Disabled</option>
          <option value="1" %BUZ_ON%>Enabled</option>
        </select>
        <div id="buzFields" style="display:none">
          <label for="buzpin">Buzzer GPIO Pin</label>
          <input type="number" id="buzpin" min="1" max="48" value="%BUZ_PIN%">
          <p style="font-size:11px;color:#8B949E;margin-top:4px">Passive buzzer. Beeps on print complete and errors.</p>
          <label style="margin-top:8px">Quiet Hours (optional)</label>
          <div style="display:flex;gap:8px;align-items:center">
            <input type="number" id="buzqs" min="0" max="23" value="%BUZ_QS%" style="width:60px" placeholder="22">
            <span style="color:#8B949E">to</span>
            <input type="number" id="buzqe" min="0" max="23" value="%BUZ_QE%" style="width:60px" placeholder="7">
            <span style="font-size:11px;color:#8B949E">(0-0 = off)</span>
          </div>
          <button type="button" id="buzTestBtn" class="btn btn-blue" style="margin-top:12px;width:auto;padding:8px 16px"
                  onclick="testBuzzer()">Test: Print Finished</button>
        </div>
      </div>

      <button type="button" class="btn btn-blue" onclick="saveRotation()">Apply</button>
    </div>
  </div>
</div>

<!-- ===== Section 5: WiFi & System ===== -->
<div class="section" id="s-wifi">
  <div class="section-header" onclick="toggleSection('wifi')">
    <h2>WiFi &amp; System</h2>
    <span class="arrow" id="arr-wifi">&#9654;</span>
  </div>
  <div class="section-content" id="sec-wifi">
    <div class="section-body">
      <label for="ssid">WiFi SSID</label>
      <input type="text" id="ssid" value="%SSID%" placeholder="Your WiFi name">
      <label for="pass">WiFi Password</label>
      <input type="password" id="pass" placeholder="Leave blank to keep current">
      <div class="check-row"><input type="checkbox" id="showpass2" onchange="document.getElementById('pass').type=this.checked?'text':'password'"><label for="showpass2">Show password</label></div>

      <label for="netmode" style="margin-top:16px">IP Assignment</label>
      <select id="netmode" onchange="toggleStatic()">
        <option value="dhcp" %NET_DHCP%>DHCP (automatic)</option>
        <option value="static" %NET_STATIC%>Static IP</option>
      </select>
      <div id="staticFields" style="display:none">
        <label for="net_ip">IP Address</label>
        <input type="text" id="net_ip" value="%NET_IP%" placeholder="192.168.1.100">
        <label for="net_gw">Gateway</label>
        <input type="text" id="net_gw" value="%NET_GW%" placeholder="192.168.1.1">
        <label for="net_sn">Subnet Mask</label>
        <input type="text" id="net_sn" value="%NET_SN%" placeholder="255.255.255.0">
        <label for="net_dns">DNS Server</label>
        <input type="text" id="net_dns" value="%NET_DNS%" placeholder="8.8.8.8">
      </div>
      <div class="check-row">
        <input type="hidden" name="has_showip" value="1">
        <input type="checkbox" id="showip" value="1" %SHOWIP%>
        <label for="showip">Show IP at startup (3s)</label>
      </div>

      <button type="button" class="btn btn-primary" onclick="saveWifi()">Save WiFi &amp; Restart</button>

      <div style="margin-top:20px;padding-top:12px;border-top:1px solid #30363D">
        <h3 style="color:#58A6FF;font-size:14px;margin-bottom:10px">Settings Backup</h3>
        <p style="font-size:11px;color:#8B949E;margin-bottom:10px">
          Export/import all settings as JSON. Useful before reflashing firmware.<br>
          Note: Cloud token is NOT included — you'll need to re-login after import.
        </p>
        <button type="button" class="btn btn-blue" style="display:inline-block;width:auto;padding:8px 16px"
                onclick="exportSettings()">Export Settings</button>
        <div style="margin-top:12px">
          <label for="importFile" style="font-size:12px">Import Settings</label>
          <input type="file" id="importFile" accept=".json"
                 style="width:100%;margin-top:4px;padding:6px;background:#0D1117;border:1px solid #30363D;border-radius:6px;color:#C9D1D9">
          <button type="button" class="btn btn-primary" style="margin-top:8px;font-size:13px;padding:8px"
                  onclick="importSettings()">Import &amp; Restart</button>
          <div id="importStatus" style="margin-top:8px;font-size:13px"></div>
        </div>
      </div>
      <div style="margin-top:20px;padding-top:12px;border-top:1px solid #30363D">
        <h3 style="color:#58A6FF;font-size:14px;margin-bottom:6px">Firmware Update</h3>
        <p style="font-size:13px;color:#8B949E;margin-bottom:10px">
          Current version: <b style="color:#58A6FF">%FW_VER%</b>
        </p>
)rawliteral"
#ifdef ENABLE_OTA_AUTO
R"rawliteral(
        <div style="display:flex;gap:4px;margin-bottom:12px">
          <button type="button" id="tab-auto-btn" onclick="switchFwTab('auto')"
            style="flex:1;padding:8px;border:1px solid #58A6FF;border-radius:6px;background:#21262D;color:#E6EDF3;font-size:13px;cursor:pointer">Auto Update</button>
          <button type="button" id="tab-manual-btn" onclick="switchFwTab('manual')"
            style="flex:1;padding:8px;border:1px solid #30363D;border-radius:6px;background:#0D1117;color:#8B949E;font-size:13px;cursor:pointer">Manual Upload</button>
        </div>
        <div id="fw-tab-auto">
          <p style="font-size:12px;color:#8B949E;margin-bottom:10px">
            Check for and install BambuHelper display device firmware updates directly from GitHub.
          </p>
          <div id="updateCheck" style="margin-bottom:12px">
            <button type="button" class="btn btn-blue" onclick="checkForUpdates()">Check for Updates</button>
            <span id="updateResult" style="margin-left:8px;font-size:13px"></span>
          </div>
          <div id="updateInfo" style="display:none;margin-bottom:12px;padding:10px;background:#0D1117;border:1px solid #30363D;border-radius:6px">
            <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:6px">
              <div>
                <b id="updateVer" style="color:#3FB950;font-size:14px"></b>
                <span id="updateDate" style="color:#8B949E;font-size:12px;margin-left:8px"></span>
              </div>
              <div style="display:flex;gap:6px;align-items:center;flex-wrap:wrap">
                <button id="installBtn" type="button" class="btn btn-primary" style="font-size:12px;padding:4px 12px" onclick="installUpdate()">Install on BambuHelper</button>
                <a id="updateLink" href="#" target="_blank" class="btn" style="font-size:12px;padding:4px 12px;text-decoration:none;background:#21262D;color:#C9D1D9;border:1px solid #30363D;border-radius:6px">Manual download</a>
              </div>
            </div>
            <div id="autoOtaWrap" style="display:none;margin-top:10px">
              <div style="background:#30363D;border-radius:4px;height:16px;overflow:hidden">
                <div id="autoOtaBar" style="background:#238636;height:100%;width:0%;transition:width 0.4s;border-radius:4px"></div>
              </div>
              <div id="autoOtaStatus" style="text-align:center;font-size:12px;color:#8B949E;margin-top:4px">Starting...</div>
              <p style="font-size:11px;color:#F0883E;margin-top:6px;text-align:center">&#9888; Do not power off or close this page</p>
            </div>
          </div>
        </div>
        <div id="fw-tab-manual" style="display:none">
          <p style="font-size:12px;color:#8B949E;margin-bottom:10px">
            Upload a .bin file to update BambuHelper display device firmware. Settings are preserved. Device restarts automatically.
          </p>
)rawliteral"
#else
R"rawliteral(
          <p style="font-size:12px;color:#8B949E;margin-bottom:10px">
            Upload a .bin file to update BambuHelper display device firmware. Settings are preserved. Device restarts automatically.
          </p>
)rawliteral"
#endif
R"rawliteral(
          <input type="file" id="otaFile" accept=".bin"
                 style="width:100%;padding:6px;background:#0D1117;border:1px solid #30363D;border-radius:6px;color:#C9D1D9">
          <div id="otaProgress" style="display:none;margin-top:12px">
            <div style="background:#30363D;border-radius:4px;height:20px;overflow:hidden">
              <div id="otaBar" style="background:#238636;height:100%;width:0%;transition:width 0.3s;border-radius:4px"></div>
            </div>
            <div id="otaPct" style="text-align:center;font-size:13px;color:#E6EDF3;margin-top:4px">0%</div>
          </div>
          <div id="otaStatus" style="margin-top:8px;font-size:13px"></div>
          <button type="button" class="btn btn-primary" style="margin-top:8px" onclick="startOta()">Upload &amp; Update</button>
)rawliteral"
#ifdef ENABLE_OTA_AUTO
R"rawliteral(
        </div>
)rawliteral"
#endif
R"rawliteral(
      </div>
      <div style="margin-top:20px;padding-top:12px;border-top:1px solid #30363D">
        <button type="button" class="btn btn-danger" onclick="if(confirm('Reset all settings to factory defaults?'))location='/reset'">Factory Reset</button>
      </div>
    </div>
  </div>
</div>

<!-- ===== Section 5: Power Monitoring ===== -->
<div class="section" id="s-power">
  <div class="section-header" onclick="toggleSection('power')">
    <h2>Power Monitoring</h2>
    <span class="arrow" id="arr-power">&#9654;</span>
  </div>
  <div class="section-content" id="sec-power">
    <div class="section-body">
      <p style="font-size:12px;color:#8B949E;margin-bottom:12px">
        Show live power consumption from a Tasmota smart plug on the display.<br>
        Replaces or alternates with the layer counter in the bottom status bar.
      </p>
      <div class="check-row">
        <input type="checkbox" id="tsm_en" value="1" %TSM_EN%>
        <label for="tsm_en">Enable power monitoring</label>
      </div>
      <label for="tsm_ip" style="margin-top:12px">Tasmota plug IP address</label>
      <input type="text" id="tsm_ip" value="%TSM_IP%" placeholder="192.168.1.x" maxlength="15">
      <label style="margin-top:12px">Display mode</label>
      <div style="display:flex;flex-direction:column;gap:6px;margin-top:4px">
        <label style="display:flex;align-items:center;gap:8px;font-size:13px;color:#C9D1D9">
          <input type="radio" name="tsm_dm" value="0" %TSM_DM0%> Alternate: layer count / watts (every 4s)
        </label>
        <label style="display:flex;align-items:center;gap:8px;font-size:13px;color:#C9D1D9">
          <input type="radio" name="tsm_dm" value="1" %TSM_DM1%> Always show watts
        </label>
      </div>
      <label for="tsm_slot" style="margin-top:12px">Assigned printer</label>
      <select id="tsm_slot">%TSM_SLOT_OPTIONS%</select>
      <label for="tsm_pi" style="margin-top:12px">Poll interval</label>
      <select id="tsm_pi">%TSM_PI_OPTIONS%</select>
      <button type="button" class="btn btn-primary" onclick="savePower()">Save Power Settings</button>
      <div id="powerStatus" style="margin-top:8px;font-size:13px"></div>
    </div>
  </div>
</div>

<!-- ===== Section 6: Diagnostics ===== -->
<div class="section" id="s-diag">
  <div class="section-header" onclick="toggleSection('diag')">
    <h2>Diagnostics</h2>
    <span class="arrow" id="arr-diag">&#9654;</span>
  </div>
  <div class="section-content" id="sec-diag">
    <div class="section-body">
      <div class="check-row">
        <input type="checkbox" id="dbglog" onchange="toggleDebug(this.checked)" %DBGLOG%>
        <label for="dbglog">Verbose Serial logging (USB)</label>
      </div>
      <div id="diagInfo" style="margin-top:10px;font-size:12px;color:#8B949E"></div>
    </div>
  </div>
</div>

<script>
// --- HTML escape helper ---
function esc(s){var d=document.createElement('div');d.appendChild(document.createTextNode(s));return d.innerHTML;}

// --- Debounced brightness send ---
var _brightTimer=null;
function sendBrightness(val){clearTimeout(_brightTimer);_brightTimer=setTimeout(function(){fetch('/brightness?val='+val);},150);}

// --- Polling timers (started/stopped when sections open/close) ---
var diagTimer=null,statsTimer=null;
function startPolling(id){
  stopPolling();
  if(id==='diag'){refreshDiag();diagTimer=setInterval(refreshDiag,5000);}
  if(id==='printer'||id==='diag'){statsTimer=setInterval(refreshLiveStats,3000);refreshLiveStats();}
}
function stopPolling(){
  if(diagTimer){clearInterval(diagTimer);diagTimer=null;}
  if(statsTimer){clearInterval(statsTimer);statsTimer=null;}
}

// --- Collapsible sections ---
function toggleSection(id){
  var content=document.getElementById('sec-'+id);
  var arrow=document.getElementById('arr-'+id);
  var sect=document.getElementById('s-'+id);
  var isOpen=content.classList.contains('open');
  // Close all
  document.querySelectorAll('.section-content').forEach(function(el){el.classList.remove('open');});
  document.querySelectorAll('.arrow').forEach(function(el){el.classList.remove('open');});
  document.querySelectorAll('.section').forEach(function(el){el.classList.remove('open');});
  stopPolling();
  if(!isOpen){
    content.classList.add('open');
    arrow.classList.add('open');
    sect.classList.add('open');
    localStorage.setItem('bambu_section',id);
    startPolling(id);
  } else {
    localStorage.removeItem('bambu_section');
  }
}
(function(){
  var saved=localStorage.getItem('bambu_section');
  if(saved) toggleSection(saved); else toggleSection('printer');
})();

// --- Multi-printer tab selection ---
var currentSlot=0;
function selectPrinterTab(slot){
  currentSlot=slot;
  document.querySelectorAll('.tab-btn').forEach(function(btn,i){
    btn.style.background=(i===slot)?'#238636':'#0D1117';
    btn.style.color=(i===slot)?'#fff':'#8B949E';
  });
  var reqSlot=slot;
  fetch('/printer/config?slot='+slot).then(function(r){return r.json();}).then(function(d){
    if(reqSlot!==currentSlot)return;
    document.getElementById('connmode').value=d.mode;
    document.getElementById('pname').value=d.name||'';
    document.getElementById('ip').value=d.ip||'';
    document.getElementById('serial').value=d.serial||'';
    document.getElementById('code').value=d.code||'';
    document.getElementById('cl_serial').value=d.serial||'';
    document.getElementById('cl_pname').value=d.name||'';
    document.getElementById('region').value=d.region||'us';
    document.getElementById('cl_token').value='';
    toggleConnMode();
    var ps=document.getElementById('printerStatus');
    if(d.connected){ps.className='status status-ok';ps.textContent='Connected';}
    else if(d.configured){ps.className='status status-off';ps.textContent='Disconnected';}
    else{ps.className='status status-na';ps.textContent='Not configured';}
  }).catch(function(e){console.warn('selectPrinterTab:',e);});
}

// --- Utility ---
function showToast(msg){
  var t=document.getElementById('toast');
  t.textContent=msg||'Applied!';
  t.style.display='block';
  setTimeout(function(){t.style.display='none';},msg&&msg.length>40?5000:2000);
}

function toggleStatic(){
  var m=document.getElementById('netmode').value;
  document.getElementById('staticFields').style.display=(m==='static')?'block':'none';
}
toggleStatic();

function toggleConnMode(){
  var v=document.getElementById('connmode').value;
  var cloud=(v==='cloud_all');
  document.getElementById('localFields').style.display=cloud?'none':'block';
  document.getElementById('cloudFields').style.display=cloud?'block':'none';
}
toggleConnMode();

// --- Save Printer (no restart) ---
function savePrinter(){
  var p=new URLSearchParams();
  p.append('slot',currentSlot);
  var mode=document.getElementById('connmode').value;
  p.append('connmode',mode);
  if(mode==='cloud_all'){
    p.append('serial',document.getElementById('cl_serial').value);
    p.append('pname',document.getElementById('cl_pname').value);
    p.append('region',document.getElementById('region').value);
    var token=document.getElementById('cl_token').value.trim();
    if(token) p.append('token',token);
  } else {
    p.append('pname',document.getElementById('pname').value);
    p.append('ip',document.getElementById('ip').value);
    p.append('serial',document.getElementById('serial').value);
    p.append('code',document.getElementById('code').value);
  }
  fetch('/save/printer',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.status==='ok'&&d.warning) showToast('Saved! Warning: '+d.warning);
      else if(d.status==='ok') showToast('Printer settings saved!');
      else showToast('Error: '+(d.message||'save failed'));
    })
    .catch(function(e){showToast('Network error');console.warn('savePrinter:',e);});
}

// --- Save WiFi (restart) ---
function saveWifi(){
  var p=new URLSearchParams();
  p.append('ssid',document.getElementById('ssid').value);
  p.append('pass',document.getElementById('pass').value);
  p.append('netmode',document.getElementById('netmode').value);
  p.append('net_ip',document.getElementById('net_ip').value);
  p.append('net_gw',document.getElementById('net_gw').value);
  p.append('net_sn',document.getElementById('net_sn').value);
  p.append('net_dns',document.getElementById('net_dns').value);
  p.append('has_showip','1');
  if(document.getElementById('showip').checked) p.append('showip','1');
  fetch('/save/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(){
      document.body.innerHTML='<div style="text-align:center;padding-top:80px"><h2 style="color:#3FB950">WiFi Saved!</h2><p style="color:#8B949E;margin-top:10px">Restarting...</p></div>';
    })
    .catch(function(e){showToast('Network error');console.warn('saveWifi:',e);});
}

// --- Cloud ---
function cloudLogout(){
  fetch('/cloud/logout',{method:'POST'}).then(function(){
    var cs=document.getElementById('cloudStatus');cs.style.color='#8B949E';cs.textContent='No token set';
    document.getElementById('cl_token').value='';
  });
}

// --- Hardware & Multi-Printer ---
function toggleBtnPin(){
  var v=document.getElementById('btntype').value;
  document.getElementById('btnPinRow').style.display=
    (v==='0'||v==='3')?'none':'block';
}
toggleBtnPin();

function toggleBuzPin(){
  document.getElementById('buzFields').style.display=
    document.getElementById('buzzen').value==='0'?'none':'block';
}
toggleBuzPin();

var buzTestSounds=[
  {id:0, name:'Print Finished'},
  {id:1, name:'Error'},
  {id:2, name:'Connected'}
];
var buzTestIdx=0;
function testBuzzer(){
  var snd=buzTestSounds[buzTestIdx];
  fetch('/buzzer/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'sound='+snd.id})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok') showToast('Playing: '+snd.name);})
    .catch(function(e){showToast('Buzzer test failed');console.warn('testBuzzer:',e);});
  buzTestIdx=(buzTestIdx+1)%buzTestSounds.length;
  document.getElementById('buzTestBtn').textContent='Test: '+buzTestSounds[buzTestIdx].name;
}

function saveRotation(){
  var p=new URLSearchParams();
  p.append('rotmode',document.getElementById('rotmode').value);
  p.append('rotinterval',document.getElementById('rotinterval').value);
  p.append('btntype',document.getElementById('btntype').value);
  p.append('btnpin',document.getElementById('btnpin').value);
  p.append('buzzen',document.getElementById('buzzen').value);
  p.append('buzpin',document.getElementById('buzpin').value);
  p.append('buzqs',document.getElementById('buzqs').value);
  p.append('buzqe',document.getElementById('buzqe').value);
  fetch('/save/rotation',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok') showToast('Settings saved');})
    .catch(function(e){showToast('Save failed');console.warn('saveRotation:',e);});
}

// --- Power monitoring ---
function savePower(){
  var p=new URLSearchParams();
  if(document.getElementById('tsm_en').checked) p.append('tsm_en','1');
  p.append('tsm_ip',document.getElementById('tsm_ip').value.trim());
  var dm=document.querySelector('input[name="tsm_dm"]:checked');
  if(dm) p.append('tsm_dm',dm.value);
  p.append('tsm_slot',document.getElementById('tsm_slot').value);
  p.append('tsm_pi',document.getElementById('tsm_pi').value);
  fetch('/save/power',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok') showToast('Power settings saved');})
    .catch(function(e){showToast('Save failed');console.warn('savePower:',e);});
}

// --- Display ---
var themes={
  default:{bg:'#081018',track:'#182028',
    prg:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},noz:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},
    bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},pfn:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},
    afn:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},cfn:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'}},
  mono_green:{bg:'#000800',track:'#0A1A0A',
    prg:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},noz:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},
    bed:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},pfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},
    afn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},cfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'}},
  neon:{bg:'#0A0014',track:'#1A0A2E',
    prg:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},noz:{a:'#FF4400',l:'#FF6600',v:'#FFFFFF'},
    bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},pfn:{a:'#00FF88',l:'#00FF88',v:'#FFFFFF'},
    afn:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},cfn:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'}},
  warm:{bg:'#140A00',track:'#2E1A08',
    prg:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},noz:{a:'#FF6347',l:'#FF6347',v:'#FFEEDD'},
    bed:{a:'#FFA500',l:'#FFA500',v:'#FFEEDD'},pfn:{a:'#FFD700',l:'#FFD700',v:'#FFEEDD'},
    afn:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},cfn:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'}},
  ocean:{bg:'#000A14',track:'#0A1A2E',
    prg:{a:'#00BFFF',l:'#00BFFF',v:'#E0F0FF'},noz:{a:'#FF7F50',l:'#FF7F50',v:'#E0F0FF'},
    bed:{a:'#4169E1',l:'#4169E1',v:'#E0F0FF'},pfn:{a:'#00CED1',l:'#00CED1',v:'#E0F0FF'},
    afn:{a:'#48D1CC',l:'#48D1CC',v:'#E0F0FF'},cfn:{a:'#20B2AA',l:'#20B2AA',v:'#E0F0FF'}}
};

function applyTheme(name){
  var t=themes[name]; if(!t) return;
  document.getElementById('clr_bg').value=t.bg;
  document.getElementById('clr_track').value=t.track;
  var g=['prg','noz','bed','pfn','afn','cfn'];
  for(var i=0;i<g.length;i++){
    var c=t[g[i]];
    document.getElementById(g[i]+'_a').value=c.a;
    document.getElementById(g[i]+'_l').value=c.l;
    document.getElementById(g[i]+'_v').value=c.v;
  }
}

function applyDisplay(){
  var p=new URLSearchParams();
  p.append('bright',document.getElementById('bright').value);
  if(document.getElementById('nighten').checked) p.append('nighten','1');
  p.append('nstart',document.getElementById('nstart').value);
  p.append('nend',document.getElementById('nend').value);
  p.append('nbright',document.getElementById('nbright').value);
  p.append('ssbright',document.getElementById('ssbright').value);
  p.append('rotation',document.getElementById('rotation').value);
  var ap=document.getElementById('afterprint').value;
  if(ap==='keepon'){p.append('keepon','1');p.append('fmins','0');}
  else if(ap==='custom'){p.append('fmins',document.getElementById('fmins').value);p.append('clock','1');}
  else{p.append('fmins',ap);p.append('clock','1');}
  if(document.getElementById('dack').checked) p.append('dack','1');
  if(document.getElementById('abar').checked) p.append('abar','1');
  if(document.getElementById('pong').checked) p.append('pong','1');
  if(document.getElementById('slbl').checked) p.append('slbl','1');
  p.append('tz',document.getElementById('tz').value);
  if(document.getElementById('use24h').checked) p.append('use24h','1');
  p.append('datefmt',document.getElementById('datefmt').value);
  p.append('clr_bg',document.getElementById('clr_bg').value);
  p.append('clr_track',document.getElementById('clr_track').value);
  var g=['prg','noz','bed','pfn','afn','cfn'];
  for(var i=0;i<g.length;i++){
    p.append(g[i]+'_a',document.getElementById(g[i]+'_a').value);
    p.append(g[i]+'_l',document.getElementById(g[i]+'_l').value);
    p.append(g[i]+'_v',document.getElementById(g[i]+'_v').value);
  }
  fetch('/apply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()}).then(function(r){
    if(r.ok) showToast('Applied!'); else showToast('Error');
  }).catch(function(e){showToast('Apply failed');console.warn('applyDisplay:',e);});
}

// --- Instant checkbox toggle ---
function toggleSetting(key,on){
  fetch('/save/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key='+key+'&val='+(on?'1':'0')}).then(function(r){
    if(r.ok) showToast(on?key+' ON':key+' OFF');
    else showToast('Error');
  }).catch(function(e){showToast('Toggle failed');console.warn('toggleSetting:',e);});
}

// --- Diagnostics ---
function toggleDebug(on){
  fetch('/debug/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'on='+(on?'1':'0')}).then(r=>{
    if(r.ok) showToast(on?'Debug ON':'Debug OFF');
  });
}
function refreshDiag(){
  fetch('/debug').then(r=>r.json()).then(d=>{
    var h='';
    if(d.printers){
      d.printers.forEach(function(p){
        h+='<div style="margin-bottom:8px;padding:6px;border-left:2px solid '+(p.connected?'#3FB950':'#F85149')+'">';
        h+='<b style="color:#E6EDF3">'+esc(p.name)+'</b> (slot '+p.slot+')<br>';
        h+='<div class="stat-row"><span>MQTT:</span><span class="stat-val">'+(p.connected?'<span style="color:#3FB950">Connected</span>':'<span style="color:#F85149">Disconnected</span>')+'</span></div>';
        h+='<div class="stat-row"><span>Attempts:</span><span class="stat-val">'+p.attempts+'</span></div>';
        h+='<div class="stat-row"><span>Messages RX:</span><span class="stat-val">'+p.messages+'</span></div>';
        if(p.last_rc!==0) h+='<div class="stat-row"><span>Last error:</span><span class="stat-val" style="color:#F85149">'+esc(p.rc_text)+'</span></div>';
        h+='</div>';
      });
    }
    h+='<div class="stat-row"><span>Free heap:</span><span class="stat-val">'+Math.round(d.heap/1024)+'KB</span></div>';
    h+='<div class="stat-row"><span>Uptime:</span><span class="stat-val">'+Math.round(d.uptime/60)+'min</span></div>';
    document.getElementById('diagInfo').innerHTML=h;
  }).catch(function(e){console.warn('refreshDiag:',e);});
}
// --- Live stats (shows currently selected tab's printer) ---
function refreshLiveStats(){
  fetch('/status?slot='+currentSlot).then(r=>r.json()).then(d=>{
    var h='';
    if(d.display_off) h+='<div class="stat-row"><span>Display:</span><span class="stat-val" style="color:#F85149">Off</span></div>';
    if(d.connected){
      h+='<div class="stat-row"><span>State:</span><span class="stat-val">'+esc(d.state)+'</span></div>';
      h+='<div class="stat-row"><span>Nozzle:</span><span class="stat-val">'+d.nozzle+'/'+d.nozzle_t+'&deg;C</span></div>';
      h+='<div class="stat-row"><span>Bed:</span><span class="stat-val">'+d.bed+'/'+d.bed_t+'&deg;C</span></div>';
      if(d.progress>0) h+='<div class="stat-row"><span>Progress:</span><span class="stat-val">'+d.progress+'%</span></div>';
      if(d.fan>0) h+='<div class="stat-row"><span>Fan:</span><span class="stat-val">'+d.fan+'%</span></div>';
    } else if(d.configured) {
      h+='<span style="color:#8B949E">Not connected (printer may be off)</span>';
    } else {
      h+='<span style="color:#8B949E">Not configured</span>';
    }
    document.getElementById('liveStats').innerHTML=h;
    var ps=document.getElementById('printerStatus');
    if(d.connected){ps.className='status status-ok';ps.textContent='Connected';}
    else if(d.configured){ps.className='status status-off';ps.textContent='Disconnected / Printer Off';}
    else{ps.className='status status-na';ps.textContent='Not configured';}
    if(d.display_off && d.connected){ps.textContent+=' (Display Off)';}
  }).catch(function(e){console.warn('liveStats:',e);});
}

// --- Settings export/import ---
function exportSettings(){
  fetch('/settings/export').then(function(r){return r.text();}).then(function(t){
    var a=document.createElement('a');
    a.href='data:application/json;charset=utf-8,'+encodeURIComponent(t);
    a.download='bambuhelper_settings.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
  }).catch(function(){showToast('Export failed');});
}

function importSettings(){
  var f=document.getElementById('importFile').files[0];
  if(!f){showToast('Select a JSON file first');return;}
  if(!confirm('Import settings and restart? Current settings will be overwritten.')) return;
  var fd=new FormData();
  fd.append('settings',f);
  var stat=document.getElementById('importStatus');
  stat.style.color='#58A6FF';stat.textContent='Importing...';
  fetch('/settings/import',{method:'POST',body:fd})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.status==='ok'){
        stat.style.color='#3FB950';stat.textContent=d.message;
      } else {
        stat.style.color='#F85149';stat.textContent='Error: '+d.message;
      }
    })
    .catch(function(){
      stat.style.color='#F85149';stat.textContent='Upload failed';
    });
}

function startOta(){
  var f=document.getElementById('otaFile').files[0];
  if(!f){showToast('Select a .bin file first');return;}
  if(!f.name.endsWith('.bin')){showToast('File must be .bin');return;}
  if(f.size<32768){showToast('File too small');return;}
  if(f.size>1310720){showToast('File too large (max 1.25MB)');return;}
  if(!confirm('Upload firmware and restart?')) return;
  var prog=document.getElementById('otaProgress');
  var bar=document.getElementById('otaBar');
  var pct=document.getElementById('otaPct');
  var stat=document.getElementById('otaStatus');
  prog.style.display='block';
  bar.style.width='0%';
  pct.textContent='0%';
  stat.innerHTML='<span style="color:#58A6FF">Uploading...</span>';
  var fd=new FormData();
  fd.append('firmware',f);
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/ota/upload',true);
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      var p=Math.round(e.loaded/e.total*100);
      bar.style.width=p+'%';
      pct.textContent=p+'%';
      if(p>=100){stat.style.color='#58A6FF';stat.textContent='Flashing...';}
    }
  };
  xhr.onload=function(){
    try{
      var d=JSON.parse(xhr.responseText);
      if(d.status==='ok'){
        bar.style.width='100%';pct.textContent='100%';
        stat.style.color='#3FB950';stat.textContent=d.message;
      } else {
        stat.style.color='#F85149';stat.textContent='Error: '+d.message;
      }
    }catch(e){
      stat.style.color='#F85149';stat.textContent='Unexpected response';
    }
  };
  xhr.onerror=function(){
    stat.style.color='#F85149';stat.textContent='Upload failed (connection lost)';
  };
  xhr.send(fd);
}

)rawliteral"
#ifdef ENABLE_OTA_AUTO
R"rawliteral(
var _autoOtaUrl='';
var _autoOtaProgress=0;
function switchFwTab(t){
  document.getElementById('fw-tab-auto').style.display=t==='auto'?'block':'none';
  document.getElementById('fw-tab-manual').style.display=t==='manual'?'block':'none';
  var a=document.getElementById('tab-auto-btn'),m=document.getElementById('tab-manual-btn');
  if(t==='auto'){a.style.borderColor='#58A6FF';a.style.background='#21262D';a.style.color='#E6EDF3';m.style.borderColor='#30363D';m.style.background='#0D1117';m.style.color='#8B949E';}
  else{m.style.borderColor='#58A6FF';m.style.background='#21262D';m.style.color='#E6EDF3';a.style.borderColor='#30363D';a.style.background='#0D1117';a.style.color='#8B949E';}
}
function checkForUpdates(){
  var res=document.getElementById('updateResult');
  var info=document.getElementById('updateInfo');
  res.style.color='#58A6FF';res.textContent='Checking...';
  info.style.display='none';
  _autoOtaUrl='';
  fetch('https://api.github.com/repos/Keralots/BambuHelper/releases/latest')
    .then(function(r){
      if(!r.ok) throw new Error('GitHub API returned '+r.status);
      return r.json();
    })
    .then(function(d){
      var latest=d.tag_name;
      var current='%FW_VER%';
      // Parse vMAJOR.MINOR[pre] — pre-release suffix (e.g. Beta1) is older than plain release
      function parseVer(v){
        var m=v.replace(/^v/,'').match(/^(\d+)\.(\d+)(.*)/);
        return m?{major:parseInt(m[1]),minor:parseInt(m[2]),pre:m[3]!==''}:null;
      }
      function isNewer(a,b){ // is release 'a' newer than current 'b'?
        var av=parseVer(a),bv=parseVer(b);
        if(!av||!bv)return a!==b;
        if(av.major!==bv.major)return av.major>bv.major;
        if(av.minor!==bv.minor)return av.minor>bv.minor;
        return !av.pre&&bv.pre; // v2.5 > v2.5Beta2
      }
      if(!isNewer(latest,current)){
        res.style.color='#3FB950';
        res.textContent=latest===current?'You are up to date ('+current+')':'Running newer version ('+current+')';
        return;
      }
      // Find the OTA binary for this board variant.
      // Expected filename: BambuHelper-<board>-<version>-ota.bin
      // e.g. BambuHelper-esp32s3-v2.5-ota.bin
      var board='%BOARD%';
      var otaBin=null;
      for(var i=0;i<d.assets.length;i++){
        var n=d.assets[i].name;
        if(n.indexOf(board)!==-1&&n.indexOf('-ota.')!==-1&&n.endsWith('.bin')){otaBin=d.assets[i];break;}
      }
      res.style.color='#F0883E';res.textContent='Update available!';
      document.getElementById('updateVer').textContent=latest;
      var pub=new Date(d.published_at);
      document.getElementById('updateDate').textContent=pub.toLocaleDateString();
      var installBtn=document.getElementById('installBtn');
      var link=document.getElementById('updateLink');
      if(otaBin){
        _autoOtaUrl=otaBin.browser_download_url;
        link.href=otaBin.browser_download_url;
        link.style.display='inline-block';
        installBtn.style.display='inline-block';
      } else {
        installBtn.style.display='none';
        link.style.display='none';
      }
      info.style.display='block';
    })
    .catch(function(e){
      res.style.color='#F85149';res.textContent='Check failed: '+e.message;
      console.warn('updateCheck:',e);
    });
}
function installUpdate(){
  if(!_autoOtaUrl){return;}
  var btn=document.getElementById('installBtn');
  btn.disabled=true;btn.textContent='Installing...';
  document.getElementById('autoOtaWrap').style.display='block';
  document.getElementById('autoOtaBar').style.width='0%';
  document.getElementById('autoOtaBar').style.background='#238636';
  document.getElementById('autoOtaStatus').style.color='#8B949E';
  document.getElementById('autoOtaStatus').textContent='Starting...';
  _autoOtaProgress=0;
  var p=new URLSearchParams();p.append('url',_autoOtaUrl);
  fetch('/ota/auto',{method:'POST',body:p})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.error){throw new Error(d.error);}
      pollOtaStatus();
    })
    .catch(function(e){
      document.getElementById('autoOtaStatus').style.color='#F85149';
      document.getElementById('autoOtaStatus').textContent='Error: '+e.message;
      btn.disabled=false;btn.textContent='Install on BambuHelper';
    });
}
var _otaPoller=null;
function pollOtaStatus(){
  _otaPoller=setInterval(function(){
    fetch('/ota/status').then(function(r){return r.json();}).then(function(d){
      var bar=document.getElementById('autoOtaBar');
      var st=document.getElementById('autoOtaStatus');
      _autoOtaProgress=d.progress||0;
      bar.style.width=d.progress+'%';
      if(d.status==='done'){
        clearInterval(_otaPoller);_otaPoller=null;
        bar.style.width='100%';bar.style.background='#3FB950';
        st.style.color='#3FB950';st.textContent='Done! Restarting device...';
        waitForReboot();
      } else if(d.status&&d.status.indexOf('failed')===0){
        clearInterval(_otaPoller);_otaPoller=null;
        st.style.color='#F85149';st.textContent=d.status;
        var btn=document.getElementById('installBtn');
        btn.disabled=false;btn.textContent='Retry';
      } else {
        st.textContent=d.status+' ('+d.progress+'%)';
      }
    }).catch(function(){
      // Device went offline or /ota/status no longer exists (rebooted to new firmware).
      // If download reached >=90%, treat as success — HTTPUpdate doesn't guarantee a 100% tick.
      if(_autoOtaProgress>=90){
        clearInterval(_otaPoller);_otaPoller=null;
        var bar=document.getElementById('autoOtaBar');
        var st=document.getElementById('autoOtaStatus');
        bar.style.width='100%';bar.style.background='#3FB950';
        st.style.color='#3FB950';st.textContent='Done! Restarting device...';
        waitForReboot();
      }
    });
  },1000);
}
function waitForReboot(){
  var st=document.getElementById('autoOtaStatus');
  st.textContent='Waiting for device to restart...';
  var wentOffline=false,tries=0;
  var check=setInterval(function(){
    fetch('/').then(function(){
      if(wentOffline){
        // Device is back online after going offline — reload to new firmware
        clearInterval(check);
        location.reload();
      }
      // else: device hasn't rebooted yet, keep waiting
    }).catch(function(){
      wentOffline=true;  // device went offline — reboot is in progress
      tries++;
      st.textContent='Restarting... ('+tries+'s)';
      if(tries>60){clearInterval(check);st.textContent='Reboot timeout — please refresh manually.';}
    });
  },2000);
}
)rawliteral"
#endif // ENABLE_OTA_AUTO
R"rawliteral(

// Pong depends on afterprint not being "keepon" (no clock when keeping finish screen on)
function toggleAfterPrint(){
  var v=document.getElementById('afterprint').value;
  document.getElementById('customMinsWrap').style.display=(v==='custom')?'block':'none';
  var pong=document.getElementById('pong');
  var row=document.getElementById('pong-row');
  var showClock=(v!=='keepon');
  pong.disabled=!showClock;
  row.style.opacity=showClock?'1':'0.4';
}
toggleAfterPrint();
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
//  Helper: replace gauge color placeholders
// ---------------------------------------------------------------------------
static void replaceGaugeColors(String& page, const char* prefix, const GaugeColors& gc) {
  char buf[8];
  char placeholder[12];

  snprintf(placeholder, sizeof(placeholder), "%%%s_A%%", prefix);
  rgb565ToHtml(gc.arc, buf);
  page.replace(placeholder, buf);

  snprintf(placeholder, sizeof(placeholder), "%%%s_L%%", prefix);
  rgb565ToHtml(gc.label, buf);
  page.replace(placeholder, buf);

  snprintf(placeholder, sizeof(placeholder), "%%%s_V%%", prefix);
  rgb565ToHtml(gc.value, buf);
  page.replace(placeholder, buf);
}

// ---------------------------------------------------------------------------
//  Template processor
// ---------------------------------------------------------------------------
static void processTemplate(String& page) {
  PrinterConfig& cfg = printers[0].config;
  BambuState& st = printers[0].state;

  page.replace("%SSID%", wifiSSID);
  page.replace("%MODE_LOCAL%", cfg.mode == CONN_LOCAL ? "selected" : "");
  page.replace("%MODE_CLOUD_ALL%", isCloudMode(cfg.mode) ? "selected" : "");
  page.replace("%PNAME%", cfg.name);
  page.replace("%IP%", cfg.ip);
  page.replace("%SERIAL%", cfg.serial);
  // Cloud region dropdown
  page.replace("%REGION_US%", cfg.region == REGION_US ? "selected" : "");
  page.replace("%REGION_EU%", cfg.region == REGION_EU ? "selected" : "");
  page.replace("%REGION_CN%", cfg.region == REGION_CN ? "selected" : "");

  // Cloud status text
  {
    char tokenBuf[32];
    bool hasToken = loadCloudToken(tokenBuf, sizeof(tokenBuf));
    page.replace("%CLOUD_STATUS%", hasToken ? "Token active" : "No token set");
  }
  page.replace("%BRIGHT%", String(brightness));

  // Night mode
  page.replace("%NIGHTEN%", dpSettings.nightModeEnabled ? "checked" : "");
  page.replace("%NIGHTDISP%", dpSettings.nightModeEnabled ? "block" : "none");
  page.replace("%NBRIGHT%", String(dpSettings.nightBrightness));
  page.replace("%SSBRIGHT%", String(dpSettings.screensaverBrightness));
  {
    String startOpts, endOpts;
    for (uint8_t h = 0; h < 24; h++) {
      char opt[64];
      snprintf(opt, sizeof(opt), "<option value=\"%d\"%s>%02d:00</option>",
               h, h == dpSettings.nightStartHour ? " selected" : "", h);
      startOpts += opt;
      snprintf(opt, sizeof(opt), "<option value=\"%d\"%s>%02d:00</option>",
               h, h == dpSettings.nightEndHour ? " selected" : "", h);
      endOpts += opt;
    }
    page.replace("%NIGHT_START_OPTS%", startOpts);
    page.replace("%NIGHT_END_OPTS%", endOpts);
  }

  // Network settings
  page.replace("%NET_DHCP%", netSettings.useDHCP ? "selected" : "");
  page.replace("%NET_STATIC%", netSettings.useDHCP ? "" : "selected");
  page.replace("%NET_IP%", netSettings.staticIP);
  page.replace("%NET_GW%", netSettings.gateway);
  page.replace("%NET_SN%", netSettings.subnet);
  page.replace("%NET_DNS%", netSettings.dns);
  page.replace("%SHOWIP%", netSettings.showIPAtStartup ? "checked" : "");

  // Timezone dropdown (generated from database, sorted by UTC offset)
  {
    size_t tzCount;
    const TimezoneRegion* regions = getSupportedTimezones(&tzCount);
    String tzOpts;
    for (size_t i = 0; i < tzCount; i++) {
      tzOpts += "          <option value=\"";
      tzOpts += String((int)i);
      tzOpts += "\"";
      if (i == netSettings.timezoneIndex) tzOpts += " selected";
      tzOpts += ">";
      tzOpts += regions[i].name;
      tzOpts += "</option>\n";
    }
    page.replace("%TIMEZONE_OPTIONS%", tzOpts);
  }

  page.replace("%USE24H%", netSettings.use24h ? "checked" : "");
  for (int i = 0; i < 6; i++) {
    char ph[12]; snprintf(ph, sizeof(ph), "%%DATEFMT%d%%", i);
    page.replace(ph, netSettings.dateFormat == i ? "selected" : "");
  }

  // Rotation dropdown
  page.replace("%ROT0%", dispSettings.rotation == 0 ? "selected" : "");
  page.replace("%ROT1%", dispSettings.rotation == 1 ? "selected" : "");
  page.replace("%ROT2%", dispSettings.rotation == 2 ? "selected" : "");
  page.replace("%ROT3%", dispSettings.rotation == 3 ? "selected" : "");

  // Display power
  // After-print dropdown: determine which option is selected
  {
    uint16_t fm = dpSettings.finishDisplayMins;
    bool keepon = dpSettings.keepDisplayOn;
    bool isPreset = (!keepon && (fm == 0 || fm == 1 || fm == 3 || fm == 5 || fm == 10));
    page.replace("%AP_CLOCK0%", (!keepon && fm == 0) ? "selected" : "");
    page.replace("%AP_F1%",     (!keepon && fm == 1) ? "selected" : "");
    page.replace("%AP_F3%",     (!keepon && fm == 3) ? "selected" : "");
    page.replace("%AP_F5%",     (!keepon && fm == 5) ? "selected" : "");
    page.replace("%AP_F10%",    (!keepon && fm == 10) ? "selected" : "");
    page.replace("%AP_CUSTOM%", (!keepon && !isPreset && fm > 0) ? "selected" : "");
    page.replace("%AP_KEEPON%", keepon ? "selected" : "");
    page.replace("%CUSTOM_DISP%", (!keepon && !isPreset && fm > 0) ? "block" : "none");
    page.replace("%FMINS%", String(fm));
  }
  page.replace("%DACK%", dpSettings.doorAckEnabled ? "checked" : "");
  page.replace("%ABAR%", dispSettings.animatedBar ? "checked" : "");
  page.replace("%PONG%", dispSettings.pongClock ? "checked" : "");
  page.replace("%SLBL%", dispSettings.smallLabels ? "checked" : "");

  // Global colors
  char buf[8];
  rgb565ToHtml(dispSettings.bgColor, buf);
  page.replace("%CLR_BG%", buf);
  rgb565ToHtml(dispSettings.trackColor, buf);
  page.replace("%CLR_TRACK%", buf);

  // Per-gauge colors
  replaceGaugeColors(page, "PRG", dispSettings.progress);
  replaceGaugeColors(page, "NOZ", dispSettings.nozzle);
  replaceGaugeColors(page, "BED", dispSettings.bed);
  replaceGaugeColors(page, "PFN", dispSettings.partFan);
  replaceGaugeColors(page, "AFN", dispSettings.auxFan);
  replaceGaugeColors(page, "CFN", dispSettings.chamberFan);

  page.replace("%DBGLOG%", mqttDebugLog ? "checked" : "");
  page.replace("%FW_VER%", FW_VERSION);
  page.replace("%BOARD%", BOARD_VARIANT);

  if (st.connected) {
    page.replace("%STATUS_CLASS%", "status status-ok");
    page.replace("%STATUS_TEXT%", "Connected");
  } else if (isPrinterConfigured(0)) {
    page.replace("%STATUS_CLASS%", "status status-off");
    page.replace("%STATUS_TEXT%", "Disconnected");
  } else {
    page.replace("%STATUS_CLASS%", "status status-na");
    page.replace("%STATUS_TEXT%", "Not configured");
  }

  // Rotation mode (multi-printer)
  page.replace("%RMODE_OFF%", rotState.mode == ROTATE_OFF ? "selected" : "");
  page.replace("%RMODE_AUTO%", rotState.mode == ROTATE_AUTO ? "selected" : "");
  page.replace("%RMODE_SMART%", rotState.mode == ROTATE_SMART ? "selected" : "");
  page.replace("%ROT_INTERVAL%", String(rotState.intervalMs / 1000));

  // Button settings
  page.replace("%BTN_OFF%", buttonType == BTN_DISABLED ? "selected" : "");
  page.replace("%BTN_PUSH%", buttonType == BTN_PUSH ? "selected" : "");
  page.replace("%BTN_TOUCH%", buttonType == BTN_TOUCH ? "selected" : "");
  page.replace("%BTN_SCREEN%", buttonType == BTN_TOUCHSCREEN ? "selected" : "");
  page.replace("%BTN_PIN%", String(buttonPin));

  // Buzzer settings
  page.replace("%BUZ_OFF%", buzzerSettings.enabled ? "" : "selected");
  page.replace("%BUZ_ON%", buzzerSettings.enabled ? "selected" : "");
  page.replace("%BUZ_PIN%", String(buzzerSettings.pin));
  page.replace("%BUZ_QS%", String(buzzerSettings.quietStartHour));
  page.replace("%BUZ_QE%", String(buzzerSettings.quietEndHour));

  // Tasmota power monitoring
  page.replace("%TSM_EN%", tasmotaSettings.enabled ? "checked" : "");
  page.replace("%TSM_IP%", tasmotaSettings.ip);
  page.replace("%TSM_DM0%", tasmotaSettings.displayMode == 0 ? "checked" : "");
  page.replace("%TSM_DM1%", tasmotaSettings.displayMode == 1 ? "checked" : "");
  // Tasmota slot dropdown (dynamic, based on configured printers)
  {
    String slotOpts;
    slotOpts += "<option value=\"255\"";
    if (tasmotaSettings.assignedSlot == 255) slotOpts += " selected";
    slotOpts += ">Any printer</option>";
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
      if (!isPrinterConfigured(i)) continue;
      slotOpts += "<option value=\"";
      slotOpts += String(i);
      slotOpts += "\"";
      if (tasmotaSettings.assignedSlot == i) slotOpts += " selected";
      slotOpts += ">";
      const char* nm = printers[i].config.name;
      if (nm[0] != '\0') slotOpts += nm;
      else { slotOpts += "Printer "; slotOpts += String(i + 1); }
      slotOpts += "</option>";
    }
    page.replace("%TSM_SLOT_OPTIONS%", slotOpts);
  }
  // Tasmota poll interval dropdown
  {
    static const uint8_t intervals[] = {10, 15, 20, 30, 60};
    static const char* const labels[] = {"10 seconds", "15 seconds", "20 seconds", "30 seconds", "60 seconds"};
    uint8_t cur = tasmotaSettings.pollInterval > 0 ? tasmotaSettings.pollInterval : 10;
    String piOpts;
    for (int i = 0; i < 5; i++) {
      piOpts += "<option value=\"";
      piOpts += String(intervals[i]);
      piOpts += "\"";
      if (cur == intervals[i]) piOpts += " selected";
      piOpts += ">";
      piOpts += labels[i];
      piOpts += "</option>";
    }
    page.replace("%TSM_PI_OPTIONS%", piOpts);
  }

}

// ---------------------------------------------------------------------------
//  Helper: read gauge colors from form
// ---------------------------------------------------------------------------
static void readGaugeColorsFromForm(const char* prefix, GaugeColors& gc) {
  char key[8];
  snprintf(key, sizeof(key), "%s_a", prefix);
  if (server.hasArg(key)) gc.arc = htmlToRgb565(server.arg(key).c_str());
  snprintf(key, sizeof(key), "%s_l", prefix);
  if (server.hasArg(key)) gc.label = htmlToRgb565(server.arg(key).c_str());
  snprintf(key, sizeof(key), "%s_v", prefix);
  if (server.hasArg(key)) gc.value = htmlToRgb565(server.arg(key).c_str());
}

// ---------------------------------------------------------------------------
//  Read display settings from form args
// ---------------------------------------------------------------------------
static void readDisplayFromForm() {
  if (server.hasArg("bright")) brightness = server.arg("bright").toInt();
  // Night mode
  dpSettings.nightModeEnabled = server.hasArg("nighten");
  if (server.hasArg("nstart")) dpSettings.nightStartHour = server.arg("nstart").toInt();
  if (server.hasArg("nend"))   dpSettings.nightEndHour = server.arg("nend").toInt();
  if (server.hasArg("nbright")) dpSettings.nightBrightness = server.arg("nbright").toInt();
  if (server.hasArg("ssbright")) dpSettings.screensaverBrightness = server.arg("ssbright").toInt();
  // Apply brightness after all brightness-related values are parsed
  setBacklight(getEffectiveBrightness());

  if (server.hasArg("rotation")) {
    uint8_t rot = server.arg("rotation").toInt();
    if (rot <= 3) dispSettings.rotation = rot;
  }
  if (server.hasArg("clr_bg"))    dispSettings.bgColor = htmlToRgb565(server.arg("clr_bg").c_str());
  if (server.hasArg("clr_track")) dispSettings.trackColor = htmlToRgb565(server.arg("clr_track").c_str());

  readGaugeColorsFromForm("prg", dispSettings.progress);
  readGaugeColorsFromForm("noz", dispSettings.nozzle);
  readGaugeColorsFromForm("bed", dispSettings.bed);
  readGaugeColorsFromForm("pfn", dispSettings.partFan);
  readGaugeColorsFromForm("afn", dispSettings.auxFan);
  readGaugeColorsFromForm("cfn", dispSettings.chamberFan);

  if (server.hasArg("fmins")) {
    dpSettings.finishDisplayMins = server.arg("fmins").toInt();
  }
  dpSettings.keepDisplayOn = server.hasArg("keepon");
  dpSettings.showClockAfterFinish = server.hasArg("clock");
  dpSettings.doorAckEnabled = server.hasArg("dack");
  dispSettings.animatedBar = server.hasArg("abar");
  dispSettings.pongClock = server.hasArg("pong");
  dispSettings.smallLabels = server.hasArg("slbl");
  if (server.hasArg("cydextra")) {
    uint8_t mode = server.arg("cydextra").toInt();
    if (mode <= 1) dispSettings.cydExtraMode = mode;
  }

  // Clock settings (timezone, 24h)
  if (server.hasArg("tz")) {
    size_t tzCount;
    const TimezoneRegion* regions = getSupportedTimezones(&tzCount);
    int idx = server.arg("tz").toInt();
    if (idx >= 0 && idx < (int)tzCount) {
      netSettings.timezoneIndex = (uint8_t)idx;
      strlcpy(netSettings.timezoneStr, regions[idx].posixString, sizeof(netSettings.timezoneStr));
    }
  }
  netSettings.use24h = server.hasArg("use24h");
  if (server.hasArg("datefmt")) {
    int df = server.arg("datefmt").toInt();
    if (df >= 0 && df <= 5) netSettings.dateFormat = (uint8_t)df;
  }
}

// ---------------------------------------------------------------------------
//  Route handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
  if (isAPMode()) {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send(200, "text/html", FPSTR(PAGE_AP_HTML));
  } else {
    String page = FPSTR(PAGE_HTML);
    processTemplate(page);
    server.send(200, "text/html", page);
  }
}

// Save printer settings only (no restart — reinit MQTT)
static void handleSavePrinter() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

  PrinterConfig& cfg = printers[slot].config;
  if (server.hasArg("connmode")) {
    String cm = server.arg("connmode");
    if (cm == "cloud_all") cfg.mode = CONN_CLOUD_ALL;
    else cfg.mode = CONN_LOCAL;
  }

  // Cloud region
  if (server.hasArg("region")) {
    String rg = server.arg("region");
    if (rg == "eu") cfg.region = REGION_EU;
    else if (rg == "cn") cfg.region = REGION_CN;
    else cfg.region = REGION_US;
  }

  if (isCloudMode(cfg.mode)) {
    if (server.hasArg("serial")) strlcpy(cfg.serial, server.arg("serial").c_str(), sizeof(cfg.serial));
    if (server.hasArg("pname"))  strlcpy(cfg.name, server.arg("pname").c_str(), sizeof(cfg.name));
    // Save token if provided
    if (server.hasArg("token") && server.arg("token").length() > 0) {
      saveCloudToken(server.arg("token").c_str());
    }
    // Extract userId from stored token
    char tokenBuf[1200];
    if (loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
      if (!cloudExtractUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId))) {
        cloudFetchUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId), cfg.region);
      }
    }
  } else {
    if (server.hasArg("pname"))  strlcpy(cfg.name, server.arg("pname").c_str(), sizeof(cfg.name));
    if (server.hasArg("ip"))     strlcpy(cfg.ip, server.arg("ip").c_str(), sizeof(cfg.ip));
    if (server.hasArg("serial")) strlcpy(cfg.serial, server.arg("serial").c_str(), sizeof(cfg.serial));
    if (server.hasArg("code") && server.arg("code").length() > 0) strlcpy(cfg.accessCode, server.arg("code").c_str(), sizeof(cfg.accessCode));
  }

  // Serial numbers must be uppercase (Bambu MQTT topics are case-sensitive)
  for (char* p = cfg.serial; *p; p++) *p = toupper(*p);

  // Validate required fields and build warnings
  String warnings = "";
  if (isCloudMode(cfg.mode)) {
    if (strlen(cfg.serial) == 0)
      warnings += "Serial number is required for cloud mode. ";
    if (strlen(cfg.cloudUserId) == 0)
      warnings += "Cloud token is missing or invalid (userId extraction failed). ";
  } else {
    if (strlen(cfg.ip) == 0)
      warnings += "Printer IP address is required. ";
    if (strlen(cfg.serial) == 0)
      warnings += "Serial number is required (used for MQTT topic). ";
    if (strlen(cfg.accessCode) == 0)
      warnings += "Access code is required. ";
    else if (strlen(cfg.accessCode) != 8)
      warnings += "Access code should be 8 characters (check printer LCD). ";
  }

  savePrinterConfig(slot);

  // Reinit MQTT — disconnect changed slot, then reinit all
  disconnectBambuMqtt(slot);
  initBambuMqtt();

  if (warnings.length() > 0) {
    String resp = "{\"status\":\"ok\",\"warning\":\"" + warnings + "\"}";
    server.send(200, "application/json", resp);
  } else {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }
}

// Save WiFi + network settings (requires restart)
static void handleSaveWifi() {
  if (server.hasArg("ssid")) strlcpy(wifiSSID, server.arg("ssid").c_str(), sizeof(wifiSSID));
  if (server.hasArg("pass") && server.arg("pass").length() > 0) strlcpy(wifiPass, server.arg("pass").c_str(), sizeof(wifiPass));

  netSettings.useDHCP = (!server.hasArg("netmode") || server.arg("netmode") == "dhcp");
  if (server.hasArg("net_ip"))  strlcpy(netSettings.staticIP, server.arg("net_ip").c_str(), sizeof(netSettings.staticIP));
  if (server.hasArg("net_gw"))  strlcpy(netSettings.gateway, server.arg("net_gw").c_str(), sizeof(netSettings.gateway));
  if (server.hasArg("net_sn"))  strlcpy(netSettings.subnet, server.arg("net_sn").c_str(), sizeof(netSettings.subnet));
  if (server.hasArg("net_dns")) strlcpy(netSettings.dns, server.arg("net_dns").c_str(), sizeof(netSettings.dns));
  if (server.hasArg("has_showip"))  // full page sends this; AP page doesn't
    netSettings.showIPAtStartup = server.hasArg("showip");

  saveSettings();

  server.send(200, "application/json", "{\"status\":\"ok\"}");
  delay(1000);
  ESP.restart();
}

// Live brightness preview (no save, just PWM update)
// Only applies when the main display is active — during clock/screensaver
// the screensaverBrightness governs the backlight, not the main slider.
static void handleBrightnessPreview() {
  if (server.hasArg("val")) {
    uint8_t val = server.arg("val").toInt();
    ScreenState scr = getScreenState();
    if (scr != SCREEN_CLOCK && scr != SCREEN_OFF) {
      setBacklight(val);
    }
  }
  server.send(200, "text/plain", "OK");
}

// Apply display settings live (no restart)
static void handleApply() {
  // Snapshot timezone before parsing — only re-init NTP if it changes.
  // configTzTime() resets the SNTP sync status, which causes getLocalTime()
  // to return false for up to 60s, blanking the clock screen unnecessarily.
  char prevTz[sizeof(netSettings.timezoneStr)];
  strlcpy(prevTz, netSettings.timezoneStr, sizeof(prevTz));
  readDisplayFromForm();
  saveSettings();
  applyDisplaySettings();
  if (strcmp(netSettings.timezoneStr, prevTz) != 0) {
    configTzTime(netSettings.timezoneStr, "pool.ntp.org", "time.nist.gov");
  }
  server.send(200, "text/plain", "OK");
}

static void handleStatus() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

  BambuState& st = printers[slot].state;

  JsonDocument doc;
  doc["connected"] = st.connected;
  doc["configured"] = isPrinterConfigured(slot);
  doc["state"] = st.gcodeState;
  doc["progress"] = st.progress;
  doc["nozzle"] = (int)st.nozzleTemp;
  doc["nozzle_t"] = (int)st.nozzleTarget;
  doc["bed"] = (int)st.bedTemp;
  doc["bed_t"] = (int)st.bedTarget;
  doc["fan"] = st.coolingFanPct;
  doc["layer"] = st.layerNum;
  doc["layers"] = st.totalLayers;
  doc["display_off"] = (getScreenState() == SCREEN_OFF);

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleReset() {
  server.send(200, "text/html",
    "<html><body style='background:#0D1117;color:#E6EDF3;text-align:center;padding-top:80px;font-family:sans-serif'>"
    "<h2 style='color:#F85149'>Factory Reset</h2>"
    "<p>Restarting...</p></body></html>");
  delay(1000);
  resetSettings();
}

static void handleDebug() {
  JsonDocument doc;

  JsonArray arr = doc["printers"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; i++) {
    if (!isPrinterConfigured(i)) continue;
    const MqttDiag& d = getMqttDiag(i);
    BambuState& st = printers[i].state;
    JsonObject p = arr.add<JsonObject>();
    p["slot"] = i;
    p["name"] = printers[i].config.name;
    p["connected"] = st.connected;
    p["attempts"] = d.attempts;
    p["messages"] = d.messagesRx;
    p["last_rc"] = d.lastRc;
    p["rc_text"] = mqttRcToString(d.lastRc);
    p["tcp_ok"] = d.tcpOk;
  }

  doc["heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["debug_log"] = mqttDebugLog;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleDebugToggle() {
  if (server.hasArg("on")) {
    mqttDebugLog = (server.arg("on") == "1");
  }
  server.send(200, "text/plain", mqttDebugLog ? "ON" : "OFF");
}

static void handleToggleSetting() {
  if (!server.hasArg("key") || !server.hasArg("val")) {
    server.send(400, "text/plain", "Missing key/val");
    return;
  }
  String key = server.arg("key");
  bool on = (server.arg("val") == "1");

  if      (key == "keepon")  dpSettings.keepDisplayOn = on;
  else if (key == "clock")   dpSettings.showClockAfterFinish = on;
  else if (key == "dack")    dpSettings.doorAckEnabled = on;
  else if (key == "abar")    dispSettings.animatedBar = on;
  else if (key == "pong")    dispSettings.pongClock = on;
  else if (key == "slbl")    dispSettings.smallLabels = on;
  else if (key == "nighten") dpSettings.nightModeEnabled = on;
  else if (key == "use24h")  netSettings.use24h = on;
  else {
    server.send(400, "text/plain", "Unknown key");
    return;
  }

  saveSettings();
  server.send(200, "text/plain", "OK");
}

static void handleCloudLogout() {
  clearCloudToken();
  server.send(200, "text/plain", "OK");
}

// Get printer config for a specific slot (multi-printer tabs)
static void handlePrinterConfig() {
  uint8_t slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot >= MAX_ACTIVE_PRINTERS) slot = 0;

  PrinterConfig& cfg = printers[slot].config;
  BambuState& st = printers[slot].state;

  JsonDocument doc;
  doc["mode"] = isCloudMode(cfg.mode) ? "cloud_all" : "local";
  doc["name"] = cfg.name;
  doc["ip"] = cfg.ip;
  doc["serial"] = cfg.serial;
  doc["region"] = cfg.region == REGION_EU ? "eu" : (cfg.region == REGION_CN ? "cn" : "us");
  doc["connected"] = st.connected;
  doc["configured"] = isPrinterConfigured(slot);

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Test buzzer from web UI
static void handleBuzzerTest() {
  uint8_t snd = 0;
  if (server.hasArg("sound")) snd = server.arg("sound").toInt();
  if (snd <= 2) buzzerPlay((BuzzerEvent)snd);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Save rotation settings (multi-printer)
static void handleSaveRotation() {
  if (server.hasArg("rotmode")) {
    uint8_t mode = server.arg("rotmode").toInt();
    if (mode <= 2) rotState.mode = (RotateMode)mode;
  }
  if (server.hasArg("rotinterval")) {
    uint32_t sec = server.arg("rotinterval").toInt();
    uint32_t ms = sec * 1000;
    if (ms < ROTATE_MIN_MS) ms = ROTATE_MIN_MS;
    if (ms > ROTATE_MAX_MS) ms = ROTATE_MAX_MS;
    rotState.intervalMs = ms;
  }
  saveRotationSettings();

  // Button settings
  if (server.hasArg("btntype")) {
    uint8_t bt = server.arg("btntype").toInt();
    if (bt <= 3) buttonType = (ButtonType)bt;
  }
  if (server.hasArg("btnpin")) {
    uint8_t bp = server.arg("btnpin").toInt();
    if (bp > 0 && bp <= 48) buttonPin = bp;
  }
  saveButtonSettings();
  initButton();

  // Buzzer settings
  if (server.hasArg("buzzen")) {
    buzzerSettings.enabled = (server.arg("buzzen") == "1");
  }
  if (server.hasArg("buzpin")) {
    uint8_t bp = server.arg("buzpin").toInt();
    if (bp > 0 && bp <= 48) buzzerSettings.pin = bp;
  }
  if (server.hasArg("buzqs")) buzzerSettings.quietStartHour = server.arg("buzqs").toInt();
  if (server.hasArg("buzqe")) buzzerSettings.quietEndHour = server.arg("buzqe").toInt();
  saveBuzzerSettings();
  initBuzzer();

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
//  Save Tasmota power monitoring settings
// ---------------------------------------------------------------------------
static void handleSavePower() {
  tasmotaSettings.enabled = server.hasArg("tsm_en");
  if (server.hasArg("tsm_ip"))
    strlcpy(tasmotaSettings.ip, server.arg("tsm_ip").c_str(), sizeof(tasmotaSettings.ip));
  if (server.hasArg("tsm_dm"))
    tasmotaSettings.displayMode = server.arg("tsm_dm").toInt() ? 1 : 0;
  if (server.hasArg("tsm_slot")) {
    int slot = server.arg("tsm_slot").toInt();
    tasmotaSettings.assignedSlot = (slot >= 0 && slot < MAX_ACTIVE_PRINTERS) ? (uint8_t)slot : 255;
  }
  if (server.hasArg("tsm_pi")) {
    int pi = server.arg("tsm_pi").toInt();
    tasmotaSettings.pollInterval = (pi >= 10 && pi <= 60) ? (uint8_t)pi : 10;
  }
  saveSettings();
  tasmotaInit();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
//  Settings export (JSON download)
// ---------------------------------------------------------------------------
static void gaugeColorsToJson(JsonObject& obj, const GaugeColors& gc) {
  char buf[8];
  rgb565ToHtml(gc.arc, buf);   obj["arc"] = String(buf);
  rgb565ToHtml(gc.label, buf); obj["label"] = String(buf);
  rgb565ToHtml(gc.value, buf); obj["value"] = String(buf);
}

static void handleSettingsExport() {
  JsonDocument doc;
  doc["_type"] = "bambuhelper_settings";
  doc["_version"] = FW_VERSION;

  // WiFi
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = wifiSSID;
  wifi["pass"] = wifiPass;

  // Printers
  JsonArray pArr = doc["printers"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_PRINTERS; i++) {
    PrinterConfig& cfg = printers[i].config;
    JsonObject p = pArr.add<JsonObject>();
    p["mode"] = (uint8_t)cfg.mode;
    p["name"] = cfg.name;
    p["ip"] = cfg.ip;
    p["serial"] = cfg.serial;
    p["accessCode"] = cfg.accessCode;
    p["cloudUserId"] = cfg.cloudUserId;
    p["region"] = (uint8_t)cfg.region;
  }

  // Display
  char buf[8];
  JsonObject disp = doc["display"].to<JsonObject>();
  disp["brightness"] = brightness;
  disp["rotation"] = dispSettings.rotation;
  rgb565ToHtml(dispSettings.bgColor, buf);    disp["bgColor"] = String(buf);
  rgb565ToHtml(dispSettings.trackColor, buf); disp["trackColor"] = String(buf);
  disp["animatedBar"] = dispSettings.animatedBar;
  disp["pongClock"] = dispSettings.pongClock;
  disp["smallLabels"] = dispSettings.smallLabels;

  JsonObject gauges = disp["gauges"].to<JsonObject>();
  JsonObject gPrg = gauges["progress"].to<JsonObject>(); gaugeColorsToJson(gPrg, dispSettings.progress);
  JsonObject gNoz = gauges["nozzle"].to<JsonObject>();   gaugeColorsToJson(gNoz, dispSettings.nozzle);
  JsonObject gBed = gauges["bed"].to<JsonObject>();      gaugeColorsToJson(gBed, dispSettings.bed);
  JsonObject gPfn = gauges["partFan"].to<JsonObject>();  gaugeColorsToJson(gPfn, dispSettings.partFan);
  JsonObject gAfn = gauges["auxFan"].to<JsonObject>();   gaugeColorsToJson(gAfn, dispSettings.auxFan);
  JsonObject gCfn = gauges["chamberFan"].to<JsonObject>(); gaugeColorsToJson(gCfn, dispSettings.chamberFan);

  // Display power
  JsonObject dp = doc["displayPower"].to<JsonObject>();
  dp["finishDisplayMins"] = dpSettings.finishDisplayMins;
  dp["keepDisplayOn"] = dpSettings.keepDisplayOn;
  dp["showClockAfterFinish"] = dpSettings.showClockAfterFinish;
  dp["doorAckEnabled"] = dpSettings.doorAckEnabled;
  dp["nightModeEnabled"] = dpSettings.nightModeEnabled;
  dp["nightStartHour"] = dpSettings.nightStartHour;
  dp["nightEndHour"] = dpSettings.nightEndHour;
  dp["nightBrightness"] = dpSettings.nightBrightness;
  dp["screensaverBrightness"] = dpSettings.screensaverBrightness;

  // Network
  JsonObject net = doc["network"].to<JsonObject>();
  net["useDHCP"] = netSettings.useDHCP;
  net["staticIP"] = netSettings.staticIP;
  net["gateway"] = netSettings.gateway;
  net["subnet"] = netSettings.subnet;
  net["dns"] = netSettings.dns;
  net["timezoneIndex"] = netSettings.timezoneIndex;
  net["timezoneStr"] = netSettings.timezoneStr;
  net["use24h"] = netSettings.use24h;
  net["dateFormat"] = netSettings.dateFormat;

  // Rotation
  JsonObject rot = doc["rotation"].to<JsonObject>();
  rot["mode"] = (uint8_t)rotState.mode;
  rot["intervalMs"] = rotState.intervalMs;

  // Button
  JsonObject btn = doc["button"].to<JsonObject>();
  btn["type"] = (uint8_t)buttonType;
  btn["pin"] = buttonPin;

  // Buzzer
  JsonObject buz = doc["buzzer"].to<JsonObject>();
  buz["enabled"] = buzzerSettings.enabled;
  buz["pin"] = buzzerSettings.pin;
  buz["quietStart"] = buzzerSettings.quietStartHour;
  buz["quietEnd"] = buzzerSettings.quietEndHour;

  String json;
  serializeJsonPretty(doc, json);

  server.sendHeader("Content-Disposition", "attachment; filename=\"bambuhelper_settings.json\"");
  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
//  Settings import (JSON upload)
// ---------------------------------------------------------------------------
static String settingsImportBuf;
static bool   otaInProgress  = false;
static bool   otaFirstChunk  = false;
static String otaError       = "";

// Auto-update (device-initiated, HTTPUpdate from GitHub releases)
#ifdef ENABLE_OTA_AUTO
static volatile bool otaAutoInProgress = false;
static volatile int  otaAutoProgress   = 0;
static String        otaAutoStatus     = "";
#endif

static void gaugeColorsFromJson(JsonObject obj, GaugeColors& gc) {
  if (obj["arc"].is<const char*>())   gc.arc   = htmlToRgb565(obj["arc"]);
  if (obj["label"].is<const char*>()) gc.label = htmlToRgb565(obj["label"]);
  if (obj["value"].is<const char*>()) gc.value = htmlToRgb565(obj["value"]);
}

static void handleSettingsImportUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    settingsImportBuf = "";
    settingsImportBuf.reserve(4096);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (settingsImportBuf.length() + upload.currentSize > 8192) return;
    for (size_t i = 0; i < upload.currentSize; i++)
      settingsImportBuf += (char)upload.buf[i];
  }
}

static void handleSettingsImportFinish() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, settingsImportBuf);
  settingsImportBuf = "";  // free memory

  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  if (!doc["_type"].is<const char*>() || strcmp(doc["_type"], "bambuhelper_settings") != 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Not a BambuHelper settings file\"}");
    return;
  }

  // WiFi
  JsonObject wifi = doc["wifi"];
  if (wifi) {
    if (wifi["ssid"].is<const char*>()) strlcpy(wifiSSID, wifi["ssid"], sizeof(wifiSSID));
    if (wifi["pass"].is<const char*>()) strlcpy(wifiPass, wifi["pass"], sizeof(wifiPass));
  }

  // Printers
  JsonArray pArr = doc["printers"];
  if (pArr) {
    for (uint8_t i = 0; i < MAX_PRINTERS && i < pArr.size(); i++) {
      JsonObject p = pArr[i];
      PrinterConfig& cfg = printers[i].config;
      if (p["mode"].is<uint8_t>())            cfg.mode = (ConnMode)p["mode"].as<uint8_t>();
      if (p["name"].is<const char*>())        strlcpy(cfg.name, p["name"], sizeof(cfg.name));
      if (p["ip"].is<const char*>())          strlcpy(cfg.ip, p["ip"], sizeof(cfg.ip));
      if (p["serial"].is<const char*>())      strlcpy(cfg.serial, p["serial"], sizeof(cfg.serial));
      if (p["accessCode"].is<const char*>())  strlcpy(cfg.accessCode, p["accessCode"], sizeof(cfg.accessCode));
      if (p["cloudUserId"].is<const char*>()) strlcpy(cfg.cloudUserId, p["cloudUserId"], sizeof(cfg.cloudUserId));
      if (p["region"].is<uint8_t>())          cfg.region = (CloudRegion)p["region"].as<uint8_t>();
    }
  }

  // Display
  JsonObject disp = doc["display"];
  if (disp) {
    if (disp["brightness"].is<uint8_t>()) brightness = disp["brightness"].as<uint8_t>();
    if (disp["rotation"].is<uint8_t>())   dispSettings.rotation = disp["rotation"].as<uint8_t>();
    if (disp["bgColor"].is<const char*>())    dispSettings.bgColor = htmlToRgb565(disp["bgColor"]);
    if (disp["trackColor"].is<const char*>()) dispSettings.trackColor = htmlToRgb565(disp["trackColor"]);
    if (disp["animatedBar"].is<bool>())       dispSettings.animatedBar = disp["animatedBar"].as<bool>();
    if (disp["pongClock"].is<bool>())         dispSettings.pongClock = disp["pongClock"].as<bool>();
    if (disp["smallLabels"].is<bool>())      dispSettings.smallLabels = disp["smallLabels"].as<bool>();

    JsonObject gauges = disp["gauges"];
    if (gauges) {
      if (gauges["progress"].is<JsonObject>()) { JsonObject g = gauges["progress"]; gaugeColorsFromJson(g, dispSettings.progress); }
      if (gauges["nozzle"].is<JsonObject>())   { JsonObject g = gauges["nozzle"];   gaugeColorsFromJson(g, dispSettings.nozzle); }
      if (gauges["bed"].is<JsonObject>())      { JsonObject g = gauges["bed"];      gaugeColorsFromJson(g, dispSettings.bed); }
      if (gauges["partFan"].is<JsonObject>())  { JsonObject g = gauges["partFan"];  gaugeColorsFromJson(g, dispSettings.partFan); }
      if (gauges["auxFan"].is<JsonObject>())   { JsonObject g = gauges["auxFan"];   gaugeColorsFromJson(g, dispSettings.auxFan); }
      if (gauges["chamberFan"].is<JsonObject>()){ JsonObject g = gauges["chamberFan"]; gaugeColorsFromJson(g, dispSettings.chamberFan); }
    }
  }

  // Display power
  JsonObject dp = doc["displayPower"];
  if (dp) {
    if (dp["finishDisplayMins"].is<uint16_t>()) dpSettings.finishDisplayMins = dp["finishDisplayMins"].as<uint16_t>();
    if (dp["keepDisplayOn"].is<bool>())         dpSettings.keepDisplayOn = dp["keepDisplayOn"].as<bool>();
    if (dp["showClockAfterFinish"].is<bool>())  dpSettings.showClockAfterFinish = dp["showClockAfterFinish"].as<bool>();
    if (dp["doorAckEnabled"].is<bool>())        dpSettings.doorAckEnabled = dp["doorAckEnabled"].as<bool>();
    if (dp["nightModeEnabled"].is<bool>())      dpSettings.nightModeEnabled = dp["nightModeEnabled"].as<bool>();
    if (dp["nightStartHour"].is<uint8_t>())     dpSettings.nightStartHour = dp["nightStartHour"].as<uint8_t>();
    if (dp["nightEndHour"].is<uint8_t>())       dpSettings.nightEndHour = dp["nightEndHour"].as<uint8_t>();
    if (dp["nightBrightness"].is<uint8_t>())    dpSettings.nightBrightness = dp["nightBrightness"].as<uint8_t>();
    if (dp["screensaverBrightness"].is<uint8_t>()) dpSettings.screensaverBrightness = dp["screensaverBrightness"].as<uint8_t>();
  }

  // Network
  JsonObject net = doc["network"];
  if (net) {
    if (net["useDHCP"].is<bool>())            netSettings.useDHCP = net["useDHCP"].as<bool>();
    if (net["staticIP"].is<const char*>())    strlcpy(netSettings.staticIP, net["staticIP"], sizeof(netSettings.staticIP));
    if (net["gateway"].is<const char*>())     strlcpy(netSettings.gateway, net["gateway"], sizeof(netSettings.gateway));
    if (net["subnet"].is<const char*>())      strlcpy(netSettings.subnet, net["subnet"], sizeof(netSettings.subnet));
    if (net["dns"].is<const char*>())         strlcpy(netSettings.dns, net["dns"], sizeof(netSettings.dns));
    if (net["timezoneStr"].is<const char*>()) {
      strlcpy(netSettings.timezoneStr, net["timezoneStr"], sizeof(netSettings.timezoneStr));
      netSettings.timezoneIndex = net["timezoneIndex"] | (uint8_t)3;
    } else if (net["gmtOffsetMin"].is<int16_t>()) {
      // Backward compat: import from old format
      int16_t oldOffset = net["gmtOffsetMin"].as<int16_t>();
      const char* migrated = getDefaultTimezoneForOffset(oldOffset);
      if (migrated) strlcpy(netSettings.timezoneStr, migrated, sizeof(netSettings.timezoneStr));
    }
    if (net["use24h"].is<bool>())             netSettings.use24h = net["use24h"].as<bool>();
    if (net["dateFormat"].is<uint8_t>())     netSettings.dateFormat = net["dateFormat"].as<uint8_t>();
  }

  // Rotation
  JsonObject rot = doc["rotation"];
  if (rot) {
    if (rot["mode"].is<uint8_t>())      rotState.mode = (RotateMode)rot["mode"].as<uint8_t>();
    if (rot["intervalMs"].is<uint32_t>()) rotState.intervalMs = rot["intervalMs"].as<uint32_t>();
  }

  // Button
  JsonObject btn = doc["button"];
  if (btn) {
    if (btn["type"].is<uint8_t>()) buttonType = (ButtonType)btn["type"].as<uint8_t>();
    if (btn["pin"].is<uint8_t>())  buttonPin = btn["pin"].as<uint8_t>();
  }

  // Buzzer
  JsonObject buz = doc["buzzer"];
  if (buz) {
    if (buz["enabled"].is<bool>())    buzzerSettings.enabled = buz["enabled"].as<bool>();
    if (buz["pin"].is<uint8_t>())     buzzerSettings.pin = buz["pin"].as<uint8_t>();
    if (buz["quietStart"].is<uint8_t>()) buzzerSettings.quietStartHour = buz["quietStart"].as<uint8_t>();
    if (buz["quietEnd"].is<uint8_t>())   buzzerSettings.quietEndHour = buz["quietEnd"].as<uint8_t>();
  }

  // Save everything to NVS
  saveSettings();
  saveRotationSettings();
  saveButtonSettings();
  saveBuzzerSettings();

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Settings imported. Restarting...\"}");
  delay(1000);
  ESP.restart();
}

// ---------------------------------------------------------------------------
//  OTA firmware update
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  Auto-update: FreeRTOS task that runs HTTPUpdate from a GitHub release URL
// ---------------------------------------------------------------------------
#ifdef ENABLE_OTA_AUTO
static void otaAutoTaskFn(void* param) {
  String* urlPtr = (String*)param;
  String url = *urlPtr;
  delete urlPtr;

  otaAutoStatus = "downloading";

  WiFiClientSecure client;
  client.setCACertBundle(rootca_crt_bundle_start);

  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) otaAutoProgress = (int)((cur / (float)total) * 100);
  });

  t_httpUpdate_return ret = httpUpdate.update(client, url);

  switch (ret) {
    case HTTP_UPDATE_OK:
      otaAutoProgress = 100;
      otaAutoStatus = "done";
      Serial.println("OTA auto: success, restarting in 4s");
      delay(4000);   // long enough for JS poller to detect "done" before reboot
      ESP.restart();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      otaAutoStatus = "already_current";
      break;
    case HTTP_UPDATE_FAILED:
    default: {
      String err = httpUpdate.getLastErrorString();
      Serial.printf("OTA auto: failed (%d) %s\n", httpUpdate.getLastError(), err.c_str());
      // Retry once with setInsecure() in case CA bundle fails
      if (httpUpdate.getLastError() != -107) {  // -107 = firmware too large, don't retry
        client.setInsecure();
        ret = httpUpdate.update(client, url);
        if (ret == HTTP_UPDATE_OK) {
          otaAutoProgress = 100;
          otaAutoStatus = "done";
          delay(4000);
          ESP.restart();
          break;
        }
      }
      otaAutoStatus = "failed: " + err;
      break;
    }
  }

  otaAutoInProgress = false;
  vTaskDelete(nullptr);
}

static void handleOtaAuto() {
  if (otaAutoInProgress) {
    server.send(409, "application/json", "{\"error\":\"Update already in progress\"}");
    return;
  }

  String url = server.arg("url");
  if (url.length() == 0 ||
      (!url.startsWith("https://github.com/") &&
       !url.startsWith("https://objects.githubusercontent.com/"))) {
    server.send(400, "application/json", "{\"error\":\"Missing or invalid url\"}");
    return;
  }

  disconnectBambuMqtt();

  otaAutoInProgress = true;
  otaAutoProgress   = 0;
  otaAutoStatus     = "starting";

  String* urlHeap = new String(url);
  xTaskCreate(otaAutoTaskFn, "otaAuto", 8192, (void*)urlHeap, 5, nullptr);

  server.send(200, "application/json", "{\"status\":\"started\"}");
}

bool        isOtaAutoInProgress() { return otaAutoInProgress; }
int         getOtaAutoProgress()  { return otaAutoProgress; }
const char* getOtaAutoStatus()    { return otaAutoStatus.c_str(); }

static void handleOtaStatus() {
  JsonDocument doc;
  doc["inProgress"] = (bool)otaAutoInProgress;
  doc["progress"]   = (int)otaAutoProgress;
  doc["status"]     = otaAutoStatus;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}
#endif // ENABLE_OTA_AUTO

static void handleOtaUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaError = "";
    otaInProgress = true;
    otaFirstChunk = true;
    Serial.printf("OTA: start, file=%s\n", upload.filename.c_str());

    disconnectBambuMqtt();

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaError = Update.errorString();
      Serial.printf("OTA: begin failed: %s\n", otaError.c_str());
      otaInProgress = false;
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaInProgress) return;

    // Validate ESP32 magic byte on first chunk
    if (otaFirstChunk && upload.currentSize > 0) {
      otaFirstChunk = false;
      if (upload.buf[0] != 0xE9) {
        otaError = "Invalid firmware file";
        Update.abort();
        otaInProgress = false;
        return;
      }
    }

    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaError = Update.errorString();
      Update.abort();
      otaInProgress = false;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaInProgress) return;

    if (Update.end(true)) {
      Serial.printf("OTA: success, %u bytes\n", upload.totalSize);
    } else {
      otaError = Update.errorString();
      Serial.printf("OTA: end failed: %s\n", otaError.c_str());
    }
    otaInProgress = false;

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaInProgress = false;
    Serial.println("OTA: aborted");
  }
}

static void handleOtaFinish() {
  if (otaError.length() > 0) {
    String msg = "{\"status\":\"error\",\"message\":\"" + otaError + "\"}";
    server.send(400, "application/json", msg);
    otaError = "";
    return;
  }
  server.send(200, "application/json",
    "{\"status\":\"ok\",\"message\":\"Update successful. Restarting...\"}");
  delay(1500);
  ESP.restart();
}

// Captive portal: redirect any unknown request to root
// Android/Samsung check /generate_204 expecting 204 — returning 302 triggers popup.
// Apple checks /hotspot-detect.html — non-"Success" body triggers popup.
// Using 302 + no-cache for all unknown paths ensures popup on all platforms.
static void handleNotFound() {
  if (isAPMode()) {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not Found");
  }
}

// ---------------------------------------------------------------------------
//  Init & handle
// ---------------------------------------------------------------------------
static void handleCaptiveDetect() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302, "text/plain", "");
}

void initWebServer() {
  // Captive portal detection endpoints (must be before onNotFound)
  server.on("/generate_204", HTTP_GET, handleCaptiveDetect);        // Android/Samsung
  server.on("/gen_204", HTTP_GET, handleCaptiveDetect);              // Android alt
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveDetect);      // Windows
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveDetect);  // Apple
  server.on("/canonical.html", HTTP_GET, handleCaptiveDetect);       // Firefox

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save/wifi", HTTP_POST, handleSaveWifi);
  server.on("/save/printer", HTTP_POST, handleSavePrinter);
  server.on("/save/rotation", HTTP_POST, handleSaveRotation);
  server.on("/save/power", HTTP_POST, handleSavePower);
  server.on("/buzzer/test", HTTP_POST, handleBuzzerTest);
  server.on("/printer/config", HTTP_GET, handlePrinterConfig);
  server.on("/apply", HTTP_POST, handleApply);
  server.on("/brightness", HTTP_GET, handleBrightnessPreview);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/debug/toggle", HTTP_POST, handleDebugToggle);
  server.on("/save/toggle", HTTP_POST, handleToggleSetting);
  server.on("/cloud/logout", HTTP_POST, handleCloudLogout);
  server.on("/settings/export", HTTP_GET, handleSettingsExport);
  server.on("/settings/import", HTTP_POST, handleSettingsImportFinish, handleSettingsImportUpload);
  server.on("/ota/upload", HTTP_POST, handleOtaFinish, handleOtaUpload);
#ifdef ENABLE_OTA_AUTO
  server.on("/ota/auto",   HTTP_POST, handleOtaAuto);
  server.on("/ota/status", HTTP_GET,  handleOtaStatus);
#endif
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started on port 80");
}

void handleWebServer() {
  server.handleClient();
}
