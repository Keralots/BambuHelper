/* ============ Loader / section dispatcher ============ */
var SECTION_LABELS = {
  printer: 'Printer Settings',
  display: 'Display',
  errors: 'Printer Errors',
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
  /* Errors move on their own schedule, not the user's - 5 s is plenty, and it
     is four slot fetches per tick. */
  if (id === 'errors') { refreshErrorCard(); statsTimer = setInterval(refreshErrorCard, 5000); }
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
var GAUGE_CAMERA_ID = 32;  // keep in sync with GAUGE_CAMERA in settings.h
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
    // Ready / Print complete slots cannot host the camera: that tile paints on
    // its own cadence and the stream only starts for a print-grid slot, so it
    // would sit frozen. Filtered here because this runs again on every capability
    // refresh and tab switch - removing the option once would not stick.
    var isIdleSel = (sel.id === 'is0' || sel.id === 'is1');
    var cur = sel.value;
    sel.innerHTML = '';
    ungrouped.forEach(function(e){
      if (isIdleSel && e.i === GAUGE_CAMERA_ID) return;
      var o = document.createElement('option');
      o.value = e.i; o.textContent = e.name;
      sel.appendChild(o);
    });
    groups.forEach(function(g){
      var og = document.createElement('optgroup');
      og.label = g;
      groupMembers[g].forEach(function(e){
        if (isIdleSel && e.i === GAUGE_CAMERA_ID) return;
        var o = document.createElement('option');
        o.value = e.i; o.textContent = e.name;
        og.appendChild(o);
      });
      if (og.children.length) sel.appendChild(og);
    });
    if (cur !== '') sel.value = cur;
  });
}
rebuildGaugeOptions();

/* Round boards (GC9A01): the printing dashboard has no 2x3 grid. Only the Rim
   skin renders gauges - three mini slots fed from gaugeSlots[0..2] - so the
   card re-labels itself and hides the bottom row. gs3-gs5 stay in the DOM
   (hidden) so saveGaugeLayout keeps posting all six slots unchanged.
   The is0/is1 pair drives the round Ready screen only: the round Print
   complete screen is a fixed layout (checkmark, headline, file name, energy)
   with no room for the two gauges, so the divider says so rather than
   promising a screen that ignores the setting. */
var IS_ROUND = DEV.round === '1';
if (IS_ROUND){
  var gd = document.getElementById('glDesc');
  if (gd) gd.innerHTML = 'Per-printer mini gauges for the round <em>Rim</em> skin: left, center and right slot. The <em>Speedo</em> and <em>Rings</em> skins have fixed layouts. The last pair is the two gauges on the <em>Ready</em> screen - the round <em>Print complete</em> screen has a fixed layout and does not use them. Set a slot to <em>Empty</em> to hide it.';
  var ipd = document.getElementById('idlePairDivider');
  if (ipd) ipd.innerHTML = '&#9670; Ready screen';
  var roundLbls = ['Left gauge','Center gauge','Right gauge'];
  for (var ri = 0; ri < 3; ri++){
    var rsel = document.getElementById('gs' + ri);
    if (rsel){ var rlb = rsel.parentNode.querySelector('label'); if (rlb) rlb.textContent = roundLbls[ri]; }
  }
  var trd = document.getElementById('topRowDivider'); if (trd) trd.style.display = 'none';
  var brg = document.getElementById('bottomRowGroup'); if (brg) brg.style.display = 'none';
}

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
      ['gaugeSlots','landscapeExtras','portraitExtras','idleSlots'].forEach(function(key){
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
  // Round Rim skin -> Nozzle/Bed/PartFan (the original fixed layout).
  // Both extras arrays default to Empty (user opts in via mode toggles).
  var d = IS_ROUND ? [2,3,4,0,0,0] : [1,2,3,4,5,6];
  for (var i = 0; i < 6; i++) { var s = document.getElementById('gs' + i); if (s) s.value = d[i]; }
  var lx = ['lx0','lx1'], px = ['px0','px1','px2'];
  lx.forEach(function(id){ var s = document.getElementById(id); if (s) s.value = 0; });
  px.forEach(function(id){ var s = document.getElementById(id); if (s) s.value = 0; });
  // Ready / Print complete -> Nozzle/Bed, what those screens always drew.
  var di = [2,3];
  for (var i = 0; i < 2; i++) { var s = document.getElementById('is' + i); if (s) s.value = di[i]; }
  showToast('Restored layout defaults');
}
function saveGaugeLayout(){
  var p = new URLSearchParams();
  p.append('slot', currentSlot);
  for (var g = 0; g < 6; g++) { var s = document.getElementById('gs' + g); if (s) p.append('gs' + g, s.value); }
  for (var g = 0; g < 2; g++) { var s = document.getElementById('lx' + g); if (s) p.append('lx' + g, s.value); }
  for (var g = 0; g < 3; g++) { var s = document.getElementById('px' + g); if (s) p.append('px' + g, s.value); }
  for (var g = 0; g < 2; g++) { var s = document.getElementById('is' + g); if (s) p.append('is' + g, s.value); }
  var av = document.getElementById('amsv');
  if (av && av.checked) p.append('amsv', '1');
  fetch('/save/gaugelayout',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok')showToast('Gauge layout saved!');else showToast('Error');})
    .catch(function(){showToast('Save failed');});
}

/* ============ Chamber light ============ */
function renderLightState(v){
  var el = document.getElementById('lightStateLbl');
  if (!el) return;
  if (v === 1) { el.className = 'status-pill status-ok'; el.textContent = 'Light: On'; }
  else if (v === 0) { el.className = 'status-pill status-off'; el.textContent = 'Light: Off'; }
  else { el.className = 'status-pill status-na'; el.textContent = 'Light: -'; }
}
function refreshLightState(){
  fetch('/printer/config?slot=' + currentSlot).then(function(r){return r.json();})
    .then(function(d){ renderLightState(d.lightState); }).catch(function(){});
}
function saveLightConfig(){
  var p = new URLSearchParams();
  p.append('slot', currentSlot);
  if (document.getElementById('loff_fin').checked)  p.append('loff_fin', '1');
  if (document.getElementById('loff_fail').checked) p.append('loff_fail', '1');
  if (document.getElementById('lon_start').checked) p.append('lon_start', '1');
  p.append('ldelay', document.getElementById('ldelay').value);
  fetch('/light/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){if(d.status==='ok')showToast('Light settings saved!');else showToast('Error');})
    .catch(function(){showToast('Save failed');});
}
function setLight(mode){
  var p = new URLSearchParams();
  p.append('slot', currentSlot);
  p.append('mode', mode);
  fetch('/light/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(function(d){
      if (d.status === 'ok'){
        showToast(mode === 'on' ? 'Light on sent' : 'Light off sent');
        setTimeout(refreshLightState, 1500);  // confirm from next lights_report
      } else showToast('Error');
    })
    .catch(function(){showToast('Command failed');});
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
    // The picker merged US and EU into one entry, but devices configured before
    // that still report 'eu' - without this the select would end up on nothing.
    document.getElementById('region').value = (d.region === 'eu') ? 'us' : (d.region || 'us');
    document.getElementById('cl_token').value = '';
    var capsChanged = false;
    Object.keys(GAUGE_REQUIRES).forEach(function(idx){
      var capName = GAUGE_REQUIRES[idx];
      if (d[capName] && !gaugeCaps[capName]){ gaugeCaps[capName] = true; capsChanged = true; }
    });
    // Keep options for gauges this printer has saved even when it is offline and
    // its capability flags say otherwise - otherwise the select renders empty and
    // the next save silently downgrades the slot to Empty.
    ['gaugeSlots','idleSlots'].forEach(function(key){
      if (!d[key]) return;
      d[key].forEach(function(v){
        if (!persistedGauges[v]){ persistedGauges[v] = true; capsChanged = true; }
      });
    });
    if (capsChanged) rebuildGaugeOptions();
    if (d.gaugeSlots) { for (var g = 0; g < 6; g++) { var sel = document.getElementById('gs' + g); if (sel) sel.value = d.gaugeSlots[g] || 0; } }
    if (d.landscapeExtras) { for (var g = 0; g < 2; g++) { var sel = document.getElementById('lx' + g); if (sel) sel.value = d.landscapeExtras[g] || 0; } }
    if (d.portraitExtras)  { for (var g = 0; g < 3; g++) { var sel = document.getElementById('px' + g); if (sel) sel.value = d.portraitExtras[g] || 0; } }
    if (d.idleSlots)       { for (var g = 0; g < 2; g++) { var sel = document.getElementById('is' + g); if (sel) sel.value = d.idleSlots[g] || 0; } }
    var av = document.getElementById('amsv');
    if (av) { av.checked = !!d.amsView; syncAmsView(); }
    var lf = d.lightFlags || 0;
    document.getElementById('loff_fin').checked  = !!(lf & 1);
    document.getElementById('loff_fail').checked = !!(lf & 2);
    document.getElementById('lon_start').checked = !!(lf & 4);
    if (typeof d.lightDelay === 'number') document.getElementById('ldelay').value = d.lightDelay;
    renderLightState(d.lightState);
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
    var pw = document.getElementById('cl_pass');
    if (pw) { pw.value = ''; document.getElementById('cl_savePass').checked = false; }
    var msg = document.getElementById('cl_loginMsg');
    if (msg) { msg.textContent = 'Signed out.'; msg.style.color = 'var(--text-mid)'; }
    var wrap = document.getElementById('cl_codeWrap');
    if (wrap) wrap.style.display = 'none';
    // The picker still lists the account we just signed out of.
    var sel = document.getElementById('cl_acctsel');
    if (sel) { sel.innerHTML = ''; sel.style.display = 'none'; }
  });
}



