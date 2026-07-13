// BambuHelper Web Flasher - client logic.
// Builds ESP Web Tools manifests on the fly from a single board map, keeps
// the install button in sync with the board selection, and renders the
// selected board's photo/specs/description card.

// Order: DIY builds grouped at the top (the "you wired this yourself" path),
// then all-in-one boards, then the two CYD-style boards differentiated by
// their display driver so users don't pick the wrong one.
//
// Optional per-board fields:
//   version - pins this board to a firmware version ahead of the global
//             VERSION file (interim releases); remove once release.py
//             publishes that version for every board.
//   credit  - community contributor shown on the board card.
const BOARDS = {
  esp32s3: {
    chipFamily: 'ESP32-S3',
    label: 'DIY - ESP32-S3 SuperMini + 1.54" ST7789',
    name: 'ESP32-S3 Super Mini + 1.54" ST7789',
    badge: 'diy',
    display: '1.54" ST7789, 240x240, SPI',
    touch: 'none (optional button / TTP223)',
    printers: 'up to 2',
    desc: 'The original DIY build this project started from: an ESP32-S3 ' +
          'Super Mini wired to a 1.54" ST7789 TFT. Cheap, small, and easy ' +
          'to solder - follow the wiring table in the README.',
    links: [
      { t: 'wiring guide', u: 'https://github.com/Keralots/BambuHelper#default-wiring' },
    ],
  },
  esp32s3_round: {
    chipFamily: 'ESP32-S3',
    label: 'DIY - ESP32-S3 SuperMini + 1.28" GC9A01 round',
    name: 'ESP32-S3 Super Mini + 1.28" GC9A01 round',
    badge: 'diy',
    display: '1.28" GC9A01, 240x240 round, SPI',
    touch: 'none (optional button / TTP223)',
    printers: 'up to 2',
    desc: 'DIY round variant with a dedicated circular dashboard and three ' +
          'selectable print skins (Rim / Speedo / Rings). Same SPI wiring as ' +
          'the square ST7789 builds. The 7-pin module has no backlight pin, ' +
          'so brightness control and night dimming are unavailable.',
    links: [
      { t: 'display module (pick Type Y)', u: 'https://aliexpress.com/item/1005007702290129.html' },
      { t: '3D-printable case', u: 'https://makerworld.com/en/models/3037776' },
      { t: 'wiring guide', u: 'https://github.com/Keralots/BambuHelper#default-wiring' },
    ],
  },
  esp32s3_zero: {
    chipFamily: 'ESP32-S3',
    label: 'DIY - Waveshare ESP32-S3-Zero + 1.54" ST7789',
    name: 'Waveshare ESP32-S3-Zero + 1.54" ST7789',
    badge: 'diy',
    display: '1.54" ST7789, 240x240, SPI',
    touch: 'none (optional button / TTP223)',
    printers: 'up to 2 (opt-in 4)',
    desc: 'Same external ST7789 wiring as the Super Mini build, on ' +
          'Waveshare\'s tiny S3-Zero module (4MB flash, 2MB PSRAM). GPIO21 ' +
          'is taken by the onboard WS2812 LED.',
    links: [
      { t: 'product page', u: 'https://www.waveshare.com/esp32-s3-zero.htm' },
      { t: 'wiring guide', u: 'https://github.com/Keralots/BambuHelper#default-wiring' },
    ],
  },
  esp32s3_zero_320: {
    chipFamily: 'ESP32-S3',
    label: 'DIY - Waveshare ESP32-S3-Zero + 2.0" ST7789 (240x320)',
    name: 'Waveshare ESP32-S3-Zero + 2.0" ST7789V',
    badge: 'diy',
    display: '2.0" ST7789V, 240x320, SPI',
    touch: 'none (optional button / TTP223)',
    printers: 'up to 2 (opt-in 4)',
    desc: 'The S3-Zero build with a bigger 2.0" 240x320 panel - identical ' +
          'pinout to the standard esp32s3_zero build, only the panel differs.',
    links: [
      { t: 'display module', u: 'https://pl.aliexpress.com/item/1005007523612119.html' },
      { t: 'wiring guide', u: 'https://github.com/Keralots/BambuHelper#default-wiring' },
    ],
  },
  esp32c3: {
    chipFamily: 'ESP32-C3',
    label: 'DIY - ESP32-C3 SuperMini + 1.54" ST7789',
    name: 'ESP32-C3 Super Mini + 1.54" ST7789',
    badge: 'diy',
    display: '1.54" ST7789, 240x240, SPI',
    touch: 'none (optional button / TTP223)',
    printers: '1 (RAM limit)',
    desc: 'Budget DIY build using the same 1.54" ST7789 display and wiring ' +
          'as the ESP32-S3 version. RAM limits it to a single printer.',
    links: [
      { t: 'wiring guide', u: 'https://github.com/Keralots/BambuHelper#default-wiring' },
    ],
  },
  esp32c3_round: {
    chipFamily: 'ESP32-C3',
    label: 'DIY - ESP32-C3 SuperMini + 1.28" GC9A01 round',
    name: 'ESP32-C3 Super Mini + 1.28" GC9A01 round',
    badge: 'diy',
    display: '1.28" GC9A01, 240x240 round, SPI',
    touch: 'none (optional button / TTP223)',
    printers: '1 (RAM limit)',
    desc: 'The round GC9A01 build on the budget ESP32-C3 Super Mini - ' +
          'circular dashboard with three selectable print skins (Rim / ' +
          'Speedo / Rings). Same SPI wiring as the square builds. The 7-pin ' +
          'module has no backlight pin, so brightness control and night ' +
          'dimming are unavailable.',
    links: [
      { t: 'display module (pick Type Y)', u: 'https://aliexpress.com/item/1005007702290129.html' },
      { t: '3D-printable case', u: 'https://makerworld.com/en/models/3037776' },
      { t: 'wiring guide', u: 'https://github.com/Keralots/BambuHelper#default-wiring' },
    ],
  },
  ws_lcd_200: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-2 (240x320)',
    name: 'Waveshare ESP32-S3-Touch-LCD-2',
    display: '2.0" ST7789, 240x320, SPI',
    touch: 'CST816D capacitive',
    printers: 'up to 2',
    desc: 'All-in-one board and the most plug-and-play option - nothing to ' +
          'wire. Touchscreen switches printers/screens, battery operation ' +
          'is supported.',
    links: [
      { t: 'product page', u: 'https://www.waveshare.com/esp32-s3-touch-lcd-2.htm' },
      { t: '3D-printable case', u: 'https://makerworld.com/en/models/2773835' },
    ],
  },
  ws_lcd_154: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-1.54 (240x240)',
    name: 'Waveshare ESP32-S3-Touch-LCD-1.54',
    display: '1.54" ST7789, 240x240, SPI',
    touch: 'CST816T capacitive + 3 buttons',
    printers: 'up to 2',
    desc: 'All-in-one board with touchscreen, three buttons, speaker, and a ' +
          'battery holder. Hold the center PWR button to power on; hold the ' +
          'left and right buttons for ~1.5 s to power off. The left (BOOT) ' +
          'button switches screens.',
    links: [
      { t: 'product page', u: 'https://www.waveshare.com/esp32-s3-touch-lcd-1.54.htm' },
    ],
  },
  ws_lcd_280: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-2.8 (240x320) - community (not owned by maintainer)',
    name: 'Waveshare ESP32-S3-Touch-LCD-2.8',
    badge: 'community',
    display: '2.8" ST7789, 240x320, SPI',
    touch: 'CST328 capacitive',
    printers: 'up to 2',
    desc: 'Like the 2.0" board but with a CST328 touch controller - the ' +
          'ws_lcd_200 firmware boots on it but leaves the screen black, so ' +
          'use this build. Battery, IMU, and audio are not wired up in ' +
          'firmware.',
    links: [
      { t: 'product page', u: 'https://www.waveshare.com/esp32-s3-touch-lcd-2.8.htm' },
    ],
    credit: { name: '@FranciscoSaoMarcos', u: 'https://github.com/FranciscoSaoMarcos' },
  },
  es3n28p: {
    chipFamily: 'ESP32-S3',
    label: 'QD ES3N28P 2.8" (240x320, ILI9341V) - community (not owned by maintainer)',
    name: 'QD ES3N28P 2.8"',
    badge: 'community',
    display: '2.8" ILI9341V IPS, 240x320, SPI',
    touch: 'FT6336 capacitive',
    printers: 'up to 2 (opt-in 4)',
    desc: 'QD electronic 2.8" IPS module (board codes ES3C28P / ES3N28P) ' +
          'with 16MB flash and 8MB PSRAM. The buzzer is disabled by default ' +
          'because its default pin doubles as an I2S clock line.',
    links: [
      { t: 'hardware notes (#125)', u: 'https://github.com/Keralots/BambuHelper/issues/125' },
    ],
    credit: { name: '@gwbuss', u: 'https://github.com/gwbuss' },
  },
  sc05_x: {
    chipFamily: 'ESP32-S3',
    label: 'Panlee SC05_X / ZX2D80CE02S 2.8" (240x320, ST7789 8080) - experimental',
    name: 'Panlee SC05_X / ZX2D80CE02S 2.8"',
    badge: 'community',
    display: '2.8" ST7789 IPS, 240x320, 8-bit parallel',
    touch: 'FT5X06 capacitive',
    printers: 'up to 2 (opt-in 4)',
    desc: 'Smartpanle/Panlee SC05_X board (model ZX2D80CE02S / WT32S3-28S ' +
          'PRO) with 8MB flash and QSPI PSRAM. This is not the 3.5" ' +
          'WT32-SC01 Plus; that firmware drives a different panel and pin map ' +
          'and leaves this board on a black screen.',
    links: [
      { t: 'vendor Arduino library', u: 'https://github.com/smartpanle/PanelLan_esp32_arduino' },
    ],
    credit: { name: '@nunnypern', u: 'https://github.com/nunnypern' },
  },
  ws_lcd_350: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-3.5 (320x480) - community (not owned by maintainer)',
    name: 'Waveshare ESP32-S3-Touch-LCD-3.5',
    badge: 'community',
    display: '3.5" ST7796 IPS, 320x480, SPI',
    touch: 'FT6336 capacitive',
    printers: 'up to 2 (opt-in 4)',
    desc: 'Waveshare\'s 3.5" all-in-one with ESP32-S3R8 (16MB flash, 8MB ' +
          'PSRAM). Uses the same 320x480 layout as the Guition board. The ' +
          'board has no buzzer hardware.',
    links: [
      { t: 'product page', u: 'https://www.waveshare.com/product/esp32-s3-touch-lcd-3.5.htm' },
    ],
  },
  wt32_sc01_plus: {
    chipFamily: 'ESP32-S3',
    label: 'Panlee WT32-SC01 Plus 3.5" (320x480) - community (not owned by maintainer)',
    name: 'Panlee WT32-SC01 Plus 3.5"',
    badge: 'community',
    display: '3.5" ST7796 IPS, 320x480, 8-bit parallel',
    touch: 'FT6336 capacitive',
    printers: 'up to 2 (opt-in 4)',
    desc: 'Sold as the WT32-SC01 Plus - a 3.5" all-in-one with 16MB flash ' +
          'and 8MB PSRAM, and the project\'s first parallel-bus display. ' +
          'The buzzer is disabled by default because its default pin is the ' +
          'touch I2C clock line.',
    links: [
      { t: 'hardware notes (#123)', u: 'https://github.com/Keralots/BambuHelper/issues/123' },
    ],
    credit: { name: '@cliomjh', u: 'https://github.com/cliomjh' },
  },
  jc3248w535: {
    chipFamily: 'ESP32-S3',
    label: 'Guition JC3248W535 (320x480, AXS15231B QSPI)',
    name: 'Guition JC3248W535',
    display: '3.5" IPS AXS15231B, 320x480, QSPI',
    touch: 'capacitive (in display IC)',
    printers: 'up to 2 (opt-in 4)',
    desc: 'All-in-one 320x480 IPS board with 16MB flash and 8MB PSRAM, an ' +
          'onboard NS4168 speaker for notifications, and a battery ' +
          'indicator. Supports portrait and landscape layouts. Initial port ' +
          'contributed by Niels.',
    links: [
      { t: 'AliExpress', u: 'https://pl.aliexpress.com/item/1005007566315926.html' },
    ],
    credit: { name: '@theNailz', u: 'https://github.com/theNailz' },
  },
  cyd: {
    chipFamily: 'ESP32',
    label: 'CYD / ESP32-2432S028 (ILI9341, 240x320)',
    name: 'CYD / ESP32-2432S028',
    display: '2.8" ILI9341, 240x320, SPI',
    touch: 'XPT2046 resistive',
    printers: '1 (RAM limit)',
    desc: 'The classic "Cheap Yellow Display". If colors look inverted ' +
          'after flashing (white background instead of dark), enable ' +
          '"Invert display colors" under Display in the web UI.',
    links: [
      { t: '3D-printable case', u: 'https://makerworld.com/models/2721746' },
    ],
  },
  tzt_2432: {
    chipFamily: 'ESP32',
    label: 'CYD / TZT L1435-2.4 (ST7789, 240x320) - community (not owned by maintainer)',
    name: 'CYD / TZT L1435-2.4',
    badge: 'community',
    display: '2.4" ST7789V, 240x320, SPI',
    touch: 'XPT2046 resistive',
    printers: '1 (RAM limit)',
    desc: 'Looks nearly identical to the standard CYD but uses an ST7789V ' +
          'panel with the backlight on GPIO27 - the regular cyd build gives ' +
          'a black screen on this hardware. Often sold on AliExpress as ' +
          '"TZT ESP32 LVGL 2.4 inch LCD TFT 240*320 With Touch".',
    links: [],
  },
};

