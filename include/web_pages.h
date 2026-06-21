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
//    PAGE_HTML     - the full configuration page (shell + sidebar + six
//                    section blocks). Run through resolvePlaceholder() by
//                    streamTemplate() in chunks of 2 KB.
//
//  Section blocks live as hidden <div id="sec-..."> elements inside the main
//  panel. Sidebar clicks toggle the `hidden` attribute - all six sections live
//  in the DOM at all times, so user-edited input values persist across nav.
// =============================================================================
#pragma once

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
//  Main page - shell + sidebar + six hidden section <div> blocks.
//
//  Field IDs (audited against web_server.cpp, NOT the design handoff README):
//    Printer:  pname, ip, serial, code, connmode, region, cl_token, cl_serial,
//              cl_pname, dualp, gs0..gs5, amsv
//    Display:  bright, nighten, nstart, nend, nbright, ssbright, afterprint,
//              fmins, dack, kps, pong, abar, slbl, shtire, fanmp, hidelp, invcol,
//              cydcls, rotation, tz, use24h, datefmt, clk_time, clk_date,
//              clk_size, clk_hidedate, noz_max, bed_max, cht_max, pwr_max,
//              gsmooth, warn_thr, warn_clr,
//              clr_bg, clr_track, clr_pbar, bulk_a/l/v,
//              prg/noz/bed/pfn/afn/afr/cfn/exh/cht/hbk/pwr/lyr + _a/_l/_v,
//              prg/noz/bed/pfn/afn/afr/cfn/exh/cht/hbk/pwr/lyr/clk/ams/nzr/nzl/dor + _lbl
//    Hardware: rotmode, rotinterval, btntype, btnpin, buzzen (DOUBLE Z!),
//              buzpin, buzqs, buzqe, buzclick, buzbeden, buzbedtemp, leden,
//              ledpin, ledbr, ledfxmd, ledfxsec, ledfxbr, ledauto, ledpause,
//              lederr, batshow
//    WiFi:     ssid, pass, showpass2, netmode, net_ip, net_gw, net_sn,
//              net_dns, showip, importFile, otaFile
//    Power:    tsm_cur, tsm_tar, tsm_en, tsm_pt, tsm_ip, tsm_dm (radio), tsm_pi,
//              tsm_ao, tsm_ad, tsm_aod, tsm_slot
//    Diag:     dbglog
// -----------------------------------------------------------------------------
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>BambuHelper</title>
<style>
/* ============ Design tokens ============ */
:root {
  --accent: #E0623A;
  --accent-soft: rgba(224, 98, 58, 0.16);
  --accent-hover: #E87655;
  --bg: #FAF8F6;
  --bg-elev: #FFFFFF;
  --bg-sub: #F3F0EC;
  --bg-input: #FFFFFF;
  --line: #E1DDD7;
  --line-soft: #ECE9E3;
  --text: #1F1F1E;
  --text-mid: #6A6862;
  --text-dim: #8C8A83;
  --danger: #DC4538;
  --success: #2DA34F;
  --warn: #D89A2C;
  --info: #1F7AD8;
  --sp-1: 4px; --sp-2: 8px; --sp-3: 12px; --sp-4: 16px; --sp-5: 20px; --sp-6: 28px; --sp-7: 40px;
  --radius-s: 6px; --radius: 10px; --radius-l: 14px;
  --shadow-sm: 0 1px 2px rgba(0,0,0,0.06);
  --shadow: 0 6px 24px -8px rgba(0,0,0,0.12), 0 1px 2px rgba(0,0,0,0.04);
  --sidebar-w: 240px;
  --header-h: 56px;
  --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  --mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  --motion: 180ms cubic-bezier(.2,.6,.2,1);
}
html[data-theme="dark"] {
  --bg: #14171B;
  --bg-elev: #1B1F24;
  --bg-sub: #181C20;
  --bg-input: #21262D;
  --line: #2A2F36;
  --line-soft: #22272D;
  --text: #E6EDF3;
  --text-mid: #B8BFC7;
  --text-dim: #8B949E;
  --shadow-sm: 0 1px 2px rgba(0,0,0,0.45);
  --shadow: 0 6px 24px -8px rgba(0,0,0,0.65), 0 1px 2px rgba(0,0,0,0.4);
}
*, *::before, *::after { box-sizing: border-box; }
html, body { margin: 0; padding: 0; height: 100%; }
body {
  font-family: var(--sans);
  background: var(--bg);
  color: var(--text);
  font-size: 14px;
  line-height: 1.45;
  -webkit-font-smoothing: antialiased;
}
button, input, select, textarea { font-family: inherit; font-size: inherit; color: inherit; }

/* ============ App shell ============ */
.app { display: grid; grid-template-columns: var(--sidebar-w) 1fr; min-height: 100vh; }
.topbar {
  position: sticky; top: 0; z-index: 30;
  grid-column: 1 / -1;
  height: var(--header-h);
  display: flex; align-items: center;
  padding: 0 var(--sp-5);
  background: var(--bg-elev);
  border-bottom: 1px solid var(--line);
  gap: var(--sp-4);
}
.topbar .hamburger {
  display: none;
  background: transparent; border: 1px solid var(--line);
  width: 36px; height: 36px; border-radius: var(--radius-s);
  cursor: pointer; align-items: center; justify-content: center;
  color: var(--text-mid);
}
.topbar .hamburger:hover { color: var(--text); border-color: var(--text-dim); }
.brand { display: flex; align-items: center; gap: var(--sp-3); font-weight: 600; font-size: 15px; color: var(--text); }
.brand .mark {
  width: 26px; height: 26px; border-radius: 7px;
  background: linear-gradient(135deg, var(--accent), #B14826);
  display: grid; place-items: center;
  color: white; font-weight: 700; font-size: 13px;
  box-shadow: var(--shadow-sm);
}
.version-pill {
  font-family: var(--mono); font-size: 11px;
  color: var(--text-mid);
  padding: 2px 7px;
  border: 1px solid var(--line);
  border-radius: 999px;
  background: var(--bg-sub);
}
.section-title { margin-left: var(--sp-5); font-size: 14px; color: var(--text-mid); font-weight: 500; }
.section-title::before { content: ""; display: inline-block; width: 1px; height: 16px; background: var(--line); margin-right: var(--sp-5); vertical-align: middle; }
.topbar-actions { margin-left: auto; display: flex; align-items: center; gap: var(--sp-2); }
.icon-btn {
  width: 34px; height: 34px;
  border-radius: var(--radius-s);
  border: 1px solid transparent;
  background: transparent; color: var(--text-mid);
  display: grid; place-items: center; cursor: pointer;
  transition: background var(--motion), color var(--motion), border-color var(--motion);
}
.icon-btn:hover { background: var(--bg-sub); color: var(--text); border-color: var(--line); }
.icon-btn svg { width: 18px; height: 18px; }
.status-dot { display: inline-flex; align-items: center; gap: 6px; font-size: 12px; color: var(--text-mid); font-family: var(--mono); }
.status-dot::before { content: ""; width: 7px; height: 7px; border-radius: 50%; background: var(--success); box-shadow: 0 0 0 3px rgba(45, 163, 79, 0.18); }
.status-dot.off::before { background: var(--text-dim); box-shadow: none; }

/* ============ Sidebar ============ */
.sidebar {
  border-right: 1px solid var(--line);
  background: var(--bg-sub);
  padding: var(--sp-5) var(--sp-3);
  overflow-y: auto;
  position: sticky; top: var(--header-h);
  align-self: start;
  height: calc(100vh - var(--header-h));
}
.sidebar h4 {
  text-transform: uppercase;
  font-size: 10px;
  letter-spacing: 0.08em;
  font-weight: 600;
  color: var(--text-dim);
  padding: var(--sp-3) var(--sp-3) var(--sp-2);
  margin: 0;
}
.nav-item {
  width: 100%;
  display: flex; align-items: center; justify-content: space-between;
  gap: var(--sp-2);
  padding: 9px var(--sp-3);
  margin-bottom: 2px;
  border-radius: var(--radius-s);
  border: 1px solid transparent;
  background: transparent;
  color: var(--text-mid);
  text-align: left;
  font-size: 13.5px;
  cursor: pointer;
  position: relative;
  transition: background var(--motion), color var(--motion);
}
.nav-item:hover { background: var(--bg-elev); color: var(--text); }
.nav-item[aria-current="true"] { background: var(--bg-elev); color: var(--text); font-weight: 600; box-shadow: var(--shadow-sm); }
.nav-item[aria-current="true"]::before { content: ""; position: absolute; left: -3px; top: 8px; bottom: 8px; width: 3px; border-radius: 0 3px 3px 0; background: var(--accent); }
.sidebar-footer { margin-top: var(--sp-5); padding: var(--sp-3); font-size: 11px; color: var(--text-dim); font-family: var(--mono); border-top: 1px solid var(--line-soft); }

/* ============ Main ============ */
.main { padding: var(--sp-6) var(--sp-7); max-width: 880px; width: 100%; }
.section[hidden] { display: none !important; }

/* ============ Cards ============ */
.card {
  background: var(--bg-elev);
  border: 1px solid var(--line);
  border-radius: var(--radius-l);
  padding: var(--sp-5) var(--sp-6);
  margin-bottom: var(--sp-5);
  box-shadow: var(--shadow-sm);
}
.card-head { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: var(--sp-4); gap: var(--sp-4); }
.card-head h3 { margin: 0; font-size: 15px; font-weight: 600; color: var(--text); }
.card-head p { margin: 4px 0 0; font-size: 12.5px; color: var(--text-dim); }
/* Reusable greyed helper text under a control, styled like .card-head p. */
.help-text { margin: 4px 0 14px; font-size: 12.5px; color: var(--text-dim); line-height: 1.45; }
.section-intro { margin: 0 0 var(--sp-5); }
.section-intro h2 { margin: 0 0 4px; font-size: 22px; font-weight: 600; letter-spacing: -0.015em; }
.section-intro p { margin: 0; color: var(--text-dim); font-size: 13.5px; }

/* ============ Form rows ============ */
.field { margin-bottom: var(--sp-4); }
.field > label, .field > .field-label {
  display: block;
  font-size: 12.5px;
  font-weight: 500;
  color: var(--text-mid);
  margin: 0 0 6px;
}
.field > label.hstack { display: flex; align-items: center; gap: var(--sp-2); }
.field .hint { margin: 6px 0 0; font-size: 12px; color: var(--text-dim); }
.row { display: grid; grid-template-columns: 1fr 1fr; gap: var(--sp-4); }
.row-3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: var(--sp-3); }

/* ============ Inputs ============ */
.section input[type="text"],
.section input[type="password"],
.section input[type="number"],
.section input[type="url"],
.section select,
.section textarea {
  width: 100%;
  padding: 9px 12px;
  border-radius: var(--radius-s);
  border: 1px solid var(--line);
  background: var(--bg-input);
  color: var(--text);
  font-size: 13.5px;
  outline: none;
  transition: border-color var(--motion), box-shadow var(--motion);
}
.section input:focus, .section select:focus, .section textarea:focus { border-color: var(--accent); box-shadow: 0 0 0 3px var(--accent-soft); }
.section input::placeholder, .section textarea::placeholder { color: var(--text-dim); }
.section input.mono, .section textarea.mono { font-family: var(--mono); font-size: 12.5px; }
.section select { appearance: none; padding-right: 28px; }
.section textarea { font-family: var(--mono); resize: vertical; min-height: 64px; }
.section input[type="range"] { -webkit-appearance: none; appearance: none; width: 100%; background: transparent; padding: 8px 0; }
.section input[type="range"]::-webkit-slider-runnable-track { height: 4px; border-radius: 2px; background: var(--line); }
.section input[type="range"]::-moz-range-track { height: 4px; border-radius: 2px; background: var(--line); }
.section input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; border-radius: 50%; background: var(--bg-elev); border: 2px solid var(--accent); margin-top: -6px; cursor: pointer; box-shadow: var(--shadow-sm); }
.section input[type="range"]::-moz-range-thumb { width: 14px; height: 14px; border-radius: 50%; background: var(--bg-elev); border: 2px solid var(--accent); cursor: pointer; }
.section input[type="color"] { width: 36px; height: 26px; padding: 0; border: 1px solid var(--line); border-radius: var(--radius-s); background: var(--bg-input); cursor: pointer; vertical-align: middle; }
.section input[type="color"]::-webkit-color-swatch { border: none; border-radius: 4px; }
.section input[type="color"]::-moz-color-swatch { border: none; border-radius: 4px; }
.section input[type="file"] { width: 100%; padding: 6px; background: var(--bg-input); border: 1px solid var(--line); border-radius: var(--radius-s); color: var(--text); font-size: 13px; }