var clLoginMode = 'password';

/* This file is shared by every board, but the sign-in markup is not: boards
   without the flow render the token block alone and no method picker. Bail out
   whenever the picker is absent, or the token block - the only way in on those
   boards - would get hidden with nothing to replace it. */
function clHasSignIn(){ return !!document.getElementById('cl_method'); }

function clSetAuthMethod(m){
  if (!clHasSignIn()) return;
  document.getElementById('cl_signinWrap').style.display = (m === 'signin') ? '' : 'none';
  document.getElementById('cl_tokenWrap').style.display  = (m === 'signin') ? 'none' : '';
}

function clInitAuthUi(){
  if (!clHasSignIn()) return;
  clSetAuthMethod('signin');
  fetch('/cloud/login/status').then(function(r){return r.json();}).then(function(d){
    if (d.email) document.getElementById('cl_email').value = d.email;
    if (d.saved_password) document.getElementById('cl_savePass').checked = true;

    // A sign-in left waiting for a code, or a background renewal that gave up,
    // is device state the page has to show - nothing else polls for it, so
    // without this the user only learns about it by trying something.
    if (d.state === 'need_tfa' || d.state === 'need_email_code' || d.failed){
      clApplyLoginState(d);
      return;
    }
    if (d.has_token && d.email){
      document.getElementById('cl_loginMsg').textContent = 'Signed in as ' + d.email;
    }
  }).catch(function(){});
}

function clSetLoginMode(m){
  clLoginMode = m;
  document.getElementById('cl-mode-pass-btn').setAttribute('aria-pressed', m === 'password');
  document.getElementById('cl-mode-code-btn').setAttribute('aria-pressed', m === 'code');
  document.getElementById('cl_passWrap').style.display = (m === 'password') ? '' : 'none';
  document.getElementById('cl_signinBtn').textContent = (m === 'password') ? 'Sign in' : 'Email me a code';
}

// One place decides what the sign-in box shows, so the password step and the
// code step cannot disagree about the current state.
function clApplyLoginState(d){
  var msg = document.getElementById('cl_loginMsg');
  var wrap = document.getElementById('cl_codeWrap');
  msg.textContent = d.message || '';
  // A refused code leaves the device waiting on the same step, so the state
  // stays 'need_*' while the message is a complaint - the device says which.
  msg.style.color = (d.failed || d.state === 'failed') ? 'var(--warn)' : 'var(--text-mid)';

  if (d.state === 'need_tfa' || d.state === 'need_email_code'){
    wrap.style.display = '';
    document.getElementById('cl_codeLabel').textContent =
      (d.state === 'need_tfa') ? 'Authenticator code' : 'Code from your email';
    document.getElementById('cl_code').focus();
    return;
  }

  wrap.style.display = 'none';
  if (d.state === 'ok'){
    document.getElementById('cl_pass').value = '';
    document.getElementById('cl_code').value = '';
    var cs = document.getElementById('cloudStatus');
    cs.style.color = 'var(--success)';
    cs.textContent = 'Token active' + (d.email ? ' (' + d.email + ')' : '');
    showToast('Signed in to Bambu Cloud');
    // The account's printer list is the whole point of being signed in - offer
    // it straight away instead of making the user hunt for the button.
    loadAccountPrinters();
  }
}

function cloudSignIn(){
  var p = new URLSearchParams();
  p.append('email', document.getElementById('cl_email').value.trim());
  p.append('mode', clLoginMode);
  if (clLoginMode === 'password'){
    p.append('password', document.getElementById('cl_pass').value);
    p.append('save', document.getElementById('cl_savePass').checked ? '1' : '0');
  }
  document.getElementById('cl_loginMsg').textContent = 'Contacting Bambu...';
  fetch('/cloud/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(clApplyLoginState)
    .catch(function(){document.getElementById('cl_loginMsg').textContent = 'Sign-in request failed.';});
}

function cloudSubmitCode(){
  var p = new URLSearchParams();
  p.append('code', document.getElementById('cl_code').value.trim());
  document.getElementById('cl_loginMsg').textContent = 'Checking the code...';
  fetch('/cloud/login/code',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();})
    .then(clApplyLoginState)
    .catch(function(){document.getElementById('cl_loginMsg').textContent = 'Verification request failed.';});
}