const DEFAULT_BOARD = 'esp32s3';

let _version = null;
let _currentManifestUrl = null;

// Boards may pin their own version (interim release ahead of the full set);
// everything else follows the global VERSION file.
function effectiveVersion(boardId) {
  return BOARDS[boardId].version || _version;
}

async function loadVersion() {
  const r = await fetch('firmware/latest/VERSION', { cache: 'no-cache' });
  if (!r.ok) {
    throw new Error(`firmware/latest/VERSION returned HTTP ${r.status}`);
  }
  const text = (await r.text()).trim();
  if (!text) {
    throw new Error('VERSION file is empty');
  }
  return text;
}

function buildManifest(boardId, version) {
  const board = BOARDS[boardId];
  const binUrl = new URL(
    `firmware/latest/BambuHelper-${boardId}-${version}-Full.bin`,
    location.href,
  ).href;
  return {
    name: 'BambuHelper',
    version,
    new_install_prompt_erase: true,
    // After flashing, wait up to 15s for the device to boot, then probe for
    // Improv-Serial. The firmware exposes Improv only on first boot (no
    // stored WiFi credentials), so this kicks in for fresh installs and
    // lets ESP Web Tools show the "Configure WiFi" dialog in-browser -
    // i.e. the "recommended" path in the After flashing card.
    new_install_improv_wait_time: 15,
    builds: [{
      chipFamily: board.chipFamily,
      parts: [{ path: binUrl, offset: 0 }],
    }],
  };
}

