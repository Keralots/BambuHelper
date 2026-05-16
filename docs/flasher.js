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

init();
