#include "web_server.h"
#include "settings.h"
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "bambu_cloud.h"
#include "wifi_manager.h"
#include "display_ui.h"
#include "config.h"
#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer server(80);

// ---------------------------------------------------------------------------
//  HTML page (embedded PROGMEM)
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
  .card {
    background: #161B22; border: 1px solid #30363D; border-radius: 8px;
    padding: 16px; margin-bottom: 16px;
  }
  .card h2 { color: #58A6FF; font-size: 16px; margin-bottom: 12px; }
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
</style>
</head>
<body>
<h1>BambuHelper</h1>
<p class="subtitle">Bambu Lab Printer Monitor</p>
<div id="toast" class="toast">Applied!</div>

<!-- ===== WiFi & Printer (requires restart) ===== -->
<form method="POST" action="/save">
<div class="card">
  <h2>WiFi Settings</h2>
  <label for="ssid">WiFi SSID</label>
  <input type="text" name="ssid" id="ssid" value="%SSID%" placeholder="Your WiFi name">
  <label for="pass">WiFi Password</label>
  <input type="password" name="pass" id="pass" value="%PASS%" placeholder="WiFi password">
</div>

<div class="card">
  <h2>Network</h2>
  <label for="netmode">IP Assignment</label>
  <select name="netmode" id="netmode" onchange="toggleStatic()">
    <option value="dhcp" %NET_DHCP%>DHCP (automatic)</option>
    <option value="static" %NET_STATIC%>Static IP</option>
  </select>
  <div id="staticFields" style="display:none">
    <label for="net_ip">IP Address</label>
    <input type="text" name="net_ip" id="net_ip" value="%NET_IP%" placeholder="192.168.1.100">
    <label for="net_gw">Gateway</label>
    <input type="text" name="net_gw" id="net_gw" value="%NET_GW%" placeholder="192.168.1.1">
    <label for="net_sn">Subnet Mask</label>
    <input type="text" name="net_sn" id="net_sn" value="%NET_SN%" placeholder="255.255.255.0">
    <label for="net_dns">DNS Server</label>
    <input type="text" name="net_dns" id="net_dns" value="%NET_DNS%" placeholder="8.8.8.8">
  </div>
  <div class="check-row">
    <input type="checkbox" name="showip" id="showip" value="1" %SHOWIP%>
    <label for="showip">Show IP at startup (3s)</label>
  </div>
  <label for="tz">Timezone</label>
  <select name="tz" id="tz">
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
</div>

<div class="card">
  <h2>Printer Settings</h2>
  <div id="printerStatus" class="%STATUS_CLASS%">%STATUS_TEXT%</div>
  <div class="check-row">
    <input type="checkbox" name="enabled" id="enabled" value="1" %ENABLED%>
    <label for="enabled">Enable Monitoring</label>
  </div>

  <label for="connmode">Connection Mode</label>
  <select name="connmode" id="connmode" onchange="toggleConnMode()">
    <option value="local" %MODE_LOCAL%>LAN Direct (P1/X1/A1)</option>
    <option value="cloud" %MODE_CLOUD%>Bambu Cloud (H2/P2S)</option>
  </select>

  <div id="localFields">
    <label for="pname">Printer Name</label>
    <input type="text" name="pname" id="pname" value="%PNAME%" placeholder="My P1S" maxlength="23">
    <label for="ip">Printer IP Address</label>
    <input type="text" name="ip" id="ip" value="%IP%" placeholder="192.168.1.xxx">
    <label for="serial">Serial Number</label>
    <input type="text" name="serial" id="serial" value="%SERIAL%" placeholder="01P00A000000000" maxlength="19">
    <label for="code">LAN Access Code</label>
    <input type="text" name="code" id="code" value="%CODE%" placeholder="12345678" maxlength="8">
  </div>

  <div id="cloudFields" style="display:none">
    <p style="font-size:12px;color:#8B949E;margin:10px 0">For H2C/H2D/H2S/P2S printers. Connects via Bambu Cloud.<br>Token valid ~3 months. Your password is NOT stored.</p>
    <div id="cloudLoginSection">
      <label for="cl_email">Bambu Account Email</label>
      <input type="text" id="cl_email" value="%CL_EMAIL%" placeholder="user@example.com">
      <label for="cl_pass">Password</label>
      <input type="password" id="cl_pass" placeholder="Account password">
      <button type="button" class="btn btn-blue" style="margin-top:10px" onclick="cloudLogin()">Login to Bambu Cloud</button>
    </div>
    <div id="cloud2FA" style="display:none;margin-top:10px">
      <label for="cl_code">Verification Code (check your email)</label>
      <input type="text" id="cl_code" placeholder="123456" maxlength="6">
      <button type="button" class="btn btn-blue" style="margin-top:8px" onclick="cloudVerify()">Verify Code</button>
    </div>
    <div id="cloudStatus" style="margin-top:8px;font-size:13px;color:#8B949E">%CLOUD_STATUS%</div>
    <div id="cloudDevices" style="display:none;margin-top:10px">
      <label for="cl_device">Select Printer</label>
      <select id="cl_device" onchange="selectCloudDevice()"></select>
    </div>
    <input type="hidden" name="cl_serial" id="cl_serial" value="%SERIAL%">
    <input type="hidden" name="cl_pname" id="cl_pname" value="%PNAME%">
    <button type="button" class="btn btn-danger" style="margin-top:10px;display:none;font-size:12px;padding:6px"
            id="cloudLogoutBtn" onclick="cloudLogout()">Logout from Cloud</button>
  </div>
  <div id="liveStats"></div>
</div>
<button type="submit" class="btn btn-primary">Save WiFi/Printer &amp; Restart</button>
</form>

<!-- ===== Display (live apply, no restart) ===== -->
<div class="card">
  <h2>Display</h2>
  <label for="bright">Brightness: <span id="brightVal">%BRIGHT%</span></label>
  <input type="range" name="bright" id="bright" min="10" max="255" value="%BRIGHT%"
         oninput="document.getElementById('brightVal').textContent=this.value">

  <label for="rotation">Screen Rotation</label>
  <select name="rotation" id="rotation">
    <option value="0" %ROT0%>0&deg; (default)</option>
    <option value="1" %ROT1%>90&deg;</option>
    <option value="2" %ROT2%>180&deg;</option>
    <option value="3" %ROT3%>270&deg;</option>
  </select>

  <label for="fmins">Display off after print complete (minutes, 0 = never)</label>
  <input type="number" name="fmins" id="fmins" min="0" max="999" value="%FMINS%">
  <div class="check-row">
    <input type="checkbox" name="keepon" id="keepon" value="1" %KEEPON%>
    <label for="keepon">Keep display always on (override timeout)</label>
  </div>
</div>

<div class="card">
  <h2>Gauge Colors</h2>

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
      <input type="color" name="clr_bg" id="clr_bg" value="%CLR_BG%">
    </div>
    <div class="color-row">
      <label>Track</label>
      <input type="color" name="clr_track" id="clr_track" value="%CLR_TRACK%">
    </div>
  </div>

  <div class="gauge-section">
    <h3>Progress</h3>
    <div class="color-row">
      <label>Arc</label><input type="color" name="prg_a" id="prg_a" value="%PRG_A%">
      <label>Label</label><input type="color" name="prg_l" id="prg_l" value="%PRG_L%">
      <label>Value</label><input type="color" name="prg_v" id="prg_v" value="%PRG_V%">
    </div>
  </div>

  <div class="gauge-section">
    <h3>Nozzle</h3>
    <div class="color-row">
      <label>Arc</label><input type="color" name="noz_a" id="noz_a" value="%NOZ_A%">
      <label>Label</label><input type="color" name="noz_l" id="noz_l" value="%NOZ_L%">
      <label>Value</label><input type="color" name="noz_v" id="noz_v" value="%NOZ_V%">
    </div>
  </div>

  <div class="gauge-section">
    <h3>Bed</h3>
    <div class="color-row">
      <label>Arc</label><input type="color" name="bed_a" id="bed_a" value="%BED_A%">
      <label>Label</label><input type="color" name="bed_l" id="bed_l" value="%BED_L%">
      <label>Value</label><input type="color" name="bed_v" id="bed_v" value="%BED_V%">
    </div>
  </div>

  <div class="gauge-section">
    <h3>Part Fan</h3>
    <div class="color-row">
      <label>Arc</label><input type="color" name="pfn_a" id="pfn_a" value="%PFN_A%">
      <label>Label</label><input type="color" name="pfn_l" id="pfn_l" value="%PFN_L%">
      <label>Value</label><input type="color" name="pfn_v" id="pfn_v" value="%PFN_V%">
    </div>
  </div>

  <div class="gauge-section">
    <h3>Aux Fan</h3>
    <div class="color-row">
      <label>Arc</label><input type="color" name="afn_a" id="afn_a" value="%AFN_A%">
      <label>Label</label><input type="color" name="afn_l" id="afn_l" value="%AFN_L%">
      <label>Value</label><input type="color" name="afn_v" id="afn_v" value="%AFN_V%">
    </div>
  </div>

  <div class="gauge-section">
    <h3>Chamber Fan</h3>
    <div class="color-row">
      <label>Arc</label><input type="color" name="cfn_a" id="cfn_a" value="%CFN_A%">
      <label>Label</label><input type="color" name="cfn_l" id="cfn_l" value="%CFN_L%">
      <label>Value</label><input type="color" name="cfn_v" id="cfn_v" value="%CFN_V%">
    </div>
  </div>
</div>

<button type="button" class="btn btn-blue" onclick="applyDisplay()">Apply Display Settings</button>

<div class="card">
  <h2>Diagnostics</h2>
  <div class="check-row">
    <input type="checkbox" id="dbglog" onchange="toggleDebug(this.checked)" %DBGLOG%>
    <label for="dbglog">Verbose Serial logging (USB)</label>
  </div>
  <div id="diagInfo" style="margin-top:10px;font-size:12px;color:#8B949E"></div>
</div>

<button class="btn btn-danger" style="margin-top:10px"
        onclick="if(confirm('Reset all settings?'))location='/reset'">Factory Reset</button>

<script>
function toggleStatic(){
  var m=document.getElementById('netmode').value;
  document.getElementById('staticFields').style.display=(m==='static')?'block':'none';
}
toggleStatic();

function toggleConnMode(){
  var cloud=document.getElementById('connmode').value==='cloud';
  document.getElementById('localFields').style.display=cloud?'none':'block';
  document.getElementById('cloudFields').style.display=cloud?'block':'none';
}
toggleConnMode();

function cloudLogin(){
  var email=document.getElementById('cl_email').value;
  var pass=document.getElementById('cl_pass').value;
  if(!email||!pass){document.getElementById('cloudStatus').innerHTML='<span style="color:#F85149">Enter email and password</span>';return;}
  document.getElementById('cloudStatus').innerHTML='<span style="color:#58A6FF">Logging in...</span>';
  var p=new URLSearchParams();
  p.append('email',email);p.append('password',pass);
  fetch('/cloud/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.status==='ok'){
        document.getElementById('cloudStatus').innerHTML='<span style="color:#3FB950">Logged in!</span>';
        document.getElementById('cloudLoginSection').style.display='none';
        document.getElementById('cloudLogoutBtn').style.display='block';
        cloudFetchDevices();
      } else if(d.status==='verify'){
        document.getElementById('cloudStatus').innerHTML='<span style="color:#58A6FF">Check your email for a verification code</span>';
        document.getElementById('cloud2FA').style.display='block';
      } else {
        document.getElementById('cloudStatus').innerHTML='<span style="color:#F85149">'+(d.message||'Login failed')+'</span>';
      }
    })
    .catch(function(){document.getElementById('cloudStatus').innerHTML='<span style="color:#F85149">Network error</span>';});
}