function manifestBlobUrl(boardId, version) {
  if (_currentManifestUrl) {
    URL.revokeObjectURL(_currentManifestUrl);
    _currentManifestUrl = null;
  }
  const blob = new Blob(
    [JSON.stringify(buildManifest(boardId, version))],
    { type: 'application/json' },
  );
  _currentManifestUrl = URL.createObjectURL(blob);
  return _currentManifestUrl;
}

function populateBoardSelect() {
  const sel = document.getElementById('board-select');
  for (const [id, info] of Object.entries(BOARDS)) {
    const opt = document.createElement('option');
    opt.value = id;
    opt.textContent = info.label;
    sel.appendChild(opt);
  }
  sel.value = DEFAULT_BOARD;
}

function renderBoardCard(boardId) {
  const info = BOARDS[boardId];

  const img = document.getElementById('board-img');
  img.src = `img/boards/${boardId}.jpg`;
  img.alt = info.name;

  document.getElementById('board-name').textContent = info.name;

  const badge = document.getElementById('board-badge');
  if (info.badge === 'community') {
    badge.hidden = false;
    badge.textContent = 'community';
    badge.classList.remove('badge-diy');
    badge.title = 'Community-contributed board, hardware not owned by the maintainer';
  } else if (info.badge === 'diy') {
    badge.hidden = false;
    badge.textContent = 'diy build';
    badge.classList.add('badge-diy');
    badge.title = 'Requires wiring a display module to the dev board yourself';
  } else {
    badge.hidden = true;
  }

  document.getElementById('board-desc').textContent = info.desc;
  document.getElementById('spec-chip').textContent = info.chipFamily;
  document.getElementById('spec-display').textContent = info.display;
  document.getElementById('spec-touch').textContent = info.touch;
  document.getElementById('spec-printers').textContent = info.printers;
  document.getElementById('spec-id').textContent = boardId;
  if (_version) {
    document.getElementById('spec-version').textContent = effectiveVersion(boardId);
  }

  const links = document.getElementById('board-links');
  links.innerHTML = '';
  for (const l of (info.links || [])) {
    const a = document.createElement('a');
    a.href = l.u;
    a.target = '_blank';
    a.rel = 'noopener';
    a.textContent = l.t + ' ↗';
    links.appendChild(a);
  }
  if (info.credit) {
    const span = document.createElement('span');
    span.className = 'credit';
    span.append('contributed by ');
    const a = document.createElement('a');
    a.href = info.credit.u;
    a.target = '_blank';
    a.rel = 'noopener';
    a.textContent = info.credit.name;
    span.appendChild(a);
    links.appendChild(span);
  }
}