/* Every printer bound to the account, straight from the cloud - so the serial
   is picked from a list instead of copied off a label. Works with a pasted
   token too, which is why it is not tied to the sign-in flow. */
function loadAccountPrinters(){
  var btn = document.getElementById('cl_acctBtn');
  var sel = document.getElementById('cl_acctsel');
  btn.disabled = true; btn.textContent = 'Loading...';
  fetch('/cloud/printers?region=' + document.getElementById('region').value)
    .then(function(r){return r.json();})
    .then(function(d){
      btn.disabled = false; btn.textContent = 'My printers';
      sel.innerHTML = '';
      if (!d.printers || !d.printers.length){
        sel.style.display = 'none';
        showToast(d.message || 'No printers found on this account');
        return;
      }
      var head = document.createElement('option');
      head.value = ''; head.textContent = 'Select a printer (' + d.printers.length + ')';
      sel.appendChild(head);
      for (var i = 0; i < d.printers.length; i++){
        var pr = d.printers[i];
        var o = document.createElement('option');
        o.value = pr.serial;
        o.setAttribute('data-name', pr.name || '');
        o.textContent = (pr.name || pr.serial) + ' - ' + (pr.model || '?') + (pr.online ? '' : ' (offline)');
        sel.appendChild(o);
      }
      sel.style.display = '';
    })
    .catch(function(){
      btn.disabled = false; btn.textContent = 'My printers';
      showToast('Could not reach the Bambu account service');
    });
}

function pickAccountPrinter(){
  var sel = document.getElementById('cl_acctsel');
  var o = sel.options[sel.selectedIndex];
  if (!o || !o.value) return;
  document.getElementById('cl_serial').value = o.value;
  var nm = document.getElementById('cl_pname');
  if (!nm.value) nm.value = (o.getAttribute('data-name') || '').substring(0, 23);
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
  var isES8311 = DEV.es8311 === '1';
  document.getElementById('buzPinRow').style.display = (buzOn && !isES8311) ? 'block' : 'none';
  document.getElementById('buzEs8311Info').style.display = (buzOn && isES8311) ? 'block' : 'none';
  var btnOn = document.getElementById('btntype').value !== '0';
  document.getElementById('buzClickRow').style.display = (buzOn && btnOn) ? 'flex' : 'none';
}
function toggleLed(){
  document.getElementById('ledFields').style.display = document.getElementById('leden').value !== '0' ? 'block' : 'none';
  var drv = document.getElementById('leddrv').value;
  /* Only a three-pin RGB LED needs the extra pins and the polarity tick; a
     WS2812 carries its own driver chip and gets colour from the data stream. */
  document.getElementById('ledRgbPins').style.display  = drv === '1' ? 'flex' : 'none';
  document.getElementById('ledAnodeRow').style.display = drv === '1' ? 'flex' : 'none';
  document.getElementById('ledColors').style.display   = drv === '0' ? 'none' : 'block';
  document.getElementById('ledpinLbl').textContent =
    drv === '1' ? 'Red GPIO pin' : (drv === '2' ? 'Data GPIO pin' : 'LED GPIO pin');
  toggleLedFx();
  toggleLedErr();
}
/* Fills the pin fields from the board's own onboard RGB wiring. The button only
   exists on boards that have one, so every caller path is guarded on the row. */
function ledUseOnboard(){
  var row = document.getElementById('ledOnboard');
  if (!row) return;
  document.getElementById('leddrv').value = row.getAttribute('data-drv');
  document.getElementById('ledpin').value = row.getAttribute('data-r');
  document.getElementById('ledping').value = row.getAttribute('data-g');
  document.getElementById('ledpinb').value = row.getAttribute('data-b');
  document.getElementById('ledanode').checked = row.getAttribute('data-anode') === '1';
  toggleLed();
  ledPreviewSend();
}
function toggleLedFx(){
  var fx = document.getElementById('ledfxmd');
  if (!fx) return;
  document.getElementById('ledFxParams').style.display = fx.value !== '0' ? 'block' : 'none';
}
function toggleLedErr(){
  var c = document.getElementById('lederr');
  if (!c) return;
  document.getElementById('ledErrParams').style.display = c.checked ? 'block' : 'none';
}
/* col is the colour to light during the preview. A colour picker passes its own
   value so the user sees that swatch on the LED while dragging; everything else
   omits it and the idle colour stands in. */
function ledPreviewSend(col){
  var p = new URLSearchParams();
  p.append('en', document.getElementById('leden').value);
  p.append('drv', document.getElementById('leddrv').value);
  p.append('pin', document.getElementById('ledpin').value);
  p.append('ping', document.getElementById('ledping').value);
  p.append('pinb', document.getElementById('ledpinb').value);
  p.append('anode', document.getElementById('ledanode').checked ? '1' : '0');
  p.append('br', document.getElementById('ledbr').value);
  p.append('col', col || document.getElementById('ledcidl').value);
  fetch('/led/preview',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()}).catch(function(){});
}
function ledTestEffect(){
  var p = new URLSearchParams();
  p.append('md', document.getElementById('ledfxmd').value);
  p.append('sec', document.getElementById('ledfxsec').value);
  p.append('br', document.getElementById('ledfxbr').value);
  p.append('col', document.getElementById('ledcfin').value);
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
  p.append('leddrv', document.getElementById('leddrv').value);
  p.append('ledpin', document.getElementById('ledpin').value);
  p.append('ledping', document.getElementById('ledping').value);
  p.append('ledpinb', document.getElementById('ledpinb').value);
  p.append('ledanode', document.getElementById('ledanode').checked ? '1' : '0');
  p.append('ledcidl', document.getElementById('ledcidl').value);
  p.append('ledcprn', document.getElementById('ledcprn').value);
  p.append('ledcpau', document.getElementById('ledcpau').value);
  p.append('ledcfin', document.getElementById('ledcfin').value);
  p.append('ledcerr', document.getElementById('ledcerr').value);
  p.append('ledbr', document.getElementById('ledbr').value);
  p.append('ledfxmd', document.getElementById('ledfxmd').value);
  p.append('ledfxsec', document.getElementById('ledfxsec').value);
  p.append('ledfxbr', document.getElementById('ledfxbr').value);
  p.append('ledauto', document.getElementById('ledauto').checked ? '1' : '0');
  p.append('ledpause', document.getElementById('ledpause').checked ? '1' : '0');
  p.append('lederr', document.getElementById('lederr').checked ? '1' : '0');
  p.append('lederrsec', document.getElementById('lederrsec').value);
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
  var plugOff = !document.getElementById('tsm_en').checked;
  var dm = document.querySelectorAll('input[name="tsm_dm"]');
  for (var i = 0; i < dm.length; i++) dm[i].disabled = hidden || plugOff;
  var note = document.getElementById('dmHiddenNote');
  if (note) note.style.display = hidden ? 'block' : 'none';
}
/* Grey out and disable the per-plug settings while "Enable power monitoring"
   is unchecked, so a filled-in but disabled plug can't be mistaken for a
   working one. The tariff card (shared) and Live stats stay active. */
