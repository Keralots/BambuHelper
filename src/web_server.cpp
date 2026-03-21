#include "web_server.h"
#include "settings.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "wifi_manager.h"
#include "display_ui.h"
#include "config.h"
#include "button.h"
#include <WebServer.h>
#include <ArduinoJson.h>

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
    .catch(function(){document.getElementById('msg').innerHTML='<span style="color:#F85149">Error</span>';});
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
        <option value="local" %MODE_LOCAL%>LAN Direct (P1/X1/A1)</option>
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
        <input type="text" id="code" value="%CODE%" placeholder="12345678" maxlength="8">
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
      <label for="bright">Brightness: <span id="brightVal">%BRIGHT%</span></label>
      <input type="range" id="bright" min="10" max="255" value="%BRIGHT%"
             oninput="document.getElementById('brightVal').textContent=this.value">

      <label for="rotation">Screen Rotation</label>
      <select id="rotation">
        <option value="0" %ROT0%>0&deg; (default)</option>
        <option value="1" %ROT1%>90&deg;</option>
        <option value="2" %ROT2%>180&deg;</option>
        <option value="3" %ROT3%>270&deg;</option>
      </select>

      <label for="fmins">Display off after print complete (minutes, 0 = never)</label>
      <input type="number" id="fmins" min="0" max="999" value="%FMINS%">
      <div class="check-row">
        <input type="checkbox" id="keepon" value="1" %KEEPON%>
        <label for="keepon">Keep display always on (override timeout)</label>
      </div>
      <div class="check-row">
        <input type="checkbox" id="clock" value="1" %CLOCK%>
        <label for="clock">Show clock after print (instead of screen off)</label>
      </div>
      <div style="font-size:11px;color:#8B949E;margin-top:4px">
        Note: Without a physical button, display will show clock instead of turning off (no way to wake manually).
      </div>
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

<!-- ===== Section 3: Diagnostics ===== -->
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

<!-- ===== Section 4: Multi-Printer ===== -->
<div class="section" id="s-rotate">
  <div class="section-header" onclick="toggleSection('rotate')">
    <h2>Multi-Printer</h2>
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
        </select>
        <div id="btnPinRow">
          <label for="btnpin">Button GPIO Pin</label>
          <input type="number" id="btnpin" min="1" max="48" value="%BTN_PIN%">
          <p style="font-size:11px;color:#8B949E;margin-top:4px">Button switches between printers. Wakes display from sleep.</p>
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
      <input type="password" id="pass" value="%PASS%" placeholder="WiFi password">

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
        <input type="checkbox" id="showip" value="1" %SHOWIP%>
        <label for="showip">Show IP at startup (3s)</label>
      </div>
      <label for="tz">Timezone</label>
      <select id="tz">
        <option value="-720" %TZ_N720%>UTC-12:00</option>
        <option value="-660" %TZ_N660%>UTC-11:00</option>
        <option value="-600" %TZ_N600%>UTC-10:00 (Hawaii)</option>
        <option value="-540" %TZ_N540%>UTC-9:00 (Alaska)</option>
        <option value="-480" %TZ_N480%>UTC-8:00 (Pacific)</option>
        <option value="-420" %TZ_N420%>UTC-7:00 (Mountain)</option>
        <option value="-360" %TZ_N360%>UTC-6:00 (Central)</option>
        <option value="-300" %TZ_N300%>UTC-5:00 (Eastern)</option>
        <option value="-240" %TZ_N240%>UTC-4:00 (Atlantic)</option>
        <option value="-210" %TZ_N210%>UTC-3:30 (Newfoundland)</option>
        <option value="-180" %TZ_N180%>UTC-3:00 (Brazil)</option>
        <option value="-120" %TZ_N120%>UTC-2:00</option>
        <option value="-60" %TZ_N60%>UTC-1:00 (Azores)</option>
        <option value="0" %TZ_0%>UTC+0:00 (London)</option>
        <option value="60" %TZ_60%>UTC+1:00 (Berlin/Paris)</option>
        <option value="120" %TZ_120%>UTC+2:00 (Helsinki/Cairo)</option>
        <option value="180" %TZ_180%>UTC+3:00 (Moscow)</option>
        <option value="210" %TZ_210%>UTC+3:30 (Tehran)</option>
        <option value="240" %TZ_240%>UTC+4:00 (Dubai)</option>
        <option value="270" %TZ_270%>UTC+4:30 (Kabul)</option>
        <option value="300" %TZ_300%>UTC+5:00 (Karachi)</option>
        <option value="330" %TZ_330%>UTC+5:30 (India)</option>
        <option value="345" %TZ_345%>UTC+5:45 (Nepal)</option>
        <option value="360" %TZ_360%>UTC+6:00 (Dhaka)</option>
        <option value="420" %TZ_420%>UTC+7:00 (Bangkok)</option>
        <option value="480" %TZ_480%>UTC+8:00 (Singapore)</option>
        <option value="540" %TZ_540%>UTC+9:00 (Tokyo)</option>
        <option value="570" %TZ_570%>UTC+9:30 (Adelaide)</option>
        <option value="600" %TZ_600%>UTC+10:00 (Sydney)</option>
        <option value="660" %TZ_660%>UTC+11:00</option>
        <option value="720" %TZ_720%>UTC+12:00 (Auckland)</option>
      </select>
      <div class="check-row">
        <input type="checkbox" id="dst" value="1" %DST%>
        <label for="dst">Daylight Saving Time (+1h)</label>
      </div>
      <div class="check-row">
        <input type="checkbox" id="use24h" value="1" %USE24H%>
        <label for="use24h">24-hour time format</label>
      </div>

      <button type="button" class="btn btn-primary" onclick="saveWifi()">Save WiFi &amp; Restart</button>

      <div style="margin-top:20px;padding-top:12px;border-top:1px solid #30363D">
        <button type="button" class="btn btn-danger" onclick="if(confirm('Reset all settings to factory defaults?'))location='/reset'">Factory Reset</button>
      </div>
    </div>
  </div>