/* ============ Check rows ============ */
.check-row {
  display: flex; align-items: flex-start; gap: 10px;
  padding: 10px 0;
  cursor: pointer;
}
.check-row + .check-row { border-top: 1px solid var(--line-soft); }
.check-row input[type="checkbox"] {
  appearance: none;
  width: 18px; height: 18px;
  border-radius: 5px;
  border: 1.5px solid var(--line);
  background: var(--bg-input);
  display: grid; place-items: center;
  cursor: pointer;
  flex-shrink: 0;
  margin-top: 1px;
  transition: background var(--motion), border-color var(--motion);
  position: relative;
}
.check-row input[type="checkbox"]:checked { background: var(--accent); border-color: var(--accent); }
.check-row input[type="checkbox"]:checked::after { content: ""; position: absolute; width: 5px; height: 9px; border-right: 2px solid white; border-bottom: 2px solid white; transform: rotate(45deg); top: 1px; left: 5px; }
.check-row label { font-size: 13.5px; color: var(--text); cursor: pointer; flex: 1; line-height: 1.4; margin: 0; font-weight: 500; padding: 0; }
.check-row + p, .field + p { font-size: 12px; color: var(--text-dim); margin: -4px 0 var(--sp-2); padding-left: 28px; }

/* ============ Buttons ============ */
.btn {
  display: inline-flex; align-items: center; justify-content: center; gap: 8px;
  padding: 9px 16px;
  border-radius: var(--radius-s);
  border: 1px solid transparent;
  background: var(--bg-elev);
  color: var(--text);
  font-size: 13.5px;
  font-weight: 500;
  cursor: pointer;
  transition: background var(--motion), border-color var(--motion), transform 80ms;
  margin: var(--sp-2) 0 0;
  text-align: center;
}
.btn:active { transform: translateY(1px); }
.btn-primary { background: var(--accent); color: white; border-color: var(--accent); }
.btn-primary:hover { background: var(--accent-hover); border-color: var(--accent-hover); }
.btn-blue { background: var(--info); color: white; border-color: var(--info); }
.btn-blue:hover { filter: brightness(1.07); }
.btn-ghost { background: transparent; border-color: var(--line); color: var(--text-mid); }
.btn-ghost:hover { background: var(--bg-sub); color: var(--text); }
.btn-danger { background: transparent; border-color: var(--danger); color: var(--danger); }
.btn-danger:hover { background: rgba(220, 69, 56, 0.10); }
.btn-success-solid { background: var(--success); color: white; border-color: var(--success); }
.btn-success-solid:hover { filter: brightness(1.08); }
.btn-danger-solid { background: var(--danger); color: white; border-color: var(--danger); }
.btn-danger-solid:hover { filter: brightness(1.08); }
.btn-sm { padding: 5px 10px; font-size: 12px; }
.btn-block { display: flex; width: 100%; }

/* ============ Segmented / tabs ============ */
.seg {
  display: inline-flex;
  border: 1px solid var(--line);
  background: var(--bg-sub);
  border-radius: var(--radius-s);
  padding: 3px;
  gap: 2px;
}
.seg button {
  background: transparent; border: none;
  padding: 6px 14px; border-radius: 5px;
  color: var(--text-mid); cursor: pointer;
  font-size: 12.5px; font-weight: 500;
  white-space: nowrap;
  transition: background var(--motion), color var(--motion);
}
.seg button[aria-pressed="true"] { background: var(--bg-elev); color: var(--text); box-shadow: var(--shadow-sm); }

/* ============ Banners ============ */
.banner {
  padding: 10px 14px;
  border-radius: var(--radius-s);
  font-size: 12.5px;
  display: flex; align-items: flex-start; gap: 10px;
  border: 1px solid var(--line);
  background: var(--bg-sub);
  color: var(--text-mid);
  margin-bottom: var(--sp-3);
}
.banner strong { color: var(--text); font-weight: 600; }
.banner-ok { border-color: rgba(45, 163, 79, 0.35); background: rgba(45, 163, 79, 0.10); }
.banner-warn { border-color: rgba(216, 154, 44, 0.40); background: rgba(216, 154, 44, 0.10); }
.banner-err { border-color: rgba(220, 69, 56, 0.40); background: rgba(220, 69, 56, 0.10); }
.banner .dot { width: 8px; height: 8px; border-radius: 50%; margin-top: 5px; flex-shrink: 0; background: currentColor; }

/* ============ KV list ============ */
.kv { display: grid; grid-template-columns: max-content 1fr; column-gap: var(--sp-5); row-gap: 6px; font-size: 12.5px; }
.kv dt { color: var(--text-dim); }
.kv dd { margin: 0; font-family: var(--mono); color: var(--text); }

/* ============ Card action bar ============ */
.action-bar {
  display: flex; justify-content: flex-end; gap: var(--sp-2);
  padding-top: var(--sp-4);
  margin-top: var(--sp-4);
  border-top: 1px solid var(--line-soft);
  flex-wrap: wrap;
}

/* ============ Disclosure ============ */
.section details {
  border-top: 1px solid var(--line-soft);
  padding-top: var(--sp-4);
  margin-top: var(--sp-4);
}
.section details summary {
  cursor: pointer;
  list-style: none;
  display: flex; align-items: center; gap: 8px;
  font-size: 13px; font-weight: 500; color: var(--text);
  user-select: none;
}
.section details summary.summary-lg { font-size: 15px; font-weight: 600; }

/* ============ Collapsible card variant ============ */
/* <details class="card card-collapsible"> with <summary> as the card-head and
   <div class="card-body"> as the body. Overrides .card padding (summary owns
   it) and the .section details border/margin so it reads as a card, not as a
   disclosure inside one. */
.section details.card-collapsible { border-top: none; padding-top: 0; margin-top: 0; padding: 0; }
.card-collapsible > summary {
  cursor: pointer; list-style: none; user-select: none;
  padding: var(--sp-5) var(--sp-6);
  display: flex; align-items: center; justify-content: space-between; gap: var(--sp-4);
  border-radius: var(--radius-l);
  transition: background var(--motion);
}
.card-collapsible > summary::-webkit-details-marker { display: none; }
.card-collapsible > summary:hover { background: var(--bg-sub); }
.card-collapsible > summary > div { flex: 1; }
.card-collapsible > summary h3 { margin: 0; font-size: 15px; font-weight: 600; color: var(--text); }
.card-collapsible > summary p { margin: 4px 0 0; font-size: 12.5px; color: var(--text-dim); }
.card-collapsible > summary::after {
  content: "";
  width: 8px; height: 8px;
  border-right: 1.5px solid var(--text-mid);
  border-bottom: 1.5px solid var(--text-mid);
  transform: rotate(-45deg);
  transition: transform var(--motion);
  flex-shrink: 0;
}
.card-collapsible[open] > summary { border-bottom: 1px solid var(--line-soft); }
.card-collapsible[open] > summary::after { transform: rotate(45deg); }
.card-collapsible > .card-body { padding: var(--sp-4) var(--sp-6) var(--sp-5); }
.section details summary::-webkit-details-marker { display: none; }
.section details summary::before {
  content: ""; width: 6px; height: 6px;
  border-right: 1.5px solid var(--text-mid);
  border-bottom: 1.5px solid var(--text-mid);
  transform: rotate(-45deg);
  transition: transform var(--motion);
}
.section details[open] summary::before { transform: rotate(45deg); }
.section details > *:not(summary) { margin-top: var(--sp-3); }

/* ============ Tabs (printer / plug slots) ============ */
.slot-tabs { display: flex; gap: 4px; flex-shrink: 0; }
.tab-btn, .power-tab-btn {
  flex: 1; padding: 8px 14px;
  min-width: 92px;
  white-space: nowrap;
  border: 1px solid var(--line);
  border-radius: var(--radius-s);
  background: var(--bg-sub);
  color: var(--text-mid);
  cursor: pointer;
  font-size: 13px; font-weight: 500;
  transition: background var(--motion), color var(--motion);
}
.tab-btn.active, .power-tab-btn.active { background: var(--accent); color: white; border-color: var(--accent); }
/* Card-head variant for cards with slot tabs: title + tabs on row 1, full-width description on row 2.
   Without this the description gets squeezed into a narrow column on the left because the tabs reserve
   ~190 px of the card-head's right side. */
.card-head-tabs { margin-bottom: var(--sp-2); align-items: center; }
.card-head-tabs h3 { flex: 1; min-width: 0; }
.card-head-tabs + .card-desc { margin: 0 0 var(--sp-4); font-size: 12.5px; color: var(--text-dim); }

/* ============ Status pills (printer connection state) ============ */
.status-pill {
  padding: 8px 12px;
  border-radius: var(--radius-s);
  margin-bottom: var(--sp-3);
  font-size: 13px; font-weight: 600;
  border-left: 3px solid var(--line);
  background: var(--bg-sub);
  color: var(--text-mid);
}
.status-pill.status-ok { border-left-color: var(--success); color: var(--success); }
.status-pill.status-off { border-left-color: var(--danger); color: var(--danger); }
.status-pill.status-na { border-left-color: var(--text-dim); color: var(--text-dim); }

/* ============ Gauge layout grid ============ */
.gauge-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: var(--sp-2); margin-top: var(--sp-3); }
.gauge-grid .cell { background: var(--bg-sub); border: 1px dashed var(--line); border-radius: var(--radius-s); padding: var(--sp-3); }
.gauge-grid .cell label { display: block; font-size: 10.5px; color: var(--text-dim); margin-bottom: 4px; text-transform: uppercase; letter-spacing: 0.05em; }
.gauge-grid .cell select { padding: 5px 8px; font-size: 12px; }
.row-divider { display: flex; align-items: center; gap: 8px; font-family: var(--mono); font-size: 11px; color: var(--text-dim); margin: var(--sp-3) 0 4px; }
.row-divider::before, .row-divider::after { content: ""; flex: 1; height: 1px; background: var(--line-soft); }

/* ============ Gauge color sub-rows ============ */
/* name | custom-label input (fills the gap) | Arc | Label | Value */
.gauge-color-row { display: grid; grid-template-columns: auto minmax(90px, 1fr) auto auto auto; gap: 10px; align-items: center; padding: 8px 0; border-bottom: 1px solid var(--line-soft); font-size: 12.5px; }
.gauge-color-row:last-child { border-bottom: none; }
.gauge-color-row .name { color: var(--text); font-weight: 500; white-space: nowrap; }
.gauge-color-row .alv { display: flex; align-items: center; gap: 4px; }
.gauge-color-row .alv span { font-size: 11px; color: var(--text-dim); }
/* Custom-label input sits next to the gauge name. Fixed ~12-char width
   (justify-self:start) so it doesn't stretch; the 1fr column absorbs the slack
   and keeps the colour pickers right-aligned. */
.gauge-color-row .lbl { grid-column: 2; grid-row: 1; max-width: 14ch; justify-self: start; font-size: 12.5px; padding: 6px 10px; }
/* Label-only rows (Clock, AMS, Nozzle L/R): name + text input, no colour pickers. */
.gauge-label-row { display: grid; grid-template-columns: auto minmax(90px, 1fr); gap: 10px; align-items: center; padding: 8px 0; border-bottom: 1px solid var(--line-soft); font-size: 12.5px; }
.gauge-label-row:last-child { border-bottom: none; }
.gauge-label-row .name { color: var(--text); font-weight: 500; white-space: nowrap; }
.gauge-label-row .lbl { max-width: 14ch; justify-self: start; font-size: 12.5px; padding: 6px 10px; }

/* ============ Bulk color picker row (Gauge Colors) ============ */
.bulk-color-row { display: flex; flex-wrap: wrap; gap: var(--sp-5); align-items: center; margin-bottom: var(--sp-3); }
.bulk-color-row label { display: inline-flex; align-items: center; gap: 8px; font-size: 12.5px; font-weight: 500; color: var(--text-mid); margin: 0; cursor: pointer; }

/* ============ Theme swatches (gauge color presets) ============ */
.swatch-row { display: flex; gap: var(--sp-2); flex-wrap: wrap; margin-bottom: var(--sp-3); }
.swatch { padding: 6px 10px; border: 1px solid var(--line); border-radius: 999px; background: var(--bg-input); cursor: pointer; font-size: 12px; display: inline-flex; align-items: center; gap: 6px; color: var(--text-mid); }
.swatch:hover { color: var(--text); border-color: var(--text-dim); }
.swatch .blob { width: 12px; height: 12px; border-radius: 50%; }

/* ============ Toast ============ */
.toast {
  position: fixed; top: 70px; left: 50%; transform: translateX(-50%) translateY(-12px);
  background: var(--text); color: var(--bg);
  padding: 9px 16px; border-radius: 999px;
  font-size: 13px; font-weight: 500;
  box-shadow: var(--shadow);
  opacity: 0; pointer-events: none;
  transition: opacity var(--motion), transform var(--motion);
  z-index: 60;
  max-width: 90vw;
  text-align: center;
}
.toast.show { opacity: 1; transform: translateX(-50%) translateY(0); }

/* ============ Mobile drawer ============ */
.scrim {
  position: fixed; inset: 0;
  background: rgba(0,0,0,0.35);
  z-index: 25;
  opacity: 0; pointer-events: none;
  transition: opacity var(--motion);
}
.scrim.show { opacity: 1; pointer-events: auto; }
@media (max-width: 820px) {
  .app { grid-template-columns: 1fr !important; }
  .topbar .hamburger { display: inline-flex; }
  .sidebar {
    position: fixed; top: var(--header-h); left: 0;
    width: 260px; height: calc(100vh - var(--header-h));
    transform: translateX(-100%); transition: transform var(--motion);
    z-index: 26; box-shadow: var(--shadow);
  }
  .sidebar.open { transform: translateX(0); }
  .main { padding: var(--sp-5); }
  .section-title { display: none; }
  .row, .row-3 { grid-template-columns: 1fr; }
  /* Gauge rows: name + colours on line 1, label input full-width on line 2. */
  .gauge-color-row { grid-template-columns: minmax(110px, 1fr) auto auto auto; }
  .gauge-color-row .lbl { grid-column: 1 / -1; grid-row: auto; max-width: none; justify-self: stretch; margin-top: 4px; }
  .gauge-label-row { grid-template-columns: minmax(110px, 1fr) 1.4fr; }
  .gauge-label-row .lbl { max-width: none; justify-self: stretch; }
}