function applyPowerEnableState(){
  var en = document.getElementById('tsm_en').checked;
  var blocks = ['plugDeps1', 'plugDeps2'];
  for (var i = 0; i < blocks.length; i++){
    var el = document.getElementById(blocks[i]);
    if (!el) continue;
    el.classList.toggle('deps-off', !en);
    var ctl = el.querySelectorAll('input,select');
    for (var j = 0; j < ctl.length; j++) ctl[j].disabled = !en;
  }
  applyHideReadoutToPowerDM();
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
    document.getElementById('tsm_pt').value = (d.plugType >= 0 && d.plugType <= 3) ? String(d.plugType) : '0';
    document.getElementById('tsm_po').value = (d.plugOutlet >= 0 && d.plugOutlet <= 3) ? String(d.plugOutlet) : '0';
    onPlugTypeChange();
    document.getElementById('tsm_ip').value = d.ip || '';
    var dm = document.querySelectorAll('input[name="tsm_dm"]');
    for (var j = 0; j < dm.length; j++) dm[j].checked = (parseInt(dm[j].value) === (d.displayMode || 0));
    applyPowerEnableState();
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
  var type = document.getElementById('tsm_pt').value;
  var shelly = (type === '1');
  var kasa = (type === '2');
  var shellyStrip = (type === '3');
  var hint = document.getElementById('tsm_shelly_hint');
  if (hint) hint.style.display = shelly ? '' : 'none';
  var kasaHint = document.getElementById('tsm_kasa_hint');
  if (kasaHint) kasaHint.style.display = kasa ? '' : 'none';
  var stripHint = document.getElementById('tsm_shellystrip_hint');
  if (stripHint) stripHint.style.display = shellyStrip ? '' : 'none';
  var outletField = document.getElementById('tsm_outlet_field');
  if (outletField) outletField.style.display = shellyStrip ? '' : 'none';
  var ptRow = document.getElementById('tsm_pt_row');
  if (ptRow) ptRow.style.gridTemplateColumns = shellyStrip ? '1fr 1fr' : '1fr';
  // Shelly and Kasa realtime APIs have no Today odometer.
  var tdLbl = document.getElementById('ptTodayLabel');
  var tdVal = document.getElementById('ptToday');
  if (tdLbl) tdLbl.style.display = (shelly || kasa || shellyStrip) ? 'none' : '';
  if (tdVal) tdVal.style.display = (shelly || kasa || shellyStrip) ? 'none' : '';
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
    // Prefer the real relay state when the plug reports it (Shelly/Kasa output);
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
  p.append('tsm_po', document.getElementById('tsm_po').value);
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
// eta/fin/stok = the three accent colours that belong to no gauge (#163).
var themes = {
  default:{bg:'#081018',track:'#182028',clkt:'#FFFFFF',clkd:'#C0C0C0',eta:'#00FF00',fin:'#00FF00',stok:'#00FF00',pname:'#00FF00',txt:'#FFFFFF',txtd:'#C0C0C0',dorc:'#00FF00',doro:'#FF7D00',prg:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},noz:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},pfn:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},afn:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},afr:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},cfn:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},exh:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'},cht:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},hbk:{a:'#FFA500',l:'#FFA500',v:'#FFFFFF'},pwr:{a:'#FFD600',l:'#FFD600',v:'#FFFFFF'},lyr:{a:'#00FF00',l:'#00FF00',v:'#FFFFFF'}},
  mono_green:{bg:'#000800',track:'#0A1A0A',clkt:'#00FF41',clkd:'#00CC33',eta:'#00FF41',fin:'#00FF41',stok:'#00FF41',pname:'#00FF41',txt:'#00FF41',txtd:'#00CC33',dorc:'#00CC33',doro:'#00FF41',prg:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},noz:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},bed:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},pfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},afn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},afr:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},cfn:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},exh:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},cht:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},hbk:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},pwr:{a:'#00FF41',l:'#00CC33',v:'#00FF41'},lyr:{a:'#00FF41',l:'#00CC33',v:'#00FF41'}},
  neon:{bg:'#0A0014',track:'#1A0A2E',clkt:'#FF00FF',clkd:'#AA00FF',eta:'#00FF88',fin:'#FF00FF',stok:'#00FF88',pname:'#FF00FF',txt:'#FFFFFF',txtd:'#B080C0',dorc:'#00FF88',doro:'#FF4400',prg:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},noz:{a:'#FF4400',l:'#FF6600',v:'#FFFFFF'},bed:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},pfn:{a:'#00FF88',l:'#00FF88',v:'#FFFFFF'},afn:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},afr:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},cfn:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},exh:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'},cht:{a:'#00FFFF',l:'#00FFFF',v:'#FFFFFF'},hbk:{a:'#FF4400',l:'#FF6600',v:'#FFFFFF'},pwr:{a:'#FFFF00',l:'#FFFF00',v:'#FFFFFF'},lyr:{a:'#FF00FF',l:'#FF00FF',v:'#FFFFFF'}},
  warm:{bg:'#140A00',track:'#2E1A08',clkt:'#FFEEDD',clkd:'#FFB347',eta:'#FFB347',fin:'#FFD700',stok:'#FFB347',pname:'#FFEEDD',txt:'#FFEEDD',txtd:'#C9A98F',dorc:'#FFB347',doro:'#FF6347',prg:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},noz:{a:'#FF6347',l:'#FF6347',v:'#FFEEDD'},bed:{a:'#FFA500',l:'#FFA500',v:'#FFEEDD'},pfn:{a:'#FFD700',l:'#FFD700',v:'#FFEEDD'},afn:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},afr:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},cfn:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},exh:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'},cht:{a:'#FFA500',l:'#FFA500',v:'#FFEEDD'},hbk:{a:'#FF8C00',l:'#FF8C00',v:'#FFEEDD'},pwr:{a:'#FFD700',l:'#FFD700',v:'#FFEEDD'},lyr:{a:'#FFB347',l:'#FFB347',v:'#FFEEDD'}},
  ocean:{bg:'#000A14',track:'#0A1A2E',clkt:'#E0F0FF',clkd:'#00BFFF',eta:'#00BFFF',fin:'#00CED1',stok:'#00BFFF',pname:'#E0F0FF',txt:'#E0F0FF',txtd:'#8FB4CF',dorc:'#00BFFF',doro:'#FF7F50',prg:{a:'#00BFFF',l:'#00BFFF',v:'#E0F0FF'},noz:{a:'#FF7F50',l:'#FF7F50',v:'#E0F0FF'},bed:{a:'#4169E1',l:'#4169E1',v:'#E0F0FF'},pfn:{a:'#00CED1',l:'#00CED1',v:'#E0F0FF'},afn:{a:'#48D1CC',l:'#48D1CC',v:'#E0F0FF'},afr:{a:'#48D1CC',l:'#48D1CC',v:'#E0F0FF'},cfn:{a:'#20B2AA',l:'#20B2AA',v:'#E0F0FF'},exh:{a:'#20B2AA',l:'#20B2AA',v:'#E0F0FF'},cht:{a:'#4169E1',l:'#4169E1',v:'#E0F0FF'},hbk:{a:'#FF7F50',l:'#FF7F50',v:'#E0F0FF'},pwr:{a:'#FFD700',l:'#FFD700',v:'#E0F0FF'},lyr:{a:'#00BFFF',l:'#00BFFF',v:'#E0F0FF'}},
  /* The only light preset, and the only one whose gauge values are dark. Both
     halves matter on a light background: the neon arcs the dark presets use are
     unreadable on one, and a light glyph is worse than unreadable - gauge values
     are drawn transparently (setGaugeClearedTextColor), so their antialiased
     edge always blends toward black and a pale value picks up a dark outline.
     Dark values make that blend the correct one. Every colour here survives the
     RGB565 round trip, so what the picker shows is what the panel paints. */
  paper:{bg:'#FFFFFF',track:'#CDCACD',clkt:'#101010',clkd:'#5A595A',eta:'#007100',fin:'#007100',stok:'#007100',pname:'#0050A4',txt:'#101010',txtd:'#5A595A',dorc:'#007100',doro:'#D52000',prg:{a:'#00A100',l:'#00A100',v:'#101010'},noz:{a:'#D56100',l:'#D56100',v:'#101010'},bed:{a:'#0091A4',l:'#0091A4',v:'#101010'},pfn:{a:'#0091A4',l:'#0091A4',v:'#101010'},afn:{a:'#D56100',l:'#D56100',v:'#101010'},afr:{a:'#D56100',l:'#D56100',v:'#101010'},cfn:{a:'#00A100',l:'#00A100',v:'#101010'},exh:{a:'#00A100',l:'#00A100',v:'#101010'},cht:{a:'#0091A4',l:'#0091A4',v:'#101010'},hbk:{a:'#D56100',l:'#D56100',v:'#101010'},pwr:{a:'#B48100',l:'#B48100',v:'#101010'},lyr:{a:'#00A100',l:'#00A100',v:'#101010'}}
};
function applyTheme(name){
  var t = themes[name]; if (!t) return;
  document.getElementById('clr_bg').value = t.bg;
  document.getElementById('clr_track').value = t.track;
  document.getElementById('clr_pbar').value = t.prg.a;
  document.getElementById('clk_time').value = t.clkt;
  document.getElementById('clk_date').value = t.clkd;
  setAccentColors(t.eta, t.fin, t.stok, t.pname, t.txt, t.txtd);
  setDoorColors(t.dorc, t.doro);
  for (var i = 0; i < GAUGE_KEYS.length; i++){
    var k = GAUGE_KEYS[i], c = t[k];
    if (!c) continue;
    document.getElementById(k + '_a').value = c.a;
    document.getElementById(k + '_l').value = c.l;
    document.getElementById(k + '_v').value = c.v;
  }
  applyDisplay();
}
// The accent pickers, written together. Tolerates a missing value so a theme
// that predates any of them still applies cleanly.
function setAccentColors(eta, fin, stok, pname, txt, txtd){
  if (eta)   document.getElementById('clr_eta').value = eta;
  if (fin)   document.getElementById('clr_fin').value = fin;
  if (stok)  document.getElementById('clr_stok').value = stok;
  if (pname) document.getElementById('clr_pname').value = pname;
  if (txt)   document.getElementById('clr_txt').value = txt;
  if (txtd)  document.getElementById('clr_txtd').value = txtd;
}
// Door status pair. Open stays a contrast colour in every preset - it is the
// half of the pair that has to catch an eye across the room.
function setDoorColors(closed, open){
  if (closed) document.getElementById('clr_dorc').value = closed;
  if (open)   document.getElementById('clr_doro').value = open;
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
  // Firmware defaults: green accents, white text, grey muted text. The grey is
  // #C0C0C0 and not #C6C6C6 because only the former survives the round trip -
  // RGB565 quantises it to 0xC618, which is exactly CLR_TEXT_DIM_DEFAULT and
  // exactly what themes.default.txtd carries. #C6C6C6 lands on 0xC638, so
  // "reset to defaults" would have left the device on a value its own defaults
  // never produce, and disagreeing with the Default theme for the same field.
  setAccentColors('#00FF00', '#00FF00', '#00FF00', '#00FF00', '#FFFFFF', '#C0C0C0');
  setDoorColors('#00FF00', '#FF7D00');
  applyDisplay();
}
function clearGaugeLabels(){
  for (var i = 0; i < GAUGE_LABEL_KEYS.length; i++){
    var k = GAUGE_LABEL_KEYS[i];
    // Door has no built-in fallback (empty = icon only, an intentional mode), so
    // "clear to defaults" must write its default text rather than blank it.
    document.getElementById(k + '_lbl').value = (k === 'dor') ? 'Door' : '';
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
  // Accents share the base hue so the ETA, the finish headline, the status
  // badge and the printer name read as one family instead of four more random
  // colours.
  var accent = hslToHex(baseH, 70, 60);
  // Text stays white/grey through a randomise: a random hue on the readouts
  // themselves costs legibility, which no roll of the dice is worth.
  setAccentColors(accent, accent, accent, accent, '#FFFFFF', '#C0C0C0');
  // Closed door joins the accent family; open keeps a fixed contrast colour so
  // a random roll can never make "door open" blend into "door closed".
  setDoorColors(accent, '#FF7D00');
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
  var afClock = document.getElementById('afterfin').value !== 'off';
  if (ap === 'keepon') { p.append('keepon', '1'); p.append('fmins', '0'); }
  else if (ap === 'custom') { p.append('fmins', document.getElementById('fmins').value); if (afClock) p.append('clock', '1'); }
  else { p.append('fmins', ap); if (afClock) p.append('clock', '1'); }
  if (document.getElementById('dack').checked) p.append('dack', '1');
  if (document.getElementById('fintm').checked) p.append('fintm', '1');
  if (document.getElementById('kps').checked) p.append('kps', '1');
  if (document.getElementById('abar').checked) p.append('abar', '1');
  if (document.getElementById('pong').checked) p.append('pong', '1');
  if (document.getElementById('slbl').checked) p.append('slbl', '1');
  p.append('timem', document.getElementById('timem').value);
  if (document.getElementById('fanmp').checked) p.append('fanmp', '1');
  p.append('glowm', document.getElementById('glowm').value);
  p.append('glow_clr', document.getElementById('glow_clr').value);
  p.append('glows', document.getElementById('glows').value);
  p.append('glowd', document.getElementById('glowd').value);
  appendHmsSettings(p);   /* no-op where the Printer Errors section does not exist */
  p.append('tz', document.getElementById('tz').value);
  if (document.getElementById('use24h').checked) p.append('use24h', '1');
  p.append('datefmt', document.getElementById('datefmt').value);
  p.append('clr_bg', document.getElementById('clr_bg').value);
  p.append('clr_track', document.getElementById('clr_track').value);
  p.append('clr_pbar', document.getElementById('clr_pbar').value);
  p.append('clr_eta', document.getElementById('clr_eta').value);
  p.append('clr_fin', document.getElementById('clr_fin').value);
  p.append('clr_stok', document.getElementById('clr_stok').value);
  p.append('clr_pname', document.getElementById('clr_pname').value);
  p.append('clr_txt', document.getElementById('clr_txt').value);
  p.append('clr_txtd', document.getElementById('clr_txtd').value);
  p.append('clr_dorc', document.getElementById('clr_dorc').value);
  p.append('clr_doro', document.getElementById('clr_doro').value);
  p.append('clk_time', document.getElementById('clk_time').value);
  p.append('clk_date', document.getElementById('clk_date').value);
  p.append('clk_size', document.getElementById('clk_size').value);
  p.append('clk_dsize', document.getElementById('clk_dsize').value);
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
/* Checkboxes pass a boolean; value pickers (e.g. round skin select) pass
   their string value through unchanged. */
function toggleSetting(key, on){
  var val = (on === true) ? '1' : (on === false) ? '0' : String(on);
  fetch('/save/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key='+key+'&val='+encodeURIComponent(val)})
    .then(function(r){if(r.ok)showToast(typeof on==='boolean'?(on?key+' ON':key+' OFF'):key+' saved');else showToast('Error');})
    .catch(function(e){showToast('Toggle failed');console.warn('toggleSetting:',e);});
}

/* ============ Printer errors (HMS + print_error) ============

   This file is shared by every board, but the Printer Errors section is not:
   flash-poor boards compile its markup out and keep only the on-screen badge.
   Every function here therefore checks that its markup exists first, and the
   two called from shared code - appendHmsSettings() and refreshErrorCard() -
   simply do nothing where it does not. */

function hmsHasSection(){ return !!document.getElementById('hmsauto'); }

/* The four alert checkboxes ride one bitmask, and it is always sent -
   including zero. Four unchecked boxes posting nothing at all would leave the
   old bits in place on the device. */
function hmsMaskValue(){
  var m = 0;
  for (var i = 0; i < 4; i++){
    var e = document.getElementById('hmsm' + i);
    if (e && e.checked) m |= (1 << i);
  }
  return m;
}
function applyHmsMask(){ toggleSetting('hmsmask', hmsMaskValue()); }

function toggleHmsFields(){
  var f = document.getElementById('hmsFields');
  var e = document.getElementById('hmsen');
  if (f && e) f.style.display = e.checked ? 'block' : 'none';
}

function appendHmsSettings(p){
  if (!hmsHasSection()) return;
  var en = document.getElementById('hmsen'), sv = document.getElementById('hmssev');
  if (en && en.checked) p.append('hmsen', '1');
  if (sv && sv.checked) p.append('hmssev', '1');
  var ol = document.getElementById('hmsonl');
  if (ol && ol.checked) p.append('hmsonl', '1');
  p.append('hmsauto', document.getElementById('hmsauto').value);
  p.append('hmsmask', String(hmsMaskValue()));
}

/* Live card. Text comes from the device where a table is compiled in; the rest
   is looked up in the published mirror, which the browser can reach and the
   firmware cannot (e.bambulab.com sends no CORS header, and the feed is
   750 KB). Fetched once per page load, never inside the poll.

   Boards carrying the full table (DEV.hmsFull) never fetch it: a code with no
   device text is blank in Bambu's feed, and the mirror is built from that same
   feed with the same blanks dropped, so the request could only ever fail to
   help. And the "hmsonl" setting lets anyone turn the lookup off - on an
   isolated network it can only fail, and some people want no third-party
   request from a LAN page at all. */
var HMS_MIRROR = 'https://keralots.github.io/BambuHelper/errors/hms_en.json';
var HMS_ON_DEVICE = (DEV.hmsFull === '1');
var _hmsMirror = null, _hmsTexts = null, _hmsLast = null;

/* Read live rather than cached: flipping the checkbox has to take effect on the
   next render, without a reload. Missing markup means an older page, where the
   lookup was unconditional. */
function hmsLookupAllowed(){
  if (HMS_ON_DEVICE) return false;
  var c = document.getElementById('hmsonl');
  return !c || c.checked;
}

/* Turning the lookup off has to drop what was already fetched, or the sentences
   stay on screen until a reload and the switch looks broken. Turning it back on
   clears the cached promise too, so the next render refetches. */
function hmsLookupChanged(){
  if (!hmsLookupAllowed()) _hmsTexts = null;
  _hmsMirror = null;
  refreshErrorCard();
}

function hmsMirror(){
  if (!hmsLookupAllowed()) return Promise.resolve(null);
  if (!_hmsMirror){
    _hmsMirror = fetch(HMS_MIRROR)
      .then(function(r){ return r.ok ? r.json() : null; })
      .catch(function(){ return null; });
  }
  return _hmsMirror;
}
function hmsSevName(s){ return ['Info','Fatal','Serious','Common'][s] || 'Info'; }
function hmsSevClr(s){
  return ['var(--text-dim)','var(--danger)','var(--warn)','var(--warn)'][s] || 'var(--text-dim)';
}

/* lbl overrides the severity word: a print_error carries no severity of its
   own, and a cancel is not an error at all. */
function hmsRow(code, sev, mod, text, base, wiki, lbl){
  var h = '<div style="padding:8px 0;border-top:1px solid var(--line-soft)">'
        + '<div class="hstack" style="flex-wrap:wrap">'
        + '<span style="font-weight:600;color:' + hmsSevClr(sev) + '">' + (lbl || hmsSevName(sev)) + '</span>'
        + (mod ? '<span class="text-dim small">' + esc(mod) + '</span>' : '')
        + '<span class="mono small">' + esc(code) + '</span>';
  if (base) h += '<span class="text-dim small" title="Already active when the device connected - listed, never alerts">standing</span>';
  if (wiki) h += '<a class="small" target="_blank" rel="noopener" href="' + wiki + '">wiki</a>';
  h += '</div>';
  if (text) h += '<div class="small" style="margin-top:2px;color:var(--text-mid)">' + esc(text) + '</div>';
  return h + '</div>';
}

/* The device now sends codes the way Bambu writes them - "HMS_0300-1A00-0002-0002"
   for HMS, "0300-8007" for print_error - because that is what a user can paste
   into a search box (issue #164). Both lookup maps are keyed on the bare 16 or 8
   hex digits, so strip the prefix and every separator before indexing them.
   Tolerates the old underscore form too: a page can outlive a firmware. */
function hmsBareCode(code){
  return String(code).replace(/^HMS[_-]/i, '').replace(/[_-]/g, '').toUpperCase();
}

function hmsText(map, code, dev){
  if (dev) return dev;
  if (!map) return '';
  var k = hmsBareCode(code);
  return map[k] || '';
}

/* Every per-code wiki page sits under a model path segment (x1, a1-mini, a2l,
   h2d, ams-ht, ...) that the code itself does not carry, so the URL cannot be
   built from the code alone - the old fixed /en/x1/ link 404'd for most codes.
   The mirror ships the path verbatim (the wiki's own separator and case; some
   of its links use "-" between the code groups, not "_"). Where the map is
   missing - a full-table board never fetches the mirror, and the text lookup
   can be switched off - link the HMS index, which lists every documented code
   and never 404s. Only ~224 of 2014 codes have a page at all, so the index is
   the common case either way. */
var HMS_WIKI_INDEX = 'https://wiki.bambulab.com/en/hms/home';
function hmsWikiUrl(code){
  var m = _hmsTexts && _hmsTexts.wiki, p = m && m[hmsBareCode(code)];
  return p ? 'https://wiki.bambulab.com/en/' + p : HMS_WIKI_INDEX;
}

function hmsRender(){
  var el = document.getElementById('hmsLive');
  if (!el || !_hmsLast) return;
  var h = '', need = false, m = _hmsTexts || {};

  for (var i = 0; i < _hmsLast.length; i++){
    var d = _hmsLast[i];
    if (!d || !d.configured) continue;
    var rows = '';

    if (d.printError){
      var t = hmsText(m.err, d.printError, d.printErrorText);
      if (!t && !d.printErrorText) need = true;
      rows += hmsRow(d.printError, d.printErrorCancel ? 0 : 1, '', t, false, '',
                     d.printErrorCancel ? 'Canceled' : 'Print error');
    }

    var a = d.hms || [];
    for (var j = 0; j < a.length; j++){
      var e = a[j], tx = hmsText(m.hms, e.code, e.text);
      if (!tx && !e.text) need = true;
      rows += hmsRow(e.code, e.sev, e.module, tx, e.baseline, hmsWikiUrl(e.code));
    }

    if (d.hmsOverflow) rows += '<div class="small text-dim" style="padding-top:6px">+' + d.hmsOverflow + ' more not kept</div>';
    if (rows) h += '<div style="margin-bottom:var(--sp-3)"><strong>' + esc(d.name || ('Printer ' + (i + 1))) + '</strong>' + rows + '</div>';
  }

  el.innerHTML = h || '<span class="text-dim">No errors reported.</span>';
  // Second guard at the call site as well as inside hmsMirror(): the card
  // re-renders on every 3 s poll, and a standing blank code would otherwise
  // build a throwaway promise each time. The third guard is inside the
  // continuation: a request already in flight when the user unticks the box
  // still resolves, and without the re-check it would put the sentences back
  // on screen after the switch said no.
  if (need && !_hmsTexts && hmsLookupAllowed())
    hmsMirror().then(function(j){
      if (j && hmsLookupAllowed()){ _hmsTexts = j; hmsRender(); }
    });
}

function refreshErrorCard(){
  if (!document.getElementById('hmsLive')) return;
  var q = [];
  for (var s = 0; s < 4; s++){
    q.push(fetch('/status?slot=' + s).then(function(r){ return r.json(); }).catch(function(){ return null; }));
  }
  Promise.all(q).then(function(all){ _hmsLast = all; hmsRender(); });
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
      if (d.no_data) h += '<div style="margin-bottom:var(--sp-2);padding:8px 10px;border:1px solid var(--danger);border-radius:6px;background:rgba(220,53,69,0.08);color:var(--danger);font-size:12.5px;line-height:1.5">Connected to Bambu, but no data received from the printer. Check: (1) the serial number is correct and UPPERCASE, (2) the printer is powered on, (3) for a Cloud printer, that the sign-in account owns this printer &mdash; not a second Bambu account (e.g. an Apple/Google login).</div>';
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
  var slotMax = parseInt(DEV.otaSlot, 10) || 1835008;
  if (f.size > slotMax) { showToast('File too large (max ' + (slotMax/1048576).toFixed(2).replace(/\.?0+$/,'') + 'MB)'); return; }
  var lowerName = f.name.toLowerCase();
  var board = DEV.board.toLowerCase();
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
      if (d.status === 'ok'){ bar.style.width = '100%'; pct.textContent = '100%'; stat.style.color = 'var(--success)'; stat.textContent = d.message; waitForReboot(stat); }
      else { var msg = d.message || 'Firmware update failed'; if (msg === 'Invalid firmware file') msg = 'Invalid firmware file or wrong board build'; stat.style.color = 'var(--danger)'; stat.textContent = 'Update failed: ' + msg; }
    } catch(e) { stat.style.color = 'var(--danger)'; stat.textContent = 'Update failed: unexpected response'; }
  };
  xhr.onerror = function(){ stat.style.color = 'var(--danger)'; stat.textContent = 'Update failed: upload interrupted or connection lost'; };
  xhr.send(fd);
}

