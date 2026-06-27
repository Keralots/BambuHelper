// BambuHelper Web Flasher - client logic.
// Builds ESP Web Tools manifests on the fly from a single board map, and
// keeps the install button in sync with the user's board selection.

// Order: DIY builds grouped at the top (the "you wired this yourself" path),
// then all-in-one boards, then the two CYD-style boards differentiated by
// their display driver so users don't pick the wrong one.
const BOARDS = {
  esp32s3: {
    chipFamily: 'ESP32-S3',
    label: 'DIY - ESP32-S3 SuperMini + 1.54" ST7789',
  },
  esp32s3_zero: {
    chipFamily: 'ESP32-S3',
    label: 'DIY - Waveshare ESP32-S3-Zero + 1.54" ST7789',
  },
  esp32s3_zero_320: {
    chipFamily: 'ESP32-S3',
    label: 'DIY - Waveshare ESP32-S3-Zero + 2.0" ST7789 (240x320)',
  },
  esp32c3: {
    chipFamily: 'ESP32-C3',
    label: 'DIY - ESP32-C3 SuperMini + 1.54" ST7789',
  },
  ws_lcd_200: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-2 (240x320)',
  },
  ws_lcd_154: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-1.54 (240x240)',
  },
  ws_lcd_280: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-2.8 (240x320)',
  },
  ws_lcd_350: {
    chipFamily: 'ESP32-S3',
    label: 'Waveshare ESP32-S3-Touch-LCD-3.5 (320x480) - community, untested (not owned by maintainer)',
  },
  jc3248w535: {
    chipFamily: 'ESP32-S3',
    label: 'Guition JC3248W535 (320x480, AXS15231B QSPI)',
  },
  cyd: {
    chipFamily: 'ESP32',
    label: 'CYD / ESP32-2432S028 (ILI9341, 240x320)',
  },
  tzt_2432: {
    chipFamily: 'ESP32',
    label: 'CYD / TZT L1435-2.4 (ST7789, 240x320)',
  },
};

const DEFAULT_BOARD = 'esp32s3';

let _version = null;
let _currentManifestUrl = null;

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
    // i.e. the "recommended" path in section 02 of index.html.
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

function renderSpecs(boardId) {
  const info = BOARDS[boardId];
  document.getElementById('spec-chip').textContent = info.chipFamily;
  document.getElementById('spec-id').textContent = boardId;
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
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = version;
}

function showVersionError(err) {
  document.getElementById('spec-version').textContent = 'unavailable';
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = 'unavailable';
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
  renderSpecs(DEFAULT_BOARD);
  wireMonitor();

  try {
    _version = await loadVersion();
  } catch (err) {
    showVersionError(err);
    return;
  }

  showVersion(_version);
  renderInstallButton(DEFAULT_BOARD, _version);

  document.getElementById('board-select').addEventListener('change', (e) => {
    const boardId = e.target.value;
    renderSpecs(boardId);
    renderInstallButton(boardId, _version);
  });
}

// ────────── 03 serial monitor ──────────
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