function renderInstallButton(boardId, version) {
  // ESP Web Tools caches the manifest the first time the button is rendered.
  // Recreate the element on every board switch so the new manifest is picked up.
  const slot = document.getElementById('install-slot');
  slot.innerHTML = '';
  const btn = document.createElement('esp-web-install-button');
  btn.setAttribute('manifest', manifestBlobUrl(boardId, version));
  // Fallback content for browsers without Web Serial.
  const fallback = document.createElement('span');
  fallback.setAttribute('slot', 'unsupported');
  fallback.className = 'unsupported';
  fallback.textContent =
    'Your browser does not support Web Serial. Use Chrome or Edge on desktop.';
  btn.appendChild(fallback);
  const notAllowed = document.createElement('span');
  notAllowed.setAttribute('slot', 'not-allowed');
  notAllowed.className = 'unsupported';
  notAllowed.textContent =
    'Web Serial requires a secure context (HTTPS). Open this page from https://.';
  btn.appendChild(notAllowed);
  slot.appendChild(btn);
}

function showStatus(message, kind) {
  const line = document.getElementById('status-line');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function showVersion(version) {
  document.getElementById('spec-version').textContent = version;
  for (const id of ['rail-version', 'sidebar-version']) {
    const el = document.getElementById(id);
    if (el) el.textContent = version;
  }
}

function showVersionError(err) {
  document.getElementById('spec-version').textContent = 'unavailable';
  for (const id of ['rail-version', 'sidebar-version']) {
    const el = document.getElementById(id);
    if (el) el.textContent = 'unavailable';
  }
  showStatus(
    `Could not load firmware version (${err.message}). The site may be mid-deploy - try again in a minute.`,
    'error',
  );
  document.getElementById('install-slot').innerHTML = '';
}

function checkBrowserSupport() {
  // Show the desktop-only callout for browsers without Web Serial.
  // Done here (not just via the button's <slot="unsupported">) so mobile users
  // see a clear, intentional message before scrolling through the page.
  if (!('serial' in navigator)) {
    document.getElementById('browser-callout').classList.add('show');
  }
}

async function init() {
  checkBrowserSupport();
  populateBoardSelect();
  renderBoardCard(DEFAULT_BOARD);
  wireMonitor();

  try {
    _version = await loadVersion();
  } catch (err) {
    showVersionError(err);
    return;
  }

  showVersion(_version);
  renderBoardCard(DEFAULT_BOARD);
  renderInstallButton(DEFAULT_BOARD, effectiveVersion(DEFAULT_BOARD));

  document.getElementById('board-select').addEventListener('change', (e) => {
    const boardId = e.target.value;
    renderBoardCard(boardId);
    renderInstallButton(boardId, effectiveVersion(boardId));
  });
}

// ────────── serial monitor ──────────
// Reads the device's USB CDC stream at 115200 baud and appends decoded text
// to <pre id="monitor-output">. Independent of the ESP Web Tools install
// button - only one program can hold the port at a time, so users must
// not click Install while connected here.

let _monitorPort = null;
let _monitorReader = null;
let _monitorReadLoopRunning = false;

async function monitorConnect() {
  if (_monitorPort) return;
  let port;
  try {
    port = await navigator.serial.requestPort();
  } catch (err) {
    if (err && err.name === 'NotFoundError') return; // user cancelled picker
    setMonitorStatus(`Could not pick a port: ${err.message}`, 'error');
    return;
  }
  try {
    await port.open({ baudRate: 115200 });
  } catch (err) {
    setMonitorStatus(
      `Could not open the port: ${err.message}. Close other monitors and try again.`,
      'error',
    );
    return;
  }
  _monitorPort = port;
  toggleMonitorButtons(true);
  setMonitorStatus('Connected. Reading from device...', 'ok');
  monitorReadLoop().catch((err) => {
    setMonitorStatus(`Read error: ${err.message}`, 'error');
  });
}

async function monitorDisconnect() {
  if (!_monitorPort) return;
  setMonitorStatus('Disconnecting...');
  try { if (_monitorReader) await _monitorReader.cancel(); } catch (_) {}
  // Wait up to 1s for the read loop to exit. Watchdog avoids ever hanging
  // the page if the reader misbehaves.
  const startedAt = Date.now();
  while (_monitorReadLoopRunning && Date.now() - startedAt < 1000) {
    await new Promise((r) => setTimeout(r, 20));
  }
  try { await _monitorPort.close(); } catch (_) {}
  _monitorPort = null;
  _monitorReader = null;
  toggleMonitorButtons(false);
  setMonitorStatus('Disconnected.');
}

async function monitorReadLoop() {
  _monitorReadLoopRunning = true;
  const decoder = new TextDecoder();
  try {
    if (!_monitorPort || !_monitorPort.readable) return;
    const reader = _monitorPort.readable.getReader();
    _monitorReader = reader;
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.byteLength) {
          appendMonitorOutput(decoder.decode(value, { stream: true }));
        }
      }
    } finally {
      try { reader.releaseLock(); } catch (_) {}
      _monitorReader = null;
    }
  } finally {
    _monitorReadLoopRunning = false;
  }
}