// Poll the device until it reboots and serves again, then reload the page.
// Used by both the manual (.bin upload) and online-update paths. Poll one
// request at a time: setInterval would stack up probes that each hang for the
// full TCP timeout while the device is offline, and a fresh success could
// resolve before those pending catches flip wentOffline - leaving the gate
// false so we never reload. A 3s AbortController makes a hang count as offline
// quickly; no-store + a cache-buster stop the browser serving a cached 200. We
// reload only after seeing the device drop AND return, never into the old fw.
function waitForReboot(st){
  st.textContent = 'Waiting for device to restart...';
  var wentOffline = false, elapsed = 0;
  (function poll(){
    if (elapsed >= 90){ st.textContent = 'Reboot timeout - please refresh manually.'; return; }
    var ctrl = new AbortController();
    var to = setTimeout(function(){ ctrl.abort(); }, 3000);
    fetch('/?_=' + Date.now(), {cache:'no-store', signal:ctrl.signal})
      .then(function(r){
        clearTimeout(to);
        if (!r.ok) throw 0;
        if (wentOffline){ location.reload(); return; }
        elapsed += 2; setTimeout(poll, 2000);
      })
      .catch(function(){
        clearTimeout(to);
        wentOffline = true; elapsed += 2;
        st.textContent = 'Restarting... (' + elapsed + 's)';
        setTimeout(poll, 2000);
      });
  })();
}