function cloudVerify(){
  var email=document.getElementById('cl_email').value;
  var code=document.getElementById('cl_code').value;
  if(!code){return;}
  document.getElementById('cloudStatus').innerHTML='<span style="color:#58A6FF">Verifying...</span>';
  var p=new URLSearchParams();
  p.append('email',email);p.append('code',code);
  fetch('/cloud/verify',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.status==='ok'){
        document.getElementById('cloudStatus').innerHTML='<span style="color:#3FB950">Verified!</span>';
        document.getElementById('cloud2FA').style.display='none';
        document.getElementById('cloudLoginSection').style.display='none';
        document.getElementById('cloudLogoutBtn').style.display='block';
        if(d.printers&&d.printers.length>0){
          var sel=document.getElementById('cl_device');
          sel.innerHTML='';
          for(var i=0;i<d.printers.length;i++){
            var o=document.createElement('option');
            o.value=d.printers[i].serial;
            o.setAttribute('data-name',d.printers[i].name);
            o.textContent=d.printers[i].name+' ('+d.printers[i].model+')';
            sel.appendChild(o);
          }
          document.getElementById('cloudDevices').style.display='block';
          selectCloudDevice();
        }
      } else {
        document.getElementById('cloudStatus').innerHTML='<span style="color:#F85149">'+(d.message||'Verification failed')+'</span>';
      }
    })
    .catch(function(){document.getElementById('cloudStatus').innerHTML='<span style="color:#F85149">Network error</span>';});
}