/* ============ Utility ============ */
.hstack { display: flex; align-items: center; gap: var(--sp-2); }
.vstack { display: flex; flex-direction: column; gap: var(--sp-2); }
.mono { font-family: var(--mono); }
.small { font-size: 12px; }
.text-dim { color: var(--text-dim); }
.text-mid { color: var(--text-mid); }
.spacer { flex: 1; }

/* ============ Live stats / diag info ============ */
#liveStats, #diagInfo { margin-top: var(--sp-3); font-size: 12.5px; color: var(--text-mid); }
.stat-row { display: flex; justify-content: space-between; padding: 3px 0; }
.stat-val { color: var(--text); font-family: var(--mono); font-size: 12px; }

/* ============ Brand sliver in dark mode ============ */
.topbar { position: relative; }
.topbar::after {
  content: ""; position: absolute; left: 0; right: 0; bottom: -1px;
  height: 1px;
  background: linear-gradient(90deg, transparent, var(--accent) 30%, var(--accent) 70%, transparent);
  opacity: 0.0;
  transition: opacity var(--motion);
}
html[data-theme="dark"] .topbar::after { opacity: 0.5; }
</style>
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
      <div class="banner">
        <span class="dot" style="background:var(--info)"></span>
        <div>
          <strong>Token-based access.</strong>
          <div class="small text-dim" style="margin-top:4px">Token expires after ~90 days. It can also be invalidated earlier if you "log out everywhere" or change your password - paste a fresh one when that happens. Your password is NEVER stored on the device.</div>
        </div>
      </div>

      <div class="row">
        <div class="field">
          <label for="region">Server region</label>
          <select id="region">
            <option value="us" %REGION_US%>Americas (US)</option>
            <option value="eu" %REGION_EU%>Europe (EU)</option>
            <option value="cn" %REGION_CN%>China (CN)</option>
          </select>
        </div>
        <div class="field">
          <label>Cloud status</label>
          <div id="cloudStatus" class="hstack" style="height:36px;font-size:13px;color:var(--text-mid)">%CLOUD_STATUS%</div>
        </div>
      </div>

      <details open>
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
          <a href="https://github.com/Keralots/BambuHelper#getting-a-cloud-token" target="_blank" class="small" style="display:inline-block;margin-top:var(--sp-2);color:var(--accent)">Detailed instructions with screenshots</a>
        </div>
      </details>

      <div class="field" style="margin-top:var(--sp-4)">
        <label for="cl_token">Access token</label>
        <textarea id="cl_token" rows="3" placeholder="Paste your Bambu Cloud token here..."></textarea>
      </div>
      <div class="row">
        <div class="field">
          <label for="cl_serial">Printer serial number</label>
          <input type="text" id="cl_serial" class="mono" value="%SERIAL%" placeholder="01P00A000000000" maxlength="19">
          <button type="button" class="btn btn-ghost btn-sm" id="cl_scanBtn" onclick="scanLan('cloud')" style="margin-top:var(--sp-2)">Scan local network</button>
          <select id="cl_devsel" onchange="pickLanDevice('cloud')" style="display:none;margin-top:var(--sp-2);width:100%"></select>
          <div class="hint">Must match exactly (UPPERCASE). Scan finds it if the printer is on the same Wi-Fi; otherwise use Bambu Handy, the printer label, or the Companion Tool. A wrong serial connects but shows no data.</div>
        </div>
        <div class="field">
          <label for="cl_pname">Printer name</label>
          <input type="text" id="cl_pname" value="%PNAME%" placeholder="My Printer" maxlength="23">
        </div>
      </div>
      <div style="margin-top:var(--sp-3)">
        <button type="button" class="btn btn-danger btn-sm" id="cloudLogoutBtn" onclick="cloudLogout()">Clear Token</button>
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
        <p>Per-printer display slots. The standard 2x3 grid is always shown. On 240x320 and 320x480 boards the extra rows below only render when the matching grid mode (<em>Landscape 8 slots</em> / <em>Portrait 9 slots</em>) is enabled under <strong>Advanced</strong>. Set any slot to <em>Empty</em> to hide it.</p>
      </div>
      <button type="button" class="btn btn-ghost btn-sm" style="white-space:nowrap" onclick="resetGaugeLayout()">Reset to default</button>
    </div>
%AMSV_ROW%
    <div class="row-divider" style="margin-top:var(--sp-3)">&#9650; Top row</div>
    <div class="gauge-grid">
      <div class="cell"><label>Top-left</label><select id="gs0" class="gauge-slot-sel"></select></div>
      <div class="cell"><label>Top-center</label><select id="gs1" class="gauge-slot-sel"></select></div>
      <div class="cell"><label>Top-right</label><select id="gs2" class="gauge-slot-sel"></select></div>
    </div>
    <div id="bottomRowGroup">
      <div class="row-divider">&#9660; Bottom row</div>
      <div class="gauge-grid">
        <div class="cell"><label>Bot-left</label><select id="gs3" class="gauge-slot-sel"></select></div>
        <div class="cell"><label>Bot-center</label><select id="gs4" class="gauge-slot-sel"></select></div>
        <div class="cell"><label>Bot-right</label><select id="gs5" class="gauge-slot-sel"></select></div>
      </div>
    </div>
%EXTRAS_SECTIONS%
    <div class="action-bar">
      <button type="button" class="btn btn-primary" onclick="saveGaugeLayout()">Save Gauge Layout</button>
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
    <label class="check-row">
      <input type="checkbox" id="shtire" value="1" %SHTIRE% onchange="toggleSetting('shtire',this.checked)">
      <label for="shtire">Show remaining time instead of ETA</label>
    </label>
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
  </div>

  <div class="card">
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
    <label class="check-row">
      <input type="checkbox" id="dack" value="1" %DACK% onchange="toggleSetting('dack',this.checked)">
      <label for="dack">Wait for door open before timeout</label>
    </label>
    <label class="check-row">
      <input type="checkbox" id="kps" value="1" %KPS% onchange="toggleSetting('kps',this.checked)">
      <label for="kps">Keep print status screen after completion</label>
    </label>
    <div class="help-text" style="padding-left:28px">Show last print stats instead of the finish screen. Drying screen still takes priority.</div>
    <div class="field" style="margin-top:var(--sp-4)">
      <label class="hstack" style="justify-content:space-between" for="ssbright"><span>Screensaver brightness</span><span class="mono text-dim" id="ssbrightVal">%SSBRIGHT%</span></label>
      <input type="range" id="ssbright" min="0" max="255" step="5" value="%SSBRIGHT%"
             oninput="document.getElementById('ssbrightVal').textContent=this.value">
      <div class="hint">Brightness when clock/screensaver is active. Set to 0 to turn off backlight.</div>
    </div>
    <label class="check-row" id="pong-row">
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
        <div class="gauge-label-row"><span class="name">Door</span><input type="text" class="lbl" id="dor_lbl" maxlength="12" value="%DOR_LBL%" placeholder="empty = icon only" title="Status-bar door label. Leave empty to show just the padlock icon."></div>
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

<!-- ===== Section 3: Hardware ===== -->
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
    <div class="card-head"><div><h3>External LED</h3><p>PWM-dimmed LED via NPN transistor or MOSFET. CYD: GPIO 22 on P3 connector.</p></div></div>
    <div class="field">
      <label for="leden">External LED</label>
      <select id="leden" onchange="toggleLed();ledPreviewSend()">
        <option value="0" %LED_OFF%>Disabled</option>
        <option value="1" %LED_ON%>Enabled</option>
      </select>
    </div>
    <div id="ledFields" style="display:none">
      <div class="row">
        <div class="field">
          <label for="ledpin">LED GPIO pin</label>
          <input type="number" id="ledpin" class="mono" min="1" max="48" value="%LED_PIN%" onchange="ledPreviewSend()" style="max-width:120px">
        </div>
        <div class="field">
          <label class="hstack" style="justify-content:space-between" for="ledbr"><span>Brightness</span><span class="mono text-dim" id="ledbrVal">%LED_BR%</span></label>
          <input type="range" id="ledbr" min="0" max="255" step="5" value="%LED_BR%"
                 oninput="document.getElementById('ledbrVal').textContent=this.value;ledPreviewSend()">
        </div>
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
        <input type="checkbox" id="lederr" %LED_ERR%>
        <label for="lederr">Fast strobe on error</label>
      </label>
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

<!-- ===== Section 4: Advanced ===== -->
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

<!-- ===== Section 5: WiFi & System ===== -->
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
      <span class="mono small text-dim">current: %FW_VER%</span>
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
  </div>

  <p class="hint" style="margin-top:var(--sp-3)">Reboot and factory reset have moved to the <strong>Advanced</strong> section.</p>
</div>

<!-- ===== Section 6: Power Monitoring ===== -->
<div class="section" id="sec-power" hidden>
  <div class="section-intro">
    <h2>Power Monitoring</h2>
    <p>Show live power consumption from Tasmota smart plug(s) on the display. Configure auto power-off and energy tariff per plug.</p>
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
      <input type="checkbox" id="tsm_en" value="1">
      <label for="tsm_en">Enable power monitoring for this plug</label>
    </label>
    <div class="field" style="margin-top:var(--sp-3)">
      <label for="tsm_pt">Power plug type</label>
      <select id="tsm_pt" onchange="onPlugTypeChange()"><option value="0">Tasmota</option><option value="1">Shelly (Gen2/Gen3)</option></select>
    </div>
    <div class="help-text" id="tsm_shelly_hint" style="display:none">Shelly Gen2/Gen3 (same RPC API), and the plug must not be password-protected (digest auth is not supported). Shelly reports live watts and a cumulative Total, but does <strong>not</strong> report Today's / Yesterday's energy, so those stay blank.</div>
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

  <div class="card">
    <div class="card-head"><div><h3>Auto power-off</h3></div></div>
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

<!-- ===== Section 6: Diagnostics ===== -->
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

<script>
/* ============ Loader / section dispatcher ============ */
var SECTION_LABELS = {
  printer: 'Printer Settings',
  display: 'Display',
  hardware: 'Hardware',
  advanced: 'Advanced',
  wifi: 'WiFi & System',
  power: 'Power Monitoring',
  diag: 'Diagnostics'
};
var currentSection = null;
function loadSection(id){
  if (id === currentSection) return;
  var sections = document.querySelectorAll('.section');
  for (var i = 0; i < sections.length; i++) {
    var sid = sections[i].id;  // sec-printer etc.
    sections[i].hidden = (sid !== 'sec-' + id);
  }
  currentSection = id;
  var title = SECTION_LABELS[id] || id;
  document.title = 'BambuHelper - ' + title;
  var st = document.getElementById('sectionTitle');
  if (st) st.textContent = title;
  var navs = document.querySelectorAll('.nav-item');
  for (var j = 0; j < navs.length; j++) {
    navs[j].setAttribute('aria-current', navs[j].dataset.section === id ? 'true' : 'false');
  }
  document.getElementById('sidebar').classList.remove('open');
  document.getElementById('scrim').classList.remove('show');
  window.scrollTo(0, 0);
  try { localStorage.setItem('bambu_section', id); } catch(e){}
  if (location.hash !== '#' + id) {
    try { history.replaceState(null, '', '#' + id); } catch(e){}
  }
  startPolling(id);
}
var navBtns = document.querySelectorAll('.nav-item');
for (var b = 0; b < navBtns.length; b++){
  navBtns[b].addEventListener('click', function(){ loadSection(this.dataset.section); });
}

/* ============ Theme toggle ============ */
/* Pure DOM mutation. Persistence happens only on explicit user click below,
   so an OS-detected preference on first load is not stamped into storage
   (which would freeze it and stop auto-following OS theme changes). */
function applyThemeMode(theme){
  document.documentElement.setAttribute('data-theme', theme);
  document.getElementById('iconSun').style.display = (theme === 'dark') ? '' : 'none';
  document.getElementById('iconMoon').style.display = (theme === 'dark') ? 'none' : '';
}
document.getElementById('themeToggle').addEventListener('click', function(){
  var cur = document.documentElement.getAttribute('data-theme');
  var next = (cur === 'dark') ? 'light' : 'dark';
  applyThemeMode(next);
  try { localStorage.setItem('bh-theme', next); } catch(e){}
});

/* ============ Mobile drawer ============ */
document.getElementById('hamburger').addEventListener('click', function(){
  document.getElementById('sidebar').classList.toggle('open');
  document.getElementById('scrim').classList.toggle('show');
});
document.getElementById('scrim').addEventListener('click', function(){
  document.getElementById('sidebar').classList.remove('open');
  document.getElementById('scrim').classList.remove('show');
});