/* Rollback: the device tells us whether the inactive app slot holds a
   different, bootable firmware; if so the "Previous firmware" block appears.
   The slot's version is only known when that firmware was new enough to
   record it, hence the two phrasings. */
function initRollback(){
  if (!document.getElementById('rollbackWrap')) return;
  fetch('/ota/slots').then(function(r){return r.json();}).then(function(d){
    if (!d.can) return;
    document.getElementById('rollbackInfo').textContent = d.fw
      ? 'Version ' + d.fw + ' is still installed in the second update slot.'
      : 'An older firmware build is still installed in the second update slot.';
    document.getElementById('rollbackWrap').style.display = 'block';
  }).catch(function(e){console.warn('rollback info:',e);});
}
function otaRollback(){
  if (!confirm('Reboot into the firmware in the other update slot? Settings are kept, but options added by the newer version may reset, and its cloud token storage is not readable by older versions (re-paste the token there if cloud stops working). You can update again at any time.')) return;
  var st = document.getElementById('rollbackStatus');
  st.style.color = 'var(--info)'; st.textContent = 'Verifying the other slot...';
  fetch('/ota/rollback',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    if (d.status === 'ok'){ st.style.color = 'var(--success)'; st.textContent = d.message; waitForReboot(st); }
    else { st.style.color = 'var(--danger)'; st.textContent = d.message || 'Rollback failed'; }
  }).catch(function(e){ st.style.color = 'var(--danger)'; st.textContent = 'Rollback failed: network error'; console.warn('rollback:',e); });
}