</div>

<script>
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
  if(!isOpen){
    content.classList.add('open');
    arrow.classList.add('open');
    sect.classList.add('open');
    localStorage.setItem('bambu_section',id);
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
  fetch('/printer/config?slot='+slot).then(function(r){return r.json();}).then(function(d){
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
  }).catch(function(){});
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
    .catch(function(){showToast('Network error');});
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
  if(document.getElementById('showip').checked) p.append('showip','1');
  p.append('tz',document.getElementById('tz').value);
  if(document.getElementById('dst').checked) p.append('dst','1');
  if(document.getElementById('use24h').checked) p.append('use24h','1');
  fetch('/save/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(){
      document.body.innerHTML='<div style="text-align:center;padding-top:80px"><h2 style="color:#3FB950">WiFi Saved!</h2><p style="color:#8B949E;margin-top:10px">Restarting...</p></div>';
    })
    .catch(function(){showToast('Error');});
}

// --- Cloud ---
function cloudLogout(){
  fetch('/cloud/logout',{method:'POST'}).then(function(){
    document.getElementById('cloudStatus').innerHTML='<span style="color:#8B949E">No token set</span>';
    document.getElementById('cl_token').value='';
  });
}

// --- Multi-Printer rotation & button ---
function toggleBtnPin(){
  document.getElementById('btnPinRow').style.display=
    document.getElementById('btntype').value==='0'?'none':'block';
}
toggleBtnPin();