function selectCloudDevice(){
  var sel=document.getElementById('cl_device');
  if(sel.selectedIndex<0)return;
  var opt=sel.options[sel.selectedIndex];
  document.getElementById('cl_serial').value=sel.value;
  document.getElementById('cl_pname').value=opt.getAttribute('data-name')||opt.textContent;
}

function cloudFetchDevices(){
  fetch('/cloud/devices').then(function(r){return r.json();}).then(function(d){
    if(d.printers&&d.printers.length>0){
      var sel=document.getElementById('cl_device');
      sel.innerHTML='';
      for(var i=0;i<d.printers.length;i++){
        var o=document.createElement('option');
        o.value=d.printers[i].serial;
        o.setAttribute('data-name',d.printers[i].name);
        o.textContent=d.printers[i].name+' ('+d.printers[i].model+')';
        sel.appendChild(o);
      }
      document.getElementById('cloudDevices').style.display='block';
      selectCloudDevice();
    }
  }).catch(function(){});
}

function cloudLogout(){
  fetch('/cloud/logout',{method:'POST'}).then(function(){
    document.getElementById('cloudStatus').innerHTML='<span style="color:#8B949E">Not logged in</span>';
    document.getElementById('cloudDevices').style.display='none';
    document.getElementById('cloudLogoutBtn').style.display='none';
    document.getElementById('cloudLoginSection').style.display='block';
  });
}