/* Online-update helpers. The markup that calls them is still built only where
   ENABLE_OTA_AUTO is set; this file ships to every board, so on the others the
   functions simply have no buttons to be reached from. */
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
      var latest = d.tag_name, current = DEV.fw;
      function parseVer(v){ var m = v.replace(/^v/,'').match(/^(\d+)\.(\d+)(?:\.(\d+))?(.*)$/); return m ? {major:parseInt(m[1]),minor:parseInt(m[2]),patch:m[3]?parseInt(m[3]):0,pre:m[4]!==''} : null; }
      function isNewer(a, b){ var av = parseVer(a), bv = parseVer(b); if (!av || !bv) return a !== b; if (av.major !== bv.major) return av.major > bv.major; if (av.minor !== bv.minor) return av.minor > bv.minor; if (av.patch !== bv.patch) return av.patch > bv.patch; return !av.pre && bv.pre; }
      if (!isNewer(latest, current)){ res.style.color = 'var(--success)'; res.textContent = latest === current ? 'You are up to date (' + current + ')' : 'Running newer version (' + current + ')'; return; }
      var board = DEV.board, expectedPrefix = 'BambuHelper-' + board + '-', otaBin = null;
      for (var i = 0; i < d.assets.length; i++){ var n = d.assets[i].name; if (n.startsWith(expectedPrefix) && n.endsWith('-ota.bin')){ otaBin = d.assets[i]; break; } }
      var slotB = parseInt(DEV.otaSlot, 10) || 0, flashMb = parseInt(DEV.flashMb, 10) || 0;
      if (otaBin && slotB > 0 && otaBin.size > slotB){
        res.style.color = 'var(--danger)';
        res.textContent = 'Update ' + latest + ' (' + Math.round(otaBin.size/1024) + ' KB) does not fit the ' + Math.round(slotB/1024) + ' KB update slot on this device.' +
          (flashMb >= 16 ? ' Back up settings (Export), then reflash once via the web flasher to repartition the ' + flashMb + ' MB chip - OTA works normally afterwards.'
                         : ' This board cannot hold this update - stay on the current version.');
        return;
      }
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
      if (d.status === 'done'){ clearInterval(_otaPoller); _otaPoller = null; bar.style.width = '100%'; st.style.color = 'var(--success)'; st.textContent = 'Done! Restarting device...'; waitForReboot(st); }
      else if (d.status && d.status.indexOf('failed') === 0){ clearInterval(_otaPoller); _otaPoller = null; st.style.color = 'var(--danger)'; st.textContent = d.status; var btn = document.getElementById('installBtn'); btn.disabled = false; btn.textContent = 'Retry'; }
      else { st.textContent = d.status + ' (' + d.progress + '%)'; }
    }).catch(function(){
      if (_autoOtaProgress >= 90){ clearInterval(_otaPoller); _otaPoller = null; var bar = document.getElementById('autoOtaBar'), st = document.getElementById('autoOtaStatus'); bar.style.width = '100%'; st.style.color = 'var(--success)'; st.textContent = 'Done! Restarting device...'; waitForReboot(st); }
    });
  }, 1000);
}



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
  // Destination select is meaningless when the finish screen never times out.
  document.getElementById('afterFinWrap').style.display = (v === 'keepon') ? 'none' : 'block';
  var pong = document.getElementById('pong');
  var row = document.getElementById('pong-row');
  var showClock = (v !== 'keepon') && document.getElementById('afterfin').value !== 'off';
  pong.disabled = !showClock;
  if (row) row.style.opacity = showClock ? '1' : '0.4';
}