function appendMonitorOutput(text) {
  const out = document.getElementById('monitor-output');
  const wasEmpty = out.textContent.length === 0;
  const atBottom = out.scrollHeight - out.clientHeight - out.scrollTop < 4;
  out.appendChild(document.createTextNode(text));
  // Cap buffer at ~200 KB so a long debug session doesn't lock the tab.
  if (out.textContent.length > 200000) {
    out.textContent = out.textContent.slice(-150000);
  }
  if (atBottom) out.scrollTop = out.scrollHeight;
  if (wasEmpty) setMonitorBufferButtons(true);
}

function monitorExport() {
  const out = document.getElementById('monitor-output');
  const text = out.textContent;
  if (!text) return;
  const ts = new Date().toISOString().replace(/[:.]/g, '-').replace('Z', '');
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `bambuhelper-serial-${ts}.txt`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  // Defer revoke so the browser has time to start the download.
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function monitorClear() {
  document.getElementById('monitor-output').textContent = '';
  setMonitorBufferButtons(false);
}

function setMonitorBufferButtons(hasContent) {
  document.getElementById('monitor-export').disabled = !hasContent;
  document.getElementById('monitor-clear').disabled = !hasContent;
}

function setMonitorStatus(message, kind) {
  const line = document.getElementById('monitor-status');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function toggleMonitorButtons(connected) {
  document.getElementById('monitor-connect').disabled = connected;
  document.getElementById('monitor-disconnect').disabled = !connected;
}

function wireMonitor() {
  const connectBtn = document.getElementById('monitor-connect');
  if (!('serial' in navigator)) {
    connectBtn.disabled = true;
    setMonitorStatus(
      'Web Serial is unavailable in this browser - use desktop Chrome or Edge.',
      'warn',
    );
    return;
  }
  connectBtn.addEventListener('click', monitorConnect);
  document.getElementById('monitor-disconnect').addEventListener('click', monitorDisconnect);
  document.getElementById('monitor-export').addEventListener('click', monitorExport);
  document.getElementById('monitor-clear').addEventListener('click', monitorClear);
}

init();