/* ============ Toast ============ */
var _toastTimer = null;
function showToast(msg){
  var t = document.getElementById('toast');
  t.textContent = msg || 'Applied!';
  t.classList.add('show');
  clearTimeout(_toastTimer);
  _toastTimer = setTimeout(function(){ t.classList.remove('show'); }, msg && msg.length > 40 ? 5000 : 2000);
}

/* ============ HTML escape ============ */
function esc(s){var d=document.createElement('div');d.appendChild(document.createTextNode(s));return d.innerHTML;}

/* ============ Polling ============ */
var diagTimer = null, statsTimer = null, powerTimer = null, hwTimer = null;
function startPolling(id){
  stopPolling();
  if (id === 'diag') { refreshDiag(); diagTimer = setInterval(refreshDiag, 5000); }
  if (id === 'printer') { refreshLiveStats(); statsTimer = setInterval(refreshLiveStats, 3000); }
  if (id === 'power') { refreshPowerStats(); powerTimer = setInterval(refreshPowerStats, 5000); }
  if (id === 'hardware' || id === 'wifi') { refreshHwInfo(); hwTimer = setInterval(refreshHwInfo, 5000); }
}
function stopPolling(){
  if (diagTimer) { clearInterval(diagTimer); diagTimer = null; }
  if (statsTimer) { clearInterval(statsTimer); statsTimer = null; }
  if (powerTimer) { clearInterval(powerTimer); powerTimer = null; }
  if (hwTimer) { clearInterval(hwTimer); hwTimer = null; }
}

/* ============ Brightness debounce ============ */
var _brightTimer = null;
function sendBrightness(val){ clearTimeout(_brightTimer); _brightTimer = setTimeout(function(){ fetch('/brightness?val=' + val); }, 150); }

/* ============ Gauge slot type labels (must match GaugeType enum in settings.h) ============
   Array index = numeric gauge ID. Empty `group` renders ungrouped at the top
   of the dropdown; named groups render as <optgroup> blocks in the order the
   first member of each group appears below. */
var gaugeTypes = [
  {name:'-- Empty --',        group:''},                  /*  0 */
  {name:'Progress',           group:'Print status'},      /*  1 */
  {name:'Nozzle Temp',        group:'Temperatures'},      /*  2 */
  {name:'Bed Temp',           group:'Temperatures'},      /*  3 */
  {name:'Part Fan',           group:'Fans'},              /*  4 */
  {name:'Aux Fan',            group:'Fans'},              /*  5 */
  {name:'Chamber Fan',        group:'Fans'},              /*  6 */
  {name:'Chamber Temp',       group:'Temperatures'},      /*  7 */
  {name:'Heatbreak Fan',      group:'Fans'},              /*  8 */
  {name:'Clock',              group:'Other'},             /*  9 */
  {name:'AMS 1 Humidity',     group:'AMS humidity'},      /* 10 */
  {name:'AMS 2 Humidity',     group:'AMS humidity'},      /* 11 */
  {name:'AMS 3 Humidity',     group:'AMS humidity'},      /* 12 */
  {name:'AMS 4 Humidity',     group:'AMS humidity'},      /* 13 */
  {name:'Layer Progress',     group:'Print status'},      /* 14 */
  {name:'AMS 1 Temp',         group:'AMS temperature'},   /* 15 */
  {name:'AMS 2 Temp',         group:'AMS temperature'},   /* 16 */
  {name:'AMS 3 Temp',         group:'AMS temperature'},   /* 17 */
  {name:'AMS 4 Temp',         group:'AMS temperature'},   /* 18 */
  {name:'AMS 1 Filament',     group:'AMS filament (quad)'},/* 19 */
  {name:'AMS 2 Filament',     group:'AMS filament (quad)'},/* 20 */
  {name:'AMS 3 Filament',     group:'AMS filament (quad)'},/* 21 */
  {name:'AMS 4 Filament',     group:'AMS filament (quad)'},/* 22 */
  {name:'Aux Fan Right (X2D)',group:'Fans'},              /* 23 */
  {name:'Exhaust Fan',        group:'Fans'},              /* 24 */
  {name:'AMS 1 Bars',         group:'AMS filament (bars)'},/* 25 */
  {name:'AMS 2 Bars',         group:'AMS filament (bars)'},/* 26 */
  {name:'AMS 3 Bars',         group:'AMS filament (bars)'},/* 27 */
  {name:'AMS 4 Bars',         group:'AMS filament (bars)'},/* 28 */
  {name:'Nozzle R Temp',      group:'Temperatures'},      /* 29 */
  {name:'Nozzle L Temp',      group:'Temperatures'},      /* 30 */
  {name:'Power',              group:'Other'},             /* 31 */
  {name:'Camera (P1/A1 LAN)', group:'Other'}              /* 32 */
];
var GAUGE_REQUIRES = { 23: 'hasAuxFanRight', 24: 'hasExhaustFan', 29: 'hasDualNozzle', 30: 'hasDualNozzle', 31: 'hasTasmota', 32: 'hasLanCamera' };
var gaugeCaps = {}, persistedGauges = {};
function gaugeAllowed(idx){
  if (persistedGauges[idx]) return true;
  var req = GAUGE_REQUIRES[idx];
  if (req) return !!gaugeCaps[req];
  return true;
}
function rebuildGaugeOptions(){
  // Build ungrouped + groups once, then clone into each <select>.
  // groups[] preserves first-seen order; groupMembers[g] is a list of {i, name}.
  var ungrouped = [];
  var groups = [];
  var groupMembers = {};
  gaugeTypes.forEach(function(gt, i){
    if (!gaugeAllowed(i)) return;
    if (!gt.group) { ungrouped.push({i:i, name:gt.name}); return; }
    if (!groupMembers[gt.group]) { groups.push(gt.group); groupMembers[gt.group] = []; }
    groupMembers[gt.group].push({i:i, name:gt.name});
  });
  var sels = document.querySelectorAll('.gauge-slot-sel');
  sels.forEach(function(sel){
    var cur = sel.value;
    sel.innerHTML = '';
    ungrouped.forEach(function(e){
      var o = document.createElement('option');
      o.value = e.i; o.textContent = e.name;
      sel.appendChild(o);
    });
    groups.forEach(function(g){
      var og = document.createElement('optgroup');
      og.label = g;
      groupMembers[g].forEach(function(e){
        var o = document.createElement('option');
        o.value = e.i; o.textContent = e.name;
        og.appendChild(o);
      });
      sel.appendChild(og);
    });
    if (cur !== '') sel.value = cur;
  });
}
rebuildGaugeOptions();
function refreshGaugeCaps(){
  Promise.all([0,1].map(function(slot){
    return fetch('/printer/config?slot=' + slot).then(function(r){return r.json();}).catch(function(){return {};});
  })).then(function(arr){
    var changed = false;
    Object.keys(GAUGE_REQUIRES).forEach(function(idx){
      var capName = GAUGE_REQUIRES[idx];
      var v = arr.some(function(d){ return d && d[capName]; });
      if (v !== !!gaugeCaps[capName]){ gaugeCaps[capName] = v; changed = true; }
    });
    arr.forEach(function(d){
      if (!d) return;
      ['gaugeSlots','landscapeExtras','portraitExtras'].forEach(function(key){
        if (!Array.isArray(d[key])) return;
        d[key].forEach(function(v){
          if (!persistedGauges[v]) { persistedGauges[v] = true; changed = true; }
        });
      });
    });
    if (changed) rebuildGaugeOptions();
  });
}
refreshGaugeCaps();