var themes={
  default:{bg:'#081018',track:'#182028',
    prg:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},
    noz:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},
    bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},
    pfn:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},
    afn:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},
    cfn:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'}},
  mono_green:{bg:'#000800',track:'#0A1A0A',
    prg:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},
    noz:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},
    bed:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},
    pfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},
    afn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},
    cfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'}},
  neon:{bg:'#0A0014',track:'#1A0A2E',
    prg:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},
    noz:{a:'#FF4400',l:'#FF6600',v:'#FFFFFF'},
    bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},
    pfn:{a:'#00FF88',l:'#00FF88',v:'#FFFFFF'},
    afn:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},
    cfn:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'}},
  warm:{bg:'#140A00',track:'#2E1A08',
    prg:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},
    noz:{a:'#FF6347',l:'#FF6347',v:'#FFEEDD'},
    bed:{a:'#FFA500',l:'#FFA500',v:'#FFEEDD'},
    pfn:{a:'#FFD700',l:'#FFD700',v:'#FFEEDD'},
    afn:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},
    cfn:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'}},
  ocean:{bg:'#000A14',track:'#0A1A2E',
    prg:{a:'#00BFFF',l:'#00BFFF',v:'#E0F0FF'},
    noz:{a:'#FF7F50',l:'#FF7F50',v:'#E0F0FF'},
    bed:{a:'#4169E1',l:'#4169E1',v:'#E0F0FF'},
    pfn:{a:'#00CED1',l:'#00CED1',v:'#E0F0FF'},
    afn:{a:'#48D1CC',l:'#48D1CC',v:'#E0F0FF'},
    cfn:{a:'#20B2AA',l:'#20B2AA',v:'#E0F0FF'}}
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