/* ============ Edge glow reveal ============ */
function toggleGlowFields(){
  var m = document.getElementById('glowm').value;
  document.getElementById('glowFields').style.display = (m === '0') ? 'none' : 'block';
  document.getElementById('glowClrWrap').style.display = (m === '1') ? 'block' : 'none';
}
function glowTestNow(){
  /* Send the current picker color so the preview matches even before Save. */
  var p = new URLSearchParams();
  p.append('clr', document.getElementById('glow_clr').value);
  fetch('/glow/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});
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



  clInitAuthUi();



  toggleConnMode();
  toggleStatic();
  toggleBtnPin();
  toggleLed();
  toggleAfterPrint();
  // Initial section: URL hash, else last visited (localStorage), else printer.
  // Printer Errors only exists on boards that compiled its markup in, so it
  // joins the list only when the page actually carries it.
  var SECTIONS = ['printer','display','hardware','wifi','power','diag'];
  if (document.getElementById('sec-errors')) SECTIONS.push('errors');
  var initId = 'printer';
  if (location.hash){
    var h = location.hash.substring(1);
    if (SECTIONS.indexOf(h) >= 0) initId = h;
  } else {
    try { var saved = localStorage.getItem('bambu_section'); if (saved && SECTIONS.indexOf(saved) >= 0) initId = saved; } catch(e){}
  }
  loadSection(initId);
  applyLabelMaxlen();
  setTimeout(function(){ selectPrinterTab(0); }, 80);
  setTimeout(function(){ selectPowerTab(0); }, 140);
  refreshHwInfo();
  initRollback();
  refreshTopStatusDots();
  setInterval(refreshTopStatusDots, 5000);
})();