function resetGaugeLayout(){
  // Standard 2x3 grid -> Progress/Nozzle/Bed/PartFan/AuxFan/ChamberFan.
  // Both extras arrays default to Empty (user opts in via mode toggles).
  var d = [1,2,3,4,5,6];
  for (var i = 0; i < 6; i++) { var s = document.getElementById('gs' + i); if (s) s.value = d[i]; }
  var lx = ['lx0','lx1'], px = ['px0','px1','px2'];
  lx.forEach(function(id){ var s = document.getElementById(id); if (s) s.value = 0; });
  px.forEach(function(id){ var s = document.getElementById(id); if (s) s.value = 0; });
  showToast('Restored layout defaults');
}
function saveGaugeLayout(){
  var p = new URLSearchParams();
  p.append('slot', currentSlot);
  for (var g = 0; g < 6; g++) { var s = document.getElementById('gs' + g); if (s) p.append('gs' + g, s.value); }
  for (var g = 0; g < 2; g++) { var s = document.getElementById('lx' + g); if (s) p.append('lx' + g, s.value); }
  for (var g = 0; g < 3; g++) { var s = document.getElementById('px' + g); if (s) p.append('px' + g, s.value); }
  var av = document.getElementById('amsv');
  if (av && av.checked) p.append('amsv', '1');
  fetch('/save/gaugelayout',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok')showToast('Gauge layout saved!');else showToast('Error');})
    .catch(function(){showToast('Save failed');});
}

/* ============ Printer tabs ============ */
var currentSlot = 0;
function selectPrinterTab(slot){
  currentSlot = slot;
  var btns = document.querySelectorAll('.tab-btn');
  for (var i = 0; i < btns.length; i++) btns[i].classList.toggle('active', i === slot);
  var reqSlot = slot;
  fetch('/printer/config?slot=' + slot).then(function(r){return r.json();}).then(function(d){
    if (reqSlot !== currentSlot) return;
    document.getElementById('connmode').value = d.mode;
    document.getElementById('pname').value = d.name || '';
    document.getElementById('ip').value = d.ip || '';
    document.getElementById('serial').value = d.serial || '';
    document.getElementById('code').value = d.code || '';
    document.getElementById('cl_serial').value = d.serial || '';
    document.getElementById('cl_pname').value = d.name || '';
    document.getElementById('region').value = d.region || 'us';
    document.getElementById('cl_token').value = '';
    var capsChanged = false;
    Object.keys(GAUGE_REQUIRES).forEach(function(idx){
      var capName = GAUGE_REQUIRES[idx];
      if (d[capName] && !gaugeCaps[capName]){ gaugeCaps[capName] = true; capsChanged = true; }
    });
    if (d.gaugeSlots){
      d.gaugeSlots.forEach(function(v){
        if (!persistedGauges[v]){ persistedGauges[v] = true; capsChanged = true; }
      });
    }
    if (capsChanged) rebuildGaugeOptions();
    if (d.gaugeSlots) { for (var g = 0; g < 6; g++) { var sel = document.getElementById('gs' + g); if (sel) sel.value = d.gaugeSlots[g] || 0; } }
    if (d.landscapeExtras) { for (var g = 0; g < 2; g++) { var sel = document.getElementById('lx' + g); if (sel) sel.value = d.landscapeExtras[g] || 0; } }
    if (d.portraitExtras)  { for (var g = 0; g < 3; g++) { var sel = document.getElementById('px' + g); if (sel) sel.value = d.portraitExtras[g] || 0; } }
    var av = document.getElementById('amsv');
    if (av) { av.checked = !!d.amsView; syncAmsView(); }
    toggleConnMode();
    var ps = document.getElementById('printerStatus');
    if (d.connected) { ps.className = 'status-pill status-ok'; ps.textContent = 'Connected'; }
    else if (d.configured) { ps.className = 'status-pill status-off'; ps.textContent = 'Disconnected'; }
    else { ps.className = 'status-pill status-na'; ps.textContent = 'Not Configured'; }
  }).catch(function(e){console.warn('selectPrinterTab:', e);});
}

function syncAmsView(){
  var cb = document.getElementById('amsv');
  var bg = document.getElementById('bottomRowGroup');
  if (bg) bg.style.display = (cb && cb.checked) ? 'none' : '';
}

/* ============ Misc ============ */
function readJsonResponse(r){
  return r.text().then(function(text){
    var data = {};
    if (text) { try { data = JSON.parse(text); } catch(e) { data = {message: text}; } }
    data._httpOk = r.ok; data._httpStatus = r.status;
    return data;
  });
}
function isValidIpv4(v){
  if (!v) return false;
  var m = v.match(/^(\d{1,3})(\.\d{1,3}){3}$/);
  if (!m) return false;
  var parts = v.split('.');
  for (var i = 0; i < parts.length; i++) { var n = parseInt(parts[i], 10); if (n < 0 || n > 255) return false; }
  return true;
}
function toggleStatic(){
  var m = document.getElementById('netmode').value;
  document.getElementById('staticFields').style.display = (m === 'static') ? 'block' : 'none';
}
function toggleConnMode(){
  var v = document.getElementById('connmode').value;
  var cloud = (v === 'cloud_all');
  document.getElementById('localFields').style.display = cloud ? 'none' : 'block';
  document.getElementById('cloudFields').style.display = cloud ? 'block' : 'none';
}

/* ============ Save printer ============ */
function savePrinter(){
  var p = new URLSearchParams();
  p.append('slot', currentSlot);
  var mode = document.getElementById('connmode').value;
  var nameField = mode === 'cloud_all' ? document.getElementById('cl_pname') : document.getElementById('pname');
  var serialField = mode === 'cloud_all' ? document.getElementById('cl_serial') : document.getElementById('serial');
  var serial = serialField.value.trim().toUpperCase();
  // Fallback: if the field is empty but a scan dropdown still has a selection,
  // use it (covers the case where the field got cleared after picking).
  if (serial.length === 0){
    var dsel = document.getElementById(mode === 'cloud_all' ? 'cl_devsel' : 'lan_devsel');
    if (dsel && dsel.value){ serial = dsel.value.trim().toUpperCase(); serialField.value = serial; }
  }
  var token = document.getElementById('cl_token').value.trim();
  if (nameField && nameField.value.trim().length === 0) { showToast('Printer name is required'); return; }
  if (serial.length === 0) { showToast(mode === 'cloud_all' ? 'Cloud mode requires a printer serial number' : 'LAN mode requires a printer serial number'); return; }
  p.append('connmode', mode);
  if (mode === 'cloud_all'){
    var cloudStatus = document.getElementById('cloudStatus').textContent || '';
    if (token.length === 0 && cloudStatus.indexOf('Token active') === -1) { showToast('Cloud mode requires a valid token'); return; }
    p.append('serial', serial);
    p.append('pname', nameField.value.trim());
    p.append('region', document.getElementById('region').value);
    if (token) p.append('token', token);
  } else {
    var ip = document.getElementById('ip').value.trim();
    var code = document.getElementById('code').value.trim();
    if (ip.length === 0) { showToast('LAN mode requires a printer IP address'); return; }
    if (!isValidIpv4(ip)) { showToast('Printer IP address is not valid'); return; }
    if (code.length > 0 && code.length !== 8) { showToast('LAN access code should be 8 characters'); return; }
    p.append('pname', nameField.value.trim());
    p.append('ip', ip);
    p.append('serial', serial);
    p.append('code', code);
  }
  fetch('/save/printer',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(readJsonResponse)
    .then(function(d){
      if (d.status === 'ok' && d.warning) showToast('Saved with warning: ' + d.warning);
      else if (d.status === 'ok') showToast('Printer settings saved');
      else if (d.message) showToast('Save failed: ' + d.message);
      else showToast('Save failed');
    })
    .catch(function(e){showToast('Save failed: network error');console.warn('savePrinter:',e);});
}

function cloudLogout(){
  fetch('/cloud/logout',{method:'POST'}).then(function(){
    var cs = document.getElementById('cloudStatus');
    cs.style.color = 'var(--text-mid)'; cs.textContent = 'No token set';
    document.getElementById('cl_token').value = '';
  });
}

function clearPrinter(){
  if (!confirm('Clear all settings for printer ' + (currentSlot + 1) + '? Connection details are removed and the gauge layout resets to defaults. The cloud token is shared and stays (use Clear Token to remove it).')) return;
  var p = new URLSearchParams();
  p.append('slot', currentSlot);
  fetch('/printer/clear',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(readJsonResponse)
    .then(function(d){
      if (d.status === 'ok'){ showToast('Printer ' + (currentSlot + 1) + ' cleared'); selectPrinterTab(currentSlot); }
      else showToast('Clear failed');
    })
    .catch(function(e){showToast('Clear failed: network error');console.warn('clearPrinter:',e);});
}

/* ============ SSDP local-network printer scan ============ */
var lanScanRunId = 0;
function scanLan(mode){
  var btn = document.getElementById(mode === 'lan' ? 'lan_scanBtn' : 'cl_scanBtn');
  var sel = document.getElementById(mode === 'lan' ? 'lan_devsel' : 'cl_devsel');
  sel.innerHTML = ''; sel.style.display = 'none';
  // Guard against results landing after the user switches tab/mode mid-scan.
  var slotAtStart = currentSlot;
  var modeAtStart = document.getElementById('connmode').value;
  var myRun = ++lanScanRunId;
  function stale(){
    return myRun !== lanScanRunId || currentSlot !== slotAtStart ||
           document.getElementById('connmode').value !== modeAtStart;
  }
  function restore(){ btn.disabled = false; btn.textContent = 'Scan local network'; }
  btn.disabled = true; btn.textContent = 'Scanning...';
  fetch('/lan/scan',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    if (stale()){ restore(); return; }
    if (d.status === 'error'){ showToast(d.msg || 'Scan failed'); restore(); return; }
    var deadline = Date.now() + 13000;  // a hair past the device's 12s scan window
    (function poll(){
      if (stale()){ restore(); return; }
      fetch('/lan/scan').then(function(r){return r.json();}).then(function(d){
        if (stale()){ restore(); return; }
        var devs = d.devices || [];
        if (d.status === 'done' || Date.now() > deadline){
          renderLanDevices(mode, sel, devs); restore();
        } else {
          setTimeout(poll, 1000);
        }
      }).catch(function(e){ showToast('Scan failed: network error'); restore(); console.warn('scanLan poll:', e); });
    })();
  }).catch(function(e){ showToast('Scan failed: network error'); restore(); console.warn('scanLan start:', e); });
}

function renderLanDevices(mode, sel, devs){
  if (devs.length === 0){ showToast('No printers found on the LAN. Same Wi-Fi/subnet required; or type it manually.'); return; }
  if (devs.length === 1){
    fillLanDevice(mode, devs[0]);
    showToast('Found ' + (devs[0].name || devs[0].serial) + (devs[0].model ? ' (' + devs[0].model + ')' : ''));
    return;
  }
  var ph = document.createElement('option');
  ph.value = ''; ph.textContent = 'Select a printer (' + devs.length + ' found)...';
  sel.appendChild(ph);
  devs.forEach(function(dev){
    var o = document.createElement('option');
    o.value = (dev.serial || '').toUpperCase();
    o.setAttribute('data-ip', dev.ip || '');
    o.textContent = (dev.name || dev.serial) + (dev.model ? ' (' + dev.model + ')' : '') +
                    ' - ' + o.value + (dev.ip ? ' @ ' + dev.ip : '');
    sel.appendChild(o);
  });
  sel.style.display = '';
  showToast(devs.length + ' printers found - pick one');
}

function fillLanDevice(mode, dev){
  var serial = (dev.serial || '').toUpperCase();
  if (mode === 'lan'){
    document.getElementById('serial').value = serial;
    if (dev.ip) document.getElementById('ip').value = dev.ip;
  } else {
    document.getElementById('cl_serial').value = serial;
  }
}

function pickLanDevice(mode){
  var sel = document.getElementById(mode === 'lan' ? 'lan_devsel' : 'cl_devsel');
  if (!sel.value) return;
  var opt = sel.options[sel.selectedIndex];
  fillLanDevice(mode, { serial: sel.value, ip: opt.getAttribute('data-ip') });
}

/* ============ Save WiFi ============ */
function saveWifi(){
  var ssid = document.getElementById('ssid').value.trim();
  var netmode = document.getElementById('netmode').value;
  var ip = document.getElementById('net_ip').value.trim();
  var gw = document.getElementById('net_gw').value.trim();
  var sn = document.getElementById('net_sn').value.trim();
  var dns = document.getElementById('net_dns').value.trim();
  if (!ssid) { showToast('WiFi SSID is required'); return; }
  if (netmode === 'static'){
    if (!ip || !gw || !sn) { showToast('Static IP mode requires IP, gateway, and subnet mask'); return; }
    if (!isValidIpv4(ip)) { showToast('Static IP address is not valid'); return; }
    if (!isValidIpv4(gw)) { showToast('Gateway address is not valid'); return; }
    if (!isValidIpv4(sn)) { showToast('Subnet mask is not valid'); return; }
    if (dns && !isValidIpv4(dns)) { showToast('DNS server address is not valid'); return; }
  }
  var p = new URLSearchParams();
  p.append('ssid', ssid);
  p.append('pass', document.getElementById('pass').value);
  p.append('netmode', netmode);
  p.append('net_ip', ip);
  p.append('net_gw', gw);
  p.append('net_sn', sn);
  p.append('net_dns', dns);
  p.append('has_showip', '1');
  if (document.getElementById('showip').checked) p.append('showip', '1');
  var host = document.getElementById('mdns_host').value.trim().toLowerCase()
               .replace(/[^a-z0-9-]/g,'').replace(/^-+|-+$/g,'');
  if (!host) host = 'bambuhelper';
  p.append('has_mdns', '1');
  p.append('mdns_host', host);
  if (document.getElementById('mdns_en').checked) p.append('mdns_en', '1');
  fetch('/save/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(readJsonResponse)
    .then(function(d){
      if (d.status && d.status !== 'ok') { throw new Error(d.message || 'settings were not accepted'); }
      document.body.innerHTML = '<div style="text-align:center;padding-top:80px;font-family:sans-serif"><h2 style="color:#3FB950">WiFi Saved!</h2><p style="color:#8B949E;margin-top:10px">Restarting...</p></div>';
    })
    .catch(function(e){showToast('WiFi save failed: ' + (e && e.message ? e.message : 'network error'));console.warn('saveWifi:', e);});
}

/* ============ Hardware ============ */
function toggleBtnPin(){
  var v = document.getElementById('btntype').value;
  document.getElementById('btnPinRow').style.display = (v === '0' || v === '3') ? 'none' : 'block';
  toggleBuzPin();
}
function toggleBuzPin(){
  var buzOn = document.getElementById('buzzen').value !== '0';
  document.getElementById('buzFields').style.display = buzOn ? 'block' : 'none';
  var isES8311 = '%ES8311_AUDIO%' === '1';
  document.getElementById('buzPinRow').style.display = (buzOn && !isES8311) ? 'block' : 'none';
  document.getElementById('buzEs8311Info').style.display = (buzOn && isES8311) ? 'block' : 'none';
  var btnOn = document.getElementById('btntype').value !== '0';
  document.getElementById('buzClickRow').style.display = (buzOn && btnOn) ? 'flex' : 'none';
}
function toggleLed(){
  document.getElementById('ledFields').style.display = document.getElementById('leden').value !== '0' ? 'block' : 'none';
  toggleLedFx();
}
function toggleLedFx(){
  var fx = document.getElementById('ledfxmd');
  if (!fx) return;
  document.getElementById('ledFxParams').style.display = fx.value !== '0' ? 'block' : 'none';
}
function ledPreviewSend(){
  var p = new URLSearchParams();
  p.append('en', document.getElementById('leden').value);
  p.append('pin', document.getElementById('ledpin').value);
  p.append('br', document.getElementById('ledbr').value);
  fetch('/led/preview',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()}).catch(function(){});
}
function ledTestEffect(){
  var p = new URLSearchParams();
  p.append('md', document.getElementById('ledfxmd').value);
  p.append('sec', document.getElementById('ledfxsec').value);
  p.append('br', document.getElementById('ledfxbr').value);
  fetch('/led/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok')showToast('LED effect test running');else if(d.error)showToast('Test failed: '+d.error);})
    .catch(function(e){showToast('LED test failed');console.warn('ledTestEffect:', e);});
}
var buzTestSounds = [{id:0,name:'Print Finished'},{id:1,name:'Error'},{id:2,name:'Connected'},{id:4,name:'Bed Cooled'}];
var buzTestIdx = 0;
function testBuzzer(){
  var snd = buzTestSounds[buzTestIdx];
  fetch('/buzzer/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'sound='+snd.id})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok')showToast('Playing: '+snd.name);})
    .catch(function(e){showToast('Buzzer test failed');console.warn('testBuzzer:',e);});
  buzTestIdx = (buzTestIdx + 1) % buzTestSounds.length;
  document.getElementById('buzTestBtn').textContent = 'Test: ' + buzTestSounds[buzTestIdx].name;
}
function saveRotation(){
  var p = new URLSearchParams();
  p.append('rotmode', document.getElementById('rotmode').value);
  p.append('rotinterval', document.getElementById('rotinterval').value);
  p.append('rotsplit', document.getElementById('rotsplit').checked ? '1' : '0');
  p.append('rotsplitf', document.getElementById('rotsplitf').checked ? '1' : '0');
  p.append('btntype', document.getElementById('btntype').value);
  p.append('btnpin', document.getElementById('btnpin').value);
  p.append('buzzen', document.getElementById('buzzen').value);
  p.append('buzpin', document.getElementById('buzpin').value);
  p.append('buzqs', document.getElementById('buzqs').value);
  p.append('buzqe', document.getElementById('buzqe').value);
  p.append('buzclick', document.getElementById('buzclick').checked ? '1' : '0');
  p.append('buzbeden', document.getElementById('buzbeden').checked ? '1' : '0');
  p.append('buzbedtemp', document.getElementById('buzbedtemp').value);
  p.append('leden', document.getElementById('leden').value);
  p.append('ledpin', document.getElementById('ledpin').value);
  p.append('ledbr', document.getElementById('ledbr').value);
  p.append('ledfxmd', document.getElementById('ledfxmd').value);
  p.append('ledfxsec', document.getElementById('ledfxsec').value);
  p.append('ledfxbr', document.getElementById('ledfxbr').value);
  p.append('ledauto', document.getElementById('ledauto').checked ? '1' : '0');
  p.append('ledpause', document.getElementById('ledpause').checked ? '1' : '0');
  p.append('lederr', document.getElementById('lederr').checked ? '1' : '0');
  var bs = document.getElementById('batshow');
  if (bs) p.append('batshow', bs.checked ? '1' : '0');
  fetch('/save/rotation',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok')showToast('Settings saved');})
    .catch(function(e){showToast('Save failed');console.warn('saveRotation:',e);});
}

/* Cap custom gauge label inputs: 8 chars with the normal (big) font, 12 when
   "Smaller gauge labels" is on. Non-destructive - only limits new typing; any
   already-stored longer label is kept and just trimmed on the device display. */
function applyLabelMaxlen(){
  var slbl = document.getElementById('slbl');
  var max = (slbl && slbl.checked) ? 12 : 8;
  var inputs = document.querySelectorAll('input.lbl');
  for (var i = 0; i < inputs.length; i++) inputs[i].maxLength = max;
}

/* ============ Power monitoring ============ */
var currentPowerPlug = 0;
var powerPlugCount = (document.getElementById('ptab1') ? 2 : 1);
/* Grey out the display-mode radios when the status-bar readout is hidden
   (Display tab) - the whole readout is gone, so the choice does nothing. */
function applyHideReadoutToPowerDM(){
  var hp = document.getElementById('hidelp');
  var hidden = !!(hp && hp.checked);
  var dm = document.querySelectorAll('input[name="tsm_dm"]');
  for (var i = 0; i < dm.length; i++) dm[i].disabled = hidden;
  var note = document.getElementById('dmHiddenNote');
  if (note) note.style.display = hidden ? 'block' : 'none';
}
function selectPowerTab(plug){
  if (plug >= powerPlugCount) return;
  currentPowerPlug = plug;
  for (var i = 0; i < 2; i++){
    var btn = document.getElementById('ptab' + i);
    if (!btn) continue;
    btn.classList.toggle('active', i === plug);
  }
  fetch('/power/config?plug=' + plug).then(function(r){return r.json();}).then(function(d){
    if (plug !== currentPowerPlug) return;
    document.getElementById('tsm_en').checked = !!d.enabled;
    document.getElementById('tsm_pt').value = (d.plugType === 1) ? '1' : '0';
    onPlugTypeChange();
    document.getElementById('tsm_ip').value = d.ip || '';
    var dm = document.querySelectorAll('input[name="tsm_dm"]');
    for (var j = 0; j < dm.length; j++) dm[j].checked = (parseInt(dm[j].value) === (d.displayMode || 0));
    applyHideReadoutToPowerDM();
    var slotSel = document.getElementById('tsm_slot');
    if (slotSel && typeof d.assignedSlot !== 'undefined') slotSel.value = d.assignedSlot;
    document.getElementById('tsm_pi').value = d.pollInterval || 10;
    document.getElementById('tsm_ao').checked = !!d.autoOffEnabled;
    document.getElementById('tsm_ad').value = d.autoOffDelayMin || 10;
    document.getElementById('tsm_aod').checked = !!d.autoOffCancelOnDoor;
    if (typeof d.tariff === 'number') document.getElementById('tsm_tar').value = d.tariff;
    if (typeof d.currency === 'string') document.getElementById('tsm_cur').value = d.currency;
    refreshPowerStats();
  }).catch(function(e){console.warn('selectPowerTab:',e);});
}
function onPlugTypeChange(){
  var shelly = (document.getElementById('tsm_pt').value === '1');
  var hint = document.getElementById('tsm_shelly_hint');
  if (hint) hint.style.display = shelly ? '' : 'none';
  // Shelly has no Today odometer - hide the row (label + value) for it.
  var tdLbl = document.getElementById('ptTodayLabel');
  var tdVal = document.getElementById('ptToday');
  if (tdLbl) tdLbl.style.display = shelly ? 'none' : '';
  if (tdVal) tdVal.style.display = shelly ? 'none' : '';
}
function fmtKwh(v){ return (v >= 0) ? (v.toFixed(3) + ' kWh') : '-'; }
function fmtMoney(v, cur){ if (!(v >= 0) || !cur) return ''; return ' (' + v.toFixed(2) + ' ' + cur + ')'; }
function refreshPowerStats(){
  fetch('/power/stats').then(function(r){return r.json();}).then(function(arr){
    if (!arr || !arr[currentPowerPlug]) return;
    var s = arr[currentPowerPlug];
    var cur = document.getElementById('tsm_cur').value || '';
    var tar = parseFloat(document.getElementById('tsm_tar').value) || 0;
    var dot = document.getElementById('ptStatusDot');
    dot.textContent = s.online ? '(online)' : '(offline)';
    dot.style.color = s.online ? 'var(--success)' : 'var(--text-dim)';
    document.getElementById('ptThis').textContent = fmtKwh(s.thisPrint) + (s.thisPrint >= 0 ? fmtMoney(s.thisPrint * tar, cur) : '');
    document.getElementById('ptToday').textContent = fmtKwh(s.today) + (s.today >= 0 ? fmtMoney(s.today * tar, cur) : '');
    document.getElementById('ptTotal').textContent = fmtKwh(s.total) + (s.total >= 0 ? fmtMoney(s.total * tar, cur) : '');
    document.getElementById('ptWatts').textContent = (s.online && s.watts >= 0) ? (s.watts.toFixed(0) + ' W') : '-';
    // Prefer the real relay state when the plug reports it (Shelly output);
    // otherwise fall back to watt inference (Tasmota has no state field).
    var on = s.stateKnown ? !!s.on : (s.online && s.watts > 0.5);
    document.getElementById('btnPowerOn').style.display = on ? 'none' : '';
    document.getElementById('btnPowerOff').style.display = on ? '' : 'none';
  }).catch(function(){});
}
function powerControl(on){
  var label = on ? 'Power ON' : 'Power OFF';
  if (!confirm(label + ' plug ' + (currentPowerPlug + 1) + ' now?')) return;
  var p = new URLSearchParams();
  p.append('plug', String(currentPowerPlug));
  p.append('on', on ? '1' : '0');
  fetch('/power/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json().then(function(d){return {ok:r.ok,d:d};});})
    .then(function(res){
      if (res.ok && res.d.status === 'ok') { showToast(label + ' sent'); setTimeout(refreshPowerStats, 800); }
      else { showToast((res.d && res.d.message) || (label + ' failed')); }
    })
    .catch(function(e){showToast(label + ' failed');console.warn('powerControl:',e);});
}
function savePower(){
  var p = new URLSearchParams();
  p.append('plug', String(currentPowerPlug));
  p.append('tsm_en', document.getElementById('tsm_en').checked ? '1' : '0');
  p.append('tsm_pt', document.getElementById('tsm_pt').value);
  p.append('tsm_ip', document.getElementById('tsm_ip').value.trim());
  var dm = document.querySelector('input[name="tsm_dm"]:checked');
  if (dm) p.append('tsm_dm', dm.value);
  p.append('tsm_pi', document.getElementById('tsm_pi').value);
  p.append('tsm_ao', document.getElementById('tsm_ao').checked ? '1' : '0');
  p.append('tsm_ad', document.getElementById('tsm_ad').value);
  p.append('tsm_aod', document.getElementById('tsm_aod').checked ? '1' : '0');
  p.append('tsm_tar', document.getElementById('tsm_tar').value);
  p.append('tsm_cur', document.getElementById('tsm_cur').value);
  var slotSel = document.getElementById('tsm_slot');
  if (slotSel) p.append('tsm_slot', slotSel.value);
  fetch('/save/power',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok')showToast('Power settings saved');})
    .catch(function(e){showToast('Save failed');console.warn('savePower:',e);});
}

/* ============ Display ============ */
var GAUGE_KEYS = ['prg','noz','bed','pfn','afn','afr','cfn','exh','cht','hbk','pwr','lyr'];
// Label override keys = the 12 colour gauges + Clock + AMS + Nozzle L/R (label-only rows).
var GAUGE_LABEL_KEYS = GAUGE_KEYS.concat(['clk','ams','nzr','nzl','dor']);
var themes = {
  default:{bg:'#081018',track:'#182028',clkt:'#FFFFFF',clkd:'#C0C0C0',prg:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},noz:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},pfn:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},afn:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},afr:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},cfn:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},exh:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},cht:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},hbk:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},pwr:{a:'#FFD600',l:'#FFD600',v:'#FFFFFF'},lyr:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'}},
  mono_green:{bg:'#000800',track:'#0A1A0A',clkt:'#00FF41',clkd:'#00CC33',prg:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},noz:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},bed:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},pfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},afn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},afr:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},cfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},exh:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},cht:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},hbk:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},pwr:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},lyr:{a:'#00FF41',l:'#00CC33',v:'#00FF41'}},
  neon:{bg:'#0A0014',track:'#1A0A2E',clkt:'#FF00FF',clkd:'#AA00FF',prg:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},noz:{a:'#FF4400',l:'#FF6600',v:'#FFFFFF'},bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},pfn:{a:'#00FF88',l:'#00FF88',v:'#FFFFFF'},afn:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},afr:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},cfn:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},exh:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},cht:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},hbk:{a:'#FF4400',l:'#FF6600',v:'#FFFFFF'},pwr:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},lyr:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'}},
  warm:{bg:'#140A00',track:'#2E1A08',clkt:'#FFEEDD',clkd:'#FFB347',prg:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},noz:{a:'#FF6347',l:'#FF6347',v:'#FFEEDD'},bed:{a:'#FFA500',l:'#FFA500',v:'#FFEEDD'},pfn:{a:'#FFD700',l:'#FFD700',v:'#FFEEDD'},afn:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},afr:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},cfn:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},exh:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},cht:{a:'#FFA500',l:'#FFA500',v:'#FFEEDD'},hbk:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},pwr:{a:'#FFD700',l:'#FFD700',v:'#FFEEDD'},lyr:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'}},
  ocean:{bg:'#000A14',track:'#0A1A2E',clkt:'#E0F0FF',clkd:'#00BFFF',prg:{a:'#00BFFF',l:'#00BFFF',v:'#E0F0FF'},noz:{a:'#FF7F50',l:'#FF7F50',v:'#E0F0FF'},bed:{a:'#4169E1',l:'#4169E1',v:'#E0F0FF'},pfn:{a:'#00CED1',l:'#00CED1',v:'#E0F0FF'},afn:{a:'#48D1CC',l:'#48D1CC',v:'#E0F0FF'},afr:{a:'#48D1CC',l:'#48D1CC',v:'#E0F0FF'},cfn:{a:'#20B2AA',l:'#20B2AA',v:'#E0F0FF'},exh:{a:'#20B2AA',l:'#20B2AA',v:'#E0F0FF'},cht:{a:'#4169E1',l:'#4169E1',v:'#E0F0FF'},hbk:{a:'#FF7F50',l:'#FF7F50',v:'#E0F0FF'},pwr:{a:'#FFD700',l:'#FFD700',v:'#E0F0FF'},lyr:{a:'#00BFFF',l:'#00BFFF',v:'#E0F0FF'}}
};
function applyTheme(name){
  var t = themes[name]; if (!t) return;
  document.getElementById('clr_bg').value = t.bg;
  document.getElementById('clr_track').value = t.track;
  document.getElementById('clr_pbar').value = t.prg.a;
  document.getElementById('clk_time').value = t.clkt;
  document.getElementById('clk_date').value = t.clkd;
  for (var i = 0; i < GAUGE_KEYS.length; i++){
    var k = GAUGE_KEYS[i], c = t[k];
    if (!c) continue;
    document.getElementById(k + '_a').value = c.a;
    document.getElementById(k + '_l').value = c.l;
    document.getElementById(k + '_v').value = c.v;
  }
  applyDisplay();
}
function bulkSet(suffix, color){
  for (var i = 0; i < GAUGE_KEYS.length; i++) document.getElementById(GAUGE_KEYS[i] + '_' + suffix).value = color;
}
var DEFAULT_GAUGES = {
  prg:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},noz:{a:'#FF7D00',l:'#FF7D00',v:'#FFFFFF'},bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},pfn:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},afn:{a:'#FF7D00',l:'#FF7D00',v:'#FFFFFF'},afr:{a:'#FF7D00',l:'#FF7D00',v:'#FFFFFF'},cfn:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},exh:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},cht:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},hbk:{a:'#FF7D00',l:'#FF7D00',v:'#FFFFFF'},pwr:{a:'#FFD600',l:'#FFD600',v:'#FFFFFF'},lyr:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'}
};
function resetGaugeColors(){
  for (var i = 0; i < GAUGE_KEYS.length; i++){
    var k = GAUGE_KEYS[i], c = DEFAULT_GAUGES[k];
    document.getElementById(k + '_a').value = c.a;
    document.getElementById(k + '_l').value = c.l;
    document.getElementById(k + '_v').value = c.v;
  }
  document.getElementById('clr_pbar').value = document.getElementById('prg_a').value;
  applyDisplay();
}
function clearGaugeLabels(){
  for (var i = 0; i < GAUGE_LABEL_KEYS.length; i++){
    document.getElementById(GAUGE_LABEL_KEYS[i] + '_lbl').value = '';
  }
  applyDisplay();
}
function randomGaugeColors(){
  var baseH = Math.floor(Math.random() * 360);
  var hueStep = 360 / GAUGE_KEYS.length;
  for (var i = 0; i < GAUGE_KEYS.length; i++){
    var h = (baseH + i * hueStep) % 360, hex = hslToHex(h, 70, 55);
    document.getElementById(GAUGE_KEYS[i] + '_a').value = hex;
    document.getElementById(GAUGE_KEYS[i] + '_l').value = hex;
    document.getElementById(GAUGE_KEYS[i] + '_v').value = '#FFFFFF';
  }
  // Give the progress bar its own hue, kept 60-300 deg from the Progress gauge
  // so it never matches the first gauge.
  var pbarH = (baseH + 60 + Math.floor(Math.random() * 240)) % 360;
  document.getElementById('clr_pbar').value = hslToHex(pbarH, 70, 55);
  applyDisplay();
}
function hslToHex(h, s, l){
  s /= 100; l /= 100;
  var c = (1 - Math.abs(2 * l - 1)) * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = l - c / 2;
  var r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
  function h2(v){ return ('0' + Math.round((v + m) * 255).toString(16)).slice(-2); }
  return '#' + h2(r) + h2(g) + h2(b);
}
function applyDisplay(){
  var p = new URLSearchParams();
  p.append('bright', document.getElementById('bright').value);
  if (document.getElementById('nighten').checked) p.append('nighten', '1');
  p.append('nstart', document.getElementById('nstart').value);
  p.append('nend', document.getElementById('nend').value);
  p.append('nbright', document.getElementById('nbright').value);
  p.append('ssbright', document.getElementById('ssbright').value);
  p.append('rotation', document.getElementById('rotation').value);
  var ap = document.getElementById('afterprint').value;
  if (ap === 'keepon') { p.append('keepon', '1'); p.append('fmins', '0'); }
  else if (ap === 'custom') { p.append('fmins', document.getElementById('fmins').value); p.append('clock', '1'); }
  else { p.append('fmins', ap); p.append('clock', '1'); }
  if (document.getElementById('dack').checked) p.append('dack', '1');
  if (document.getElementById('kps').checked) p.append('kps', '1');
  if (document.getElementById('abar').checked) p.append('abar', '1');
  if (document.getElementById('pong').checked) p.append('pong', '1');
  if (document.getElementById('slbl').checked) p.append('slbl', '1');
  if (document.getElementById('shtire').checked) p.append('shtire', '1');
  if (document.getElementById('fanmp').checked) p.append('fanmp', '1');
  p.append('tz', document.getElementById('tz').value);
  if (document.getElementById('use24h').checked) p.append('use24h', '1');
  p.append('datefmt', document.getElementById('datefmt').value);
  p.append('clr_bg', document.getElementById('clr_bg').value);
  p.append('clr_track', document.getElementById('clr_track').value);
  p.append('clr_pbar', document.getElementById('clr_pbar').value);
  p.append('clk_time', document.getElementById('clk_time').value);
  p.append('clk_date', document.getElementById('clk_date').value);
  p.append('clk_size', document.getElementById('clk_size').value);
  if (document.getElementById('clk_hidedate').checked) p.append('clk_hidedate', '1');
  p.append('noz_max', document.getElementById('noz_max').value);
  p.append('bed_max', document.getElementById('bed_max').value);
  p.append('cht_max', document.getElementById('cht_max').value);
  p.append('pwr_max', document.getElementById('pwr_max').value);
  p.append('gsmooth', document.getElementById('gsmooth').value);
  p.append('warn_thr', document.getElementById('warn_thr').value);
  p.append('warn_clr', document.getElementById('warn_clr').value);
  for (var i = 0; i < GAUGE_KEYS.length; i++){
    var k = GAUGE_KEYS[i];
    p.append(k + '_a', document.getElementById(k + '_a').value);
    p.append(k + '_l', document.getElementById(k + '_l').value);
    p.append(k + '_v', document.getElementById(k + '_v').value);
  }
  for (var j = 0; j < GAUGE_LABEL_KEYS.length; j++){
    var lk = GAUGE_LABEL_KEYS[j];
    p.append(lk + '_lbl', document.getElementById(lk + '_lbl').value);
  }
  fetch('/apply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){ if (r.ok) showToast('Applied!'); else showToast('Error'); })
    .catch(function(e){showToast('Apply failed');console.warn('applyDisplay:',e);});
}

/* ============ Whitelisted toggle ============ */
function toggleSetting(key, on){
  fetch('/save/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key='+key+'&val='+(on?'1':'0')})
    .then(function(r){if(r.ok)showToast(on?key+' ON':key+' OFF');else showToast('Error');})
    .catch(function(e){showToast('Toggle failed');console.warn('toggleSetting:',e);});
}
function toggleDualPrinterMode(on){
  toggleSetting('dualp', on);
  var t = document.getElementById('tab1');
  if (t) t.style.display = on ? '' : 'none';
  var d = document.getElementById('topStatusDot1');
  if (d) d.style.display = on ? '' : 'none';
  if (!on) selectPrinterTab(0);
}
/* PSRAM boards: reveal/hide printer 3 + 4 tabs and topbar dots when the
   experimental 4-printer beta is toggled, mirroring toggleDualPrinterMode. */