function toggleDebug(on){
  fetch('/debug/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'on='+(on?'1':'0')}).then(r=>{
    if(r.ok) showToast(on?'Debug ON':'Debug OFF');
  });
}
function refreshDiag(){
  fetch('/debug').then(r=>r.json()).then(d=>{
    var h='';
    h+='<div class="stat-row"><span>MQTT:</span><span class="stat-val">'+(d.connected?'<span style="color:#3FB950">Connected</span>':'<span style="color:#F85149">Disconnected</span>')+'</span></div>';
    h+='<div class="stat-row"><span>Attempts:</span><span class="stat-val">'+d.attempts+'</span></div>';
    h+='<div class="stat-row"><span>Messages RX:</span><span class="stat-val">'+d.messages+'</span></div>';
    h+='<div class="stat-row"><span>Last TCP:</span><span class="stat-val">'+(d.tcp_ok?'<span style="color:#3FB950">OK</span>':'<span style="color:#F85149">Failed</span>')+'</span></div>';
    if(d.last_rc!==0) h+='<div class="stat-row"><span>Last error:</span><span class="stat-val" style="color:#F85149">'+d.rc_text+' (rc='+d.last_rc+')</span></div>';
    h+='<div class="stat-row"><span>Free heap:</span><span class="stat-val">'+Math.round(d.heap/1024)+'KB</span></div>';
    h+='<div class="stat-row"><span>Uptime:</span><span class="stat-val">'+Math.round(d.uptime/60)+'min</span></div>';
    document.getElementById('diagInfo').innerHTML=h;
  }).catch(function(){});
}
refreshDiag();
setInterval(refreshDiag,5000);

function showToast(msg){
  var t=document.getElementById('toast');
  t.textContent=msg||'Applied!';
  t.style.display='block';
  setTimeout(function(){t.style.display='none';},2000);
}