function saveRotation(){
  var p=new URLSearchParams();
  p.append('rotmode',document.getElementById('rotmode').value);
  p.append('rotinterval',document.getElementById('rotinterval').value);
  p.append('btntype',document.getElementById('btntype').value);
  p.append('btnpin',document.getElementById('btnpin').value);
  fetch('/save/rotation',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok') showToast('Settings saved');})
    .catch(function(){showToast('Error');});
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
  p.append('rotation',document.getElementById('rotation').value);
  p.append('fmins',document.getElementById('fmins').value);
  if(document.getElementById('keepon').checked) p.append('keepon','1');
  if(document.getElementById('clock').checked) p.append('clock','1');
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
  }).catch(function(){showToast('Error');});
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
        h+='<b style="color:#E6EDF3">'+p.name+'</b> (slot '+p.slot+')<br>';
        h+='<div class="stat-row"><span>MQTT:</span><span class="stat-val">'+(p.connected?'<span style="color:#3FB950">Connected</span>':'<span style="color:#F85149">Disconnected</span>')+'</span></div>';
        h+='<div class="stat-row"><span>Attempts:</span><span class="stat-val">'+p.attempts+'</span></div>';
        h+='<div class="stat-row"><span>Messages RX:</span><span class="stat-val">'+p.messages+'</span></div>';
        if(p.last_rc!==0) h+='<div class="stat-row"><span>Last error:</span><span class="stat-val" style="color:#F85149">'+p.rc_text+'</span></div>';
        h+='</div>';
      });
    }
    h+='<div class="stat-row"><span>Free heap:</span><span class="stat-val">'+Math.round(d.heap/1024)+'KB</span></div>';
    h+='<div class="stat-row"><span>Uptime:</span><span class="stat-val">'+Math.round(d.uptime/60)+'min</span></div>';
    document.getElementById('diagInfo').innerHTML=h;
  }).catch(function(){});
}
refreshDiag();
setInterval(refreshDiag,5000);