function toggleQuadPrinterMode(on){
  toggleSetting('quadp', on);
  [2,3].forEach(function(i){
    var t = document.getElementById('tab'+i);
    if (t) t.style.display = on ? '' : 'none';
    var d = document.getElementById('topStatusDot'+i);
    if (d) d.style.display = on ? '' : 'none';
  });
  if (!on) selectPrinterTab(0);
}
/* Toggle a grid mode (l8s / p9s) and sync the matching Gauge Layout extras
   block in the Printer section. Hides the extra dropdowns when the user
   doesn't have the mode enabled so the layout card stays compact. */
function toggleGridMode(key, on){
  toggleSetting(key, on);
  var elId = (key === 'l8s') ? 'landExtrasGroup' : 'portExtrasGroup';
  var el = document.getElementById(elId);
  if (el) el.style.display = on ? '' : 'none';
}
function toggleDangerUnlock(on){
  var ops = document.getElementById('dangerOps');
  if (ops) ops.style.display = on ? '' : 'none';
}

/* ============ Diagnostics ============ */
function toggleDebug(on){
  fetch('/debug/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'on='+(on?'1':'0')})
    .then(function(r){if(r.ok)showToast(on?'Debug ON':'Debug OFF');});
}
function refreshDiag(){
  fetch('/debug').then(function(r){return r.json();}).then(function(d){
    var h = '';
    function ageText(sec, hasAny){
      if (!hasAny) return 'Never';
      if (sec < 60) return sec + 's ago';
      if (sec < 3600) return Math.round(sec / 60) + ' min ago';
      return Math.round(sec / 3600) + ' h ago';
    }
    if (d.printers){
      d.printers.forEach(function(p){
        h += '<div style="margin-bottom:8px;padding:6px;border-left:2px solid '+(p.connected?'var(--success)':'var(--danger)')+'">';
        h += '<strong style="color:var(--text)">'+esc(p.name)+'</strong> (slot '+p.slot+')<br>';
        h += '<div class="stat-row"><span>MQTT:</span><span class="stat-val">'+(p.connected?'<span style="color:var(--success)">Connected</span>':'<span style="color:var(--danger)">Disconnected</span>')+'</span></div>';
        h += '<div class="stat-row"><span>Attempts:</span><span class="stat-val">'+p.attempts+'</span></div>';
        h += '<div class="stat-row"><span>Messages RX:</span><span class="stat-val">'+p.messages+'</span></div>';
        h += '<div class="stat-row"><span>Pushall total:</span><span class="stat-val">'+(p.pushall_total||0)+'</span></div>';
        var rc=p.rec_print||0, rd=p.rec_conn_dead||0, rf=p.rec_finish||0, ri=p.rec_idle||0;
        h += '<div class="stat-row"><span>Pushall recovery:</span><span class="stat-val">'+(rc+rd+rf+ri)+' (P:'+rc+' D:'+rd+' F:'+rf+' I:'+ri+')</span></div>';
        h += '<div class="stat-row"><span>Last pushall:</span><span class="stat-val">'+esc(p.last_pushall_reason||'Never')+' ('+ageText(p.last_pushall_age_s,p.pushall_total>0)+')</span></div>';
        if (p.last_rc!==0) h += '<div class="stat-row"><span>Last error:</span><span class="stat-val" style="color:var(--danger)">'+esc(p.rc_text)+'</span></div>';
        if (p.rc_hint) h += '<div style="margin-top:4px;font-size:11px;color:var(--warn);line-height:1.3">'+esc(p.rc_hint)+'</div>';
        h += '</div>';
      });
    }
    h += '<div class="stat-row"><span>Free heap:</span><span class="stat-val">'+Math.round(d.heap/1024)+' KB</span></div>';
    h += '<div class="stat-row"><span>Uptime:</span><span class="stat-val">'+formatUptime(d.uptime)+'</span></div>';
    h += '<div style="margin-top:8px;font-size:11px;color:var(--text-dim)">Recovery: P=Print stale, D=Conn dead, F=Finish stale, I=Idle/Unknown</div>';
    document.getElementById('diagInfo').innerHTML = h;
  }).catch(function(e){console.warn('refreshDiag:',e);});
}

/* ============ Hardware info (also drives WiFi section live KV) ============ */
function fmtBytes(kb){ if (kb >= 1024) return (kb/1024).toFixed(1) + ' MB'; return kb + ' KB'; }
function formatUptime(secs){
  var d = Math.floor(secs / 86400);
  var h = Math.floor((secs % 86400) / 3600);
  var m = Math.floor((secs % 3600) / 60);
  var s = secs % 60;
  if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
  if (h > 0) return h + 'h ' + m + 'm';
  if (m > 0) return m + 'm ' + s + 's';
  return s + 's';
}
function refreshHwInfo(){
  fetch('/status?slot='+currentSlot).then(function(r){return r.json();}).then(function(d){
    var heapEl = document.getElementById('hwHeap');
    if (heapEl && typeof d.heap_kb === 'number') heapEl.textContent = fmtBytes(d.heap_kb) + ' free';
    var fEl = document.getElementById('hwFlash');
    if (fEl && d.flash_kb) fEl.textContent = fmtBytes(d.flash_kb);
    var pEl = document.getElementById('hwPsram');
    if (pEl) pEl.textContent = d.psram_kb ? fmtBytes(d.psram_kb) : 'none';
    var mEl = document.getElementById('hwMac');
    if (mEl && d.mac) mEl.textContent = d.mac;
    var rEl = document.getElementById('wifiRssi');
    if (rEl && typeof d.rssi === 'number') rEl.textContent = d.rssi + ' dBm';
    var uEl = document.getElementById('wifiUptime');
    if (uEl && typeof d.uptime === 'number') uEl.textContent = formatUptime(d.uptime);
    var wt = document.getElementById('wifiTopText');
    if (wt) wt.textContent = d.ip || '-';
  }).catch(function(){});
}

/* ============ Topbar status dots (per-slot, independent of current tab) ============ */
function _updateTopDot(slot, dotId, txtId){
  var dot = document.getElementById(dotId);
  // Skip missing dots, and skip dots hidden by an experimental opt-in gate
  // (low-RAM 2-printer / PSRAM 4-printer) so disabled slots are not polled.
  if (!dot || dot.style.display === 'none') return;
  fetch('/status?slot=' + slot).then(function(r){return r.json();}).then(function(d){
    var ts = document.getElementById(txtId);
    var label;
    if (!d.configured) label = '-';
    else if (d.connected) label = d.name || ('Slot ' + (slot + 1));
    else label = (d.name || ('Slot ' + (slot + 1))) + ' (off)';
    if (ts) ts.textContent = label;
    dot.classList.toggle('off', !d.connected);
  }).catch(function(){});
}
function refreshTopStatusDots(){
  _updateTopDot(0, 'topStatusDot', 'topStatusText');
  _updateTopDot(1, 'topStatusDot1', 'topStatusText1');
  _updateTopDot(2, 'topStatusDot2', 'topStatusText2');
  _updateTopDot(3, 'topStatusDot3', 'topStatusText3');
}

/* ============ Live stats (printer card) ============ */
function refreshLiveStats(){
  fetch('/status?slot='+currentSlot).then(function(r){return r.json();}).then(function(d){
    var h = '';
    if (d.display_off) h += '<div class="stat-row"><span>Display:</span><span class="stat-val" style="color:var(--danger)">Off</span></div>';
    if (d.connected){
      if (d.no_data) h += '<div style="margin-bottom:var(--sp-2);padding:8px 10px;border:1px solid var(--danger);border-radius:6px;background:rgba(220,53,69,0.08);color:var(--danger);font-size:12.5px;line-height:1.5">Connected to Bambu, but no data received from the printer. Check: (1) the serial number is correct and UPPERCASE, (2) the printer is powered on.</div>';
      h += '<div class="stat-row"><span>State:</span><span class="stat-val">'+esc(d.state)+'</span></div>';
      h += '<div class="stat-row"><span>Nozzle:</span><span class="stat-val">'+d.nozzle+'/'+d.nozzle_t+'&deg;C</span></div>';
      h += '<div class="stat-row"><span>Bed:</span><span class="stat-val">'+d.bed+'/'+d.bed_t+'&deg;C</span></div>';
      if (d.progress > 0) h += '<div class="stat-row"><span>Progress:</span><span class="stat-val">'+d.progress+'%</span></div>';
      if (d.fan > 0) h += '<div class="stat-row"><span>Fan:</span><span class="stat-val">'+d.fan+'%</span></div>';
    } else if (d.configured) {
      h += '<span class="text-dim">Not connected (printer may be off)</span>';
    } else {
      h += '<span class="text-dim">Not Configured</span>';
    }
    document.getElementById('liveStats').innerHTML = h;
    var ps = document.getElementById('printerStatus');
    if (d.connected) { ps.className = 'status-pill status-ok'; ps.textContent = 'Connected'; }
    else if (d.configured) { ps.className = 'status-pill status-off'; ps.textContent = 'Disconnected / Powered Off'; }
    else { ps.className = 'status-pill status-na'; ps.textContent = 'Not Configured'; }
    if (d.display_off && d.connected) ps.textContent += ' (Display Off)';
  }).catch(function(e){console.warn('liveStats:',e);});
}

/* ============ Settings export / import ============ */
function exportSettings(){
  fetch('/settings/export').then(function(r){return r.text();}).then(function(t){
    var d = new Date();
    var pad = function(n){return (n<10?'0':'')+n;};
    var ts = d.getFullYear() + pad(d.getMonth()+1) + pad(d.getDate()) + '_' +
             pad(d.getHours()) + pad(d.getMinutes()) + pad(d.getSeconds());
    var a = document.createElement('a');
    a.href = 'data:application/json;charset=utf-8,' + encodeURIComponent(t);
    a.download = 'bambuhelper_settings_' + ts + '.json';
    document.body.appendChild(a); a.click(); document.body.removeChild(a);
  }).catch(function(){showToast('Export failed');});
}
function importSettings(){
  var f = document.getElementById('importFile').files[0];
  if (!f) { showToast('Select a JSON file first'); return; }
  if (!f.name.toLowerCase().endsWith('.json')) { showToast('Import file must be a JSON backup'); return; }
  if (!confirm('Import settings and restart? Current settings will be overwritten.')) return;
  var fd = new FormData(); fd.append('settings', f);
  var stat = document.getElementById('importStatus');
  stat.style.color = 'var(--info)'; stat.textContent = 'Importing...';
  fetch('/settings/import',{method:'POST',body:fd})
    .then(readJsonResponse)
    .then(function(d){
      if (d.status === 'ok'){ stat.style.color = 'var(--success)'; stat.textContent = d.message; }
      else if (d.message){ stat.style.color = 'var(--danger)'; stat.textContent = 'Import failed: ' + d.message; }
      else { stat.style.color = 'var(--danger)'; stat.textContent = 'Import failed'; }
    })
    .catch(function(e){ stat.style.color = 'var(--danger)'; stat.textContent = 'Import failed: upload or parsing error'; console.warn('importSettings:',e); });
}

/* ============ OTA ============ */
function startOta(){
  var f = document.getElementById('otaFile').files[0];
  if (!f) { showToast('Select a .bin file first'); return; }
  if (!f.name.toLowerCase().endsWith('.bin')) { showToast('Firmware file must end with .bin'); return; }
  if (f.size < 32768) { showToast('File too small'); return; }
  if (f.size > 1835008) { showToast('File too large (max 1.75MB)'); return; }
  var lowerName = f.name.toLowerCase();
  var board = '%BOARD%'.toLowerCase();
  if (lowerName.indexOf('bambuhelper-') === 0 && lowerName.indexOf('-' + board + '-') === -1){ showToast('Selected firmware looks like a different board variant'); return; }
  if (!confirm('Upload firmware and restart?')) return;
  var prog = document.getElementById('otaProgress');
  var bar = document.getElementById('otaBar');
  var pct = document.getElementById('otaPct');
  var stat = document.getElementById('otaStatus');
  prog.style.display = 'block'; bar.style.width = '0%'; pct.textContent = '0%';
  stat.innerHTML = '<span style="color:var(--info)">Uploading...</span>';
  var fd = new FormData(); fd.append('firmware', f);
  var xhr = new XMLHttpRequest();
  xhr.open('POST','/ota/upload',true);
  xhr.upload.onprogress = function(e){
    if (e.lengthComputable){
      var p = Math.round(e.loaded / e.total * 100);
      bar.style.width = p + '%'; pct.textContent = p + '%';
      if (p >= 100){ stat.style.color = 'var(--info)'; stat.textContent = 'Flashing...'; }
    }
  };
  xhr.onload = function(){
    try {
      var d = JSON.parse(xhr.responseText);
      if (d.status === 'ok'){ bar.style.width = '100%'; pct.textContent = '100%'; stat.style.color = 'var(--success)'; stat.textContent = d.message; }
      else { var msg = d.message || 'Firmware update failed'; if (msg === 'Invalid firmware file') msg = 'Invalid firmware file or wrong board build'; stat.style.color = 'var(--danger)'; stat.textContent = 'Update failed: ' + msg; }
    } catch(e) { stat.style.color = 'var(--danger)'; stat.textContent = 'Update failed: unexpected response'; }
  };
  xhr.onerror = function(){ stat.style.color = 'var(--danger)'; stat.textContent = 'Update failed: upload interrupted or connection lost'; };
  xhr.send(fd);
}

)rawliteral"
#ifdef ENABLE_OTA_AUTO
R"rawliteral(
var _autoOtaUrl='',_autoOtaProgress=0;
function switchFwTab(t){
  document.getElementById('fw-tab-auto').style.display = t === 'auto' ? 'block' : 'none';
  document.getElementById('fw-tab-manual').style.display = t === 'manual' ? 'block' : 'none';
  document.getElementById('tab-auto-btn').setAttribute('aria-pressed', t === 'auto' ? 'true' : 'false');
  document.getElementById('tab-manual-btn').setAttribute('aria-pressed', t === 'manual' ? 'true' : 'false');
}
function checkForUpdates(){
  var res = document.getElementById('updateResult'), info = document.getElementById('updateInfo');
  res.style.color = 'var(--info)'; res.textContent = 'Checking...'; info.style.display = 'none'; _autoOtaUrl = '';
  fetch('https://api.github.com/repos/Keralots/BambuHelper/releases/latest')
    .then(function(r){ if (!r.ok) throw new Error('GitHub API returned ' + r.status); return r.json(); })
    .then(function(d){
      var latest = d.tag_name, current = '%FW_VER%';
      function parseVer(v){ var m = v.replace(/^v/,'').match(/^(\d+)\.(\d+)(?:\.(\d+))?(.*)$/); return m ? {major:parseInt(m[1]),minor:parseInt(m[2]),patch:m[3]?parseInt(m[3]):0,pre:m[4]!==''} : null; }
      function isNewer(a, b){ var av = parseVer(a), bv = parseVer(b); if (!av || !bv) return a !== b; if (av.major !== bv.major) return av.major > bv.major; if (av.minor !== bv.minor) return av.minor > bv.minor; if (av.patch !== bv.patch) return av.patch > bv.patch; return !av.pre && bv.pre; }
      if (!isNewer(latest, current)){ res.style.color = 'var(--success)'; res.textContent = latest === current ? 'You are up to date (' + current + ')' : 'Running newer version (' + current + ')'; return; }
      var board = '%BOARD%', expectedPrefix = 'BambuHelper-' + board + '-', otaBin = null;
      for (var i = 0; i < d.assets.length; i++){ var n = d.assets[i].name; if (n.startsWith(expectedPrefix) && n.endsWith('-ota.bin')){ otaBin = d.assets[i]; break; } }
      res.style.color = 'var(--warn)'; res.textContent = 'Update available!';
      document.getElementById('updateVer').textContent = latest;
      document.getElementById('updateDate').textContent = new Date(d.published_at).toLocaleDateString();
      var installBtn = document.getElementById('installBtn'), link = document.getElementById('updateLink');
      if (otaBin){ _autoOtaUrl = otaBin.browser_download_url; link.href = otaBin.browser_download_url; link.style.display = 'inline-block'; installBtn.style.display = 'inline-block'; }
      else { installBtn.style.display = 'none'; link.style.display = 'none'; }
      info.style.display = 'block';
    })
    .catch(function(e){ res.style.color = 'var(--danger)'; res.textContent = 'Check failed: ' + e.message; console.warn('updateCheck:',e); });
}
function installUpdate(){
  if (!_autoOtaUrl) return;
  var btn = document.getElementById('installBtn');
  btn.disabled = true; btn.textContent = 'Installing...';
  document.getElementById('autoOtaWrap').style.display = 'block';
  document.getElementById('autoOtaBar').style.width = '0%';
  document.getElementById('autoOtaStatus').textContent = 'Starting...';
  _autoOtaProgress = 0;
  var p = new URLSearchParams(); p.append('url', _autoOtaUrl);
  fetch('/ota/auto',{method:'POST',body:p})
    .then(function(r){return r.json();})
    .then(function(d){ if (d.error) throw new Error(d.error); pollOtaStatus(); })
    .catch(function(e){ document.getElementById('autoOtaStatus').style.color = 'var(--danger)'; document.getElementById('autoOtaStatus').textContent = 'Error: ' + e.message; btn.disabled = false; btn.textContent = 'Install Update'; });
}
var _otaPoller = null;
function pollOtaStatus(){
  _otaPoller = setInterval(function(){
    fetch('/ota/status').then(function(r){return r.json();}).then(function(d){
      var bar = document.getElementById('autoOtaBar'), st = document.getElementById('autoOtaStatus');
      _autoOtaProgress = d.progress || 0;
      bar.style.width = d.progress + '%';
      if (d.status === 'done'){ clearInterval(_otaPoller); _otaPoller = null; bar.style.width = '100%'; st.style.color = 'var(--success)'; st.textContent = 'Done! Restarting device...'; waitForReboot(); }
      else if (d.status && d.status.indexOf('failed') === 0){ clearInterval(_otaPoller); _otaPoller = null; st.style.color = 'var(--danger)'; st.textContent = d.status; var btn = document.getElementById('installBtn'); btn.disabled = false; btn.textContent = 'Retry'; }
      else { st.textContent = d.status + ' (' + d.progress + '%)'; }
    }).catch(function(){
      if (_autoOtaProgress >= 90){ clearInterval(_otaPoller); _otaPoller = null; var bar = document.getElementById('autoOtaBar'), st = document.getElementById('autoOtaStatus'); bar.style.width = '100%'; st.style.color = 'var(--success)'; st.textContent = 'Done! Restarting device...'; waitForReboot(); }
    });
  }, 1000);
}
function waitForReboot(){
  var st = document.getElementById('autoOtaStatus');
  st.textContent = 'Waiting for device to restart...';
  var wentOffline = false, tries = 0;
  var check = setInterval(function(){
    fetch('/').then(function(){ if (wentOffline){ clearInterval(check); location.reload(); } })
      .catch(function(){ wentOffline = true; tries++; st.textContent = 'Restarting... (' + tries + 's)'; if (tries > 60){ clearInterval(check); st.textContent = 'Reboot timeout - please refresh manually.'; } });
  }, 2000);
}
)rawliteral"
#endif
R"rawliteral(