function applyDisplay(){
  var p=new URLSearchParams();
  p.append('bright',document.getElementById('bright').value);
  p.append('rotation',document.getElementById('rotation').value);
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

setInterval(function(){
  fetch('/status').then(r=>r.json()).then(d=>{
    var h='';
    if(d.display_off) h+='<div class="stat-row"><span>Display:</span><span class="stat-val" style="color:#F85149">Off</span></div>';
    if(d.connected){
      h+='<div class="stat-row"><span>State:</span><span class="stat-val">'+d.state+'</span></div>';
      h+='<div class="stat-row"><span>Nozzle:</span><span class="stat-val">'+d.nozzle+'/'+d.nozzle_t+'&deg;C</span></div>';
      h+='<div class="stat-row"><span>Bed:</span><span class="stat-val">'+d.bed+'/'+d.bed_t+'&deg;C</span></div>';
      if(d.progress>0) h+='<div class="stat-row"><span>Progress:</span><span class="stat-val">'+d.progress+'%</span></div>';
      if(d.fan>0) h+='<div class="stat-row"><span>Fan:</span><span class="stat-val">'+d.fan+'%</span></div>';
    } else {
      h+='<span style="color:#8B949E">Not connected (printer may be off)</span>';
    }
    document.getElementById('liveStats').innerHTML=h;
    var ps=document.getElementById('printerStatus');
    var cls=d.connected?'status-ok':(d.enabled?'status-off':'status-na');
    var txt=d.connected?'Connected':(d.enabled?'Disconnected / Printer Off':'Disabled');
    if(d.display_off){cls='status-na';txt+=' (Display Off)';}
    ps.className='status '+cls;
    ps.textContent=txt;
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
  page.replace("%ENABLED%", cfg.enabled ? "checked" : "");
  page.replace("%MODE_LOCAL%", cfg.mode == CONN_LOCAL ? "selected" : "");
  page.replace("%MODE_CLOUD%", cfg.mode == CONN_CLOUD ? "selected" : "");
  page.replace("%PNAME%", cfg.name);
  page.replace("%IP%", cfg.ip);
  page.replace("%SERIAL%", cfg.serial);
  page.replace("%CODE%", cfg.accessCode);
  page.replace("%CL_EMAIL%", cloudEmail);

  // Cloud status text
  if (cfg.mode == CONN_CLOUD && strlen(cloudEmail) > 0) {
    char clStatus[96];
    snprintf(clStatus, sizeof(clStatus), "Logged in as %s", cloudEmail);
    page.replace("%CLOUD_STATUS%", clStatus);
  } else {
    page.replace("%CLOUD_STATUS%", "Not logged in");
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

  // Timezone dropdown — mark the selected option
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

  // Rotation dropdown
  page.replace("%ROT0%", dispSettings.rotation == 0 ? "selected" : "");
  page.replace("%ROT1%", dispSettings.rotation == 1 ? "selected" : "");
  page.replace("%ROT2%", dispSettings.rotation == 2 ? "selected" : "");
  page.replace("%ROT3%", dispSettings.rotation == 3 ? "selected" : "");

  // Display power
  page.replace("%FMINS%", String(dpSettings.finishDisplayMins));
  page.replace("%KEEPON%", dpSettings.keepDisplayOn ? "checked" : "");

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

  if (cfg.enabled && st.connected) {
    page.replace("%STATUS_CLASS%", "status status-ok");
    page.replace("%STATUS_TEXT%", "Connected");
  } else if (cfg.enabled) {
    page.replace("%STATUS_CLASS%", "status status-off");
    page.replace("%STATUS_TEXT%", "Disconnected");
  } else {
    page.replace("%STATUS_CLASS%", "status status-na");
    page.replace("%STATUS_TEXT%", "Disabled");
  }

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
//  Read display settings from form args (shared by /save and /apply)
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
}

// ---------------------------------------------------------------------------
//  Route handlers
// ---------------------------------------------------------------------------
static void handleRoot() {
  String html = FPSTR(PAGE_HTML);
  server.send(200, "text/html", processTemplate(html));
}

// Save WiFi + Printer settings (requires restart)
static void handleSave() {
  if (server.hasArg("ssid")) {
    strlcpy(wifiSSID, server.arg("ssid").c_str(), sizeof(wifiSSID));
  }
  if (server.hasArg("pass")) {
    strlcpy(wifiPass, server.arg("pass").c_str(), sizeof(wifiPass));
  }

  PrinterConfig& cfg = printers[0].config;
  cfg.enabled = server.hasArg("enabled");

  // Connection mode
  if (server.hasArg("connmode")) {
    cfg.mode = (server.arg("connmode") == "cloud") ? CONN_CLOUD : CONN_LOCAL;
  }

  // Network settings
  netSettings.useDHCP = (!server.hasArg("netmode") || server.arg("netmode") == "dhcp");
  if (server.hasArg("net_ip"))  strlcpy(netSettings.staticIP, server.arg("net_ip").c_str(), sizeof(netSettings.staticIP));
  if (server.hasArg("net_gw"))  strlcpy(netSettings.gateway, server.arg("net_gw").c_str(), sizeof(netSettings.gateway));
  if (server.hasArg("net_sn"))  strlcpy(netSettings.subnet, server.arg("net_sn").c_str(), sizeof(netSettings.subnet));
  if (server.hasArg("net_dns")) strlcpy(netSettings.dns, server.arg("net_dns").c_str(), sizeof(netSettings.dns));
  netSettings.showIPAtStartup = server.hasArg("showip");
  if (server.hasArg("tz")) netSettings.gmtOffsetMin = server.arg("tz").toInt();

  if (cfg.mode == CONN_CLOUD) {
    // Cloud mode: serial and name come from hidden fields
    if (server.hasArg("cl_serial")) strlcpy(cfg.serial, server.arg("cl_serial").c_str(), sizeof(cfg.serial));
    if (server.hasArg("cl_pname"))  strlcpy(cfg.name, server.arg("cl_pname").c_str(), sizeof(cfg.name));
    // Extract userId from stored token
    char tokenBuf[1200];
    if (loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
      cloudExtractUserId(tokenBuf, cfg.cloudUserId, sizeof(cfg.cloudUserId));
    }
  } else {
    // Local mode: standard fields
    if (server.hasArg("pname")) strlcpy(cfg.name, server.arg("pname").c_str(), sizeof(cfg.name));
    if (server.hasArg("ip"))    strlcpy(cfg.ip, server.arg("ip").c_str(), sizeof(cfg.ip));
    if (server.hasArg("serial"))strlcpy(cfg.serial, server.arg("serial").c_str(), sizeof(cfg.serial));
    if (server.hasArg("code"))  strlcpy(cfg.accessCode, server.arg("code").c_str(), sizeof(cfg.accessCode));
  }

  saveSettings();

  server.send(200, "text/html",
    "<html><body style='background:#0D1117;color:#E6EDF3;text-align:center;padding-top:80px;font-family:sans-serif'>"
    "<h2 style='color:#3FB950'>Settings Saved!</h2>"
    "<p>Restarting...</p></body></html>");

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
  BambuState& st = printers[0].state;
  PrinterConfig& cfg = printers[0].config;

  JsonDocument doc;
  doc["connected"] = st.connected;
  doc["enabled"] = cfg.enabled;
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
  const MqttDiag& d = getMqttDiag();
  BambuState& st = printers[0].state;

  JsonDocument doc;
  doc["connected"] = st.connected;
  doc["attempts"] = d.attempts;
  doc["messages"] = d.messagesRx;
  doc["last_rc"] = d.lastRc;
  doc["rc_text"] = mqttRcToString(d.lastRc);
  doc["tcp_ok"] = d.tcpOk;
  doc["heap"] = d.freeHeap;
  doc["connect_ms"] = d.connectDurMs;
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

// ---------------------------------------------------------------------------
//  Cloud login endpoints
// ---------------------------------------------------------------------------
static void handleCloudLogin() {
  String email = server.arg("email");
  String pass = server.arg("password");

  CloudResult r = cloudLogin(email.c_str(), pass.c_str());

  JsonDocument doc;
  switch (r) {
    case CLOUD_OK:          doc["status"] = "ok"; break;
    case CLOUD_NEED_VERIFY: doc["status"] = "verify"; break;
    case CLOUD_BAD_CREDS:   doc["status"] = "error"; doc["message"] = "Invalid email or password"; break;
    default:                doc["status"] = "error"; doc["message"] = "Network or server error"; break;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleCloudVerify() {
  String email = server.arg("email");
  String code = server.arg("code");

  CloudResult r = cloudVerifyCode(email.c_str(), code.c_str());

  JsonDocument doc;
  if (r == CLOUD_OK) {
    doc["status"] = "ok";
    // Fetch and return device list
    char tokenBuf[1200];
    if (loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
      CloudPrinter devs[8];
      int count = cloudFetchDevices(tokenBuf, devs, 8);
      JsonArray arr = doc["printers"].to<JsonArray>();
      for (int i = 0; i < count; i++) {
        JsonObject d = arr.add<JsonObject>();
        d["serial"] = devs[i].serial;
        d["name"] = devs[i].name;
        d["model"] = devs[i].model;
      }
    }
  } else {
    doc["status"] = "error";
    doc["message"] = (r == CLOUD_BAD_CREDS) ? "Invalid verification code" : "Verification failed";
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleCloudDevices() {
  char tokenBuf[1200];
  if (!loadCloudToken(tokenBuf, sizeof(tokenBuf))) {
    server.send(200, "application/json", "{\"printers\":[]}");
    return;
  }
  CloudPrinter devs[8];
  int count = cloudFetchDevices(tokenBuf, devs, 8);

  JsonDocument doc;
  JsonArray arr = doc["printers"].to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject d = arr.add<JsonObject>();
    d["serial"] = devs[i].serial;
    d["name"] = devs[i].name;
    d["model"] = devs[i].model;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleCloudLogout() {
  clearCloudToken();
  server.send(200, "text/plain", "OK");
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
  server.on("/save", HTTP_POST, handleSave);
  server.on("/apply", HTTP_POST, handleApply);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/debug/toggle", HTTP_POST, handleDebugToggle);
  server.on("/cloud/login", HTTP_POST, handleCloudLogin);
  server.on("/cloud/verify", HTTP_POST, handleCloudVerify);
  server.on("/cloud/devices", HTTP_GET, handleCloudDevices);
  server.on("/cloud/logout", HTTP_POST, handleCloudLogout);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started on port 80");
}

void handleWebServer() {
  server.handleClient();
}