// --- Live stats (shows currently selected tab's printer) ---
setInterval(function(){
  fetch('/status?slot='+currentSlot).then(r=>r.json()).then(d=>{
    var h='';
    if(d.display_off) h+='<div class="stat-row"><span>Display:</span><span class="stat-val" style="color:#F85149">Off</span></div>';
    if(d.connected){
      h+='<div class="stat-row"><span>State:</span><span class="stat-val">'+d.state+'</span></div>';
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
  }).catch(function(){});
}, 3000);
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
static String processTemplate(const String& html) {
  PrinterConfig& cfg = printers[0].config;
  BambuState& st = printers[0].state;

  String page = html;
  page.replace("%SSID%", wifiSSID);
  page.replace("%PASS%", wifiPass);
  page.replace("%MODE_LOCAL%", cfg.mode == CONN_LOCAL ? "selected" : "");
  page.replace("%MODE_CLOUD_ALL%", isCloudMode(cfg.mode) ? "selected" : "");
  page.replace("%PNAME%", cfg.name);
  page.replace("%IP%", cfg.ip);
  page.replace("%SERIAL%", cfg.serial);
  page.replace("%CODE%", cfg.accessCode);
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

  // Network settings
  page.replace("%NET_DHCP%", netSettings.useDHCP ? "selected" : "");
  page.replace("%NET_STATIC%", netSettings.useDHCP ? "" : "selected");
  page.replace("%NET_IP%", netSettings.staticIP);
  page.replace("%NET_GW%", netSettings.gateway);
  page.replace("%NET_SN%", netSettings.subnet);
  page.replace("%NET_DNS%", netSettings.dns);
  page.replace("%SHOWIP%", netSettings.showIPAtStartup ? "checked" : "");

  // Timezone dropdown
  {
    const int16_t tzVals[] = {-720,-660,-600,-540,-480,-420,-360,-300,-240,-210,-180,-120,-60,
                              0,60,120,180,210,240,270,300,330,345,360,420,480,540,570,600,660,720};
    for (int i = 0; i < (int)(sizeof(tzVals)/sizeof(tzVals[0])); i++) {
      char ph[16];
      if (tzVals[i] < 0)
        snprintf(ph, sizeof(ph), "%%TZ_N%d%%", -tzVals[i]);
      else
        snprintf(ph, sizeof(ph), "%%TZ_%d%%", tzVals[i]);
      page.replace(ph, netSettings.gmtOffsetMin == tzVals[i] ? "selected" : "");
    }
  }

  page.replace("%DST%", netSettings.dstEnabled ? "checked" : "");
  page.replace("%USE24H%", netSettings.use24h ? "checked" : "");

  // Rotation dropdown
  page.replace("%ROT0%", dispSettings.rotation == 0 ? "selected" : "");
  page.replace("%ROT1%", dispSettings.rotation == 1 ? "selected" : "");
  page.replace("%ROT2%", dispSettings.rotation == 2 ? "selected" : "");
  page.replace("%ROT3%", dispSettings.rotation == 3 ? "selected" : "");

  // Display power
  page.replace("%FMINS%", String(dpSettings.finishDisplayMins));
  page.replace("%KEEPON%", dpSettings.keepDisplayOn ? "checked" : "");
  page.replace("%CLOCK%", dpSettings.showClockAfterFinish ? "checked" : "");

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
  page.replace("%BTN_PIN%", String(buttonPin));

  return page;
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
  if (server.hasArg("bright")) {
    brightness = server.arg("bright").toInt();
    setBacklight(brightness);
  }
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
}

// ---------------------------------------------------------------------------
//  Route handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
  if (isAPMode()) {
    server.send(200, "text/html", FPSTR(PAGE_AP_HTML));
  } else {
    String html = FPSTR(PAGE_HTML);
    server.send(200, "text/html", processTemplate(html));
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
    if (server.hasArg("code"))   strlcpy(cfg.accessCode, server.arg("code").c_str(), sizeof(cfg.accessCode));
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
  if (server.hasArg("pass")) strlcpy(wifiPass, server.arg("pass").c_str(), sizeof(wifiPass));

  netSettings.useDHCP = (!server.hasArg("netmode") || server.arg("netmode") == "dhcp");
  if (server.hasArg("net_ip"))  strlcpy(netSettings.staticIP, server.arg("net_ip").c_str(), sizeof(netSettings.staticIP));
  if (server.hasArg("net_gw"))  strlcpy(netSettings.gateway, server.arg("net_gw").c_str(), sizeof(netSettings.gateway));
  if (server.hasArg("net_sn"))  strlcpy(netSettings.subnet, server.arg("net_sn").c_str(), sizeof(netSettings.subnet));
  if (server.hasArg("net_dns")) strlcpy(netSettings.dns, server.arg("net_dns").c_str(), sizeof(netSettings.dns));
  netSettings.showIPAtStartup = server.hasArg("showip");
  if (server.hasArg("tz")) netSettings.gmtOffsetMin = server.arg("tz").toInt();
  netSettings.dstEnabled = server.hasArg("dst");
  netSettings.use24h = server.hasArg("use24h");

  saveSettings();

  server.send(200, "application/json", "{\"status\":\"ok\"}");
  delay(1000);
  ESP.restart();
}

// Apply display settings live (no restart)
static void handleApply() {
  readDisplayFromForm();
  saveSettings();
  applyDisplaySettings();
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
  doc["code"] = cfg.accessCode;
  doc["region"] = cfg.region == REGION_EU ? "eu" : (cfg.region == REGION_CN ? "cn" : "us");
  doc["connected"] = st.connected;
  doc["configured"] = isPrinterConfigured(slot);

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
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
    if (bt <= 2) buttonType = (ButtonType)bt;
  }
  if (server.hasArg("btnpin")) {
    uint8_t bp = server.arg("btnpin").toInt();
    if (bp > 0 && bp <= 48) buttonPin = bp;
  }
  saveButtonSettings();
  initButton();

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Captive portal: redirect any unknown request to root
static void handleNotFound() {
  if (isAPMode()) {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not Found");
  }
}

// ---------------------------------------------------------------------------
//  Init & handle
// ---------------------------------------------------------------------------
void initWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save/wifi", HTTP_POST, handleSaveWifi);
  server.on("/save/printer", HTTP_POST, handleSavePrinter);
  server.on("/save/rotation", HTTP_POST, handleSaveRotation);
  server.on("/printer/config", HTTP_GET, handlePrinterConfig);
  server.on("/apply", HTTP_POST, handleApply);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/debug/toggle", HTTP_POST, handleDebugToggle);
  server.on("/cloud/logout", HTTP_POST, handleCloudLogout);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started on port 80");
}

void handleWebServer() {
  server.handleClient();
}