/* ============ Reboot / factory reset ============ */
function rebootDevice(){
  if (!confirm('Reboot device? Settings are preserved.')) return;
  fetch('/reboot',{method:'POST'}).then(function(){
    document.body.innerHTML = '<div style="text-align:center;padding-top:80px;font-family:sans-serif"><h2 style="color:#3FB950">Rebooting...</h2><p style="color:#8B949E;margin-top:10px">The device will be back in a few seconds.</p></div>';
  }).catch(function(){});
}
function factoryReset(){
  if (!confirm('Factory reset wipes ALL settings (WiFi, printers, gauge layout). Continue?')) return;
  if (!confirm('Are you absolutely sure? This cannot be undone.')) return;
  location = '/reset';
}

/* ============ After-print reveal ============ */
function toggleAfterPrint(){
  var v = document.getElementById('afterprint').value;
  document.getElementById('customMinsWrap').style.display = (v === 'custom') ? 'block' : 'none';
  var pong = document.getElementById('pong');
  var row = document.getElementById('pong-row');
  var showClock = (v !== 'keepon');
  pong.disabled = !showClock;
  if (row) row.style.opacity = showClock ? '1' : '0.4';
}

/* ============ Timezone list (AJAX-loaded to keep PROGMEM small) ============ */
fetch('/api/timezones').then(function(r){return r.json();}).then(function(d){
  var sel = document.getElementById('tz');
  for (var i = 0; i < d.zones.length; i++){
    var o = document.createElement('option');
    o.value = i; o.textContent = d.zones[i];
    if (i === d.selected) o.selected = true;
    sel.appendChild(o);
  }
}).catch(function(e){console.warn('tz load:',e);});

/* ============ Boot ============ */
/* Theme already applied by the inline <head> script before first paint;
   here we just sync the icon to match whatever state the document is in. */
applyThemeMode(document.documentElement.getAttribute('data-theme') || 'dark');

(function boot(){
  // First-time setup that runs once
  toggleConnMode();
  toggleStatic();
  toggleBtnPin();
  toggleLed();
  toggleAfterPrint();
  // Initial section: URL hash, else last visited (localStorage), else printer
  var initId = 'printer';
  if (location.hash){
    var h = location.hash.substring(1);
    if (['printer','display','hardware','wifi','power','diag'].indexOf(h) >= 0) initId = h;
  } else {
    try { var saved = localStorage.getItem('bambu_section'); if (saved && ['printer','display','hardware','wifi','power','diag'].indexOf(saved) >= 0) initId = saved; } catch(e){}
  }
  loadSection(initId);
  applyLabelMaxlen();
  setTimeout(function(){ selectPrinterTab(0); }, 80);
  setTimeout(function(){ selectPowerTab(0); }, 140);
  refreshHwInfo();
  refreshTopStatusDots();
  setInterval(refreshTopStatusDots, 5000);
})();
</script>
</body>
</html>
)rawliteral";
