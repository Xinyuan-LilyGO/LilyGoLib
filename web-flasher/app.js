/**
 * @file      app.js
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-28
 *
 * Manage multilingual device selection, serial connections, and firmware writes.
 */
import { getLanguage, setLanguage, t } from './i18n.js';

const ESPTOOL_URL = 'https://unpkg.com/esptool-js@latest/bundle.js';

const firmwareRoot = '../firmware/';

const devices = [
  {
    id: 'lora-pager',
    name: 'T-LoRa-Pager',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.loraPager',
    firmwares: [],
  },
  {
    id: 'vibe-pager',
    name: 'T-Vibe-Pager',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.vibePager',
    firmwares: [],
  },
  {
    id: 'watch-ultra',
    name: 'T-Watch Ultra',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.ultra',
    firmwares: [],
  },
  {
    id: 'watch-s3',
    name: 'T-Watch S3 / S3 Plus',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.watchS3',
    firmwares: [],
  },
  {
    id: 't-deck',
    name: 'T-Deck / T-Deck Plus',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.deck',
    firmwares: [],
  },
  {
    id: 'vibe-dev',
    name: 'T-Vibe-Dev',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.vibeDev',
    firmwares: [],
  },
  {
    id: 'watch-2020',
    name: 'T-Watch 2020 V3',
    meta: ['ESP32', '16 MB'],
    note: 'note.legacy',
    firmwares: [],
  },
  {
    id: 'watch-2019',
    name: 'T-Watch 2019',
    meta: ['ESP32', '16 MB'],
    note: 'note.legacy',
    firmwares: [],
  },
  {
    id: 't-embed-plus',
    name: 'T-Embed-Plus',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.embedV1',
    firmwares: [],
  },
  {
    id: 't-embed-plus-v2',
    name: 'T-Embed-Plus V2',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.embedV2',
    firmwares: [],
  },
  {
    id: 't-embed',
    name: 'T-Embed',
    meta: ['ESP32-S3', '16 MB'],
    note: 'note.embed',
    firmwares: [],
  },
];

const guideRoot = 'https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/';
const deviceGuides = {
  'lora-pager': 'lilygo-t-lora-pager.md#entering-download-mode-on-the-t-lora-pager',
  'vibe-pager': 'lilygo-t-vibe-pager.md',
  'watch-ultra': 'lilygo-t-watch-ultra.md#entering-download-mode-on-the-t-watch-ultra',
  'watch-s3': 'lilygo-t-watch-s3.md#entering-download-mode-on-the-t-watch-s3',
  't-deck': 'lilygo-t-deck.md#entering-download-mode-on-the-t-deck',
  'vibe-dev': 'lilygo-t-vibe-dev.md',
  'watch-2020': 'lilygo-t-watch-2020-v3.md',
  'watch-2019': 'lilygo-t-watch.md',
  't-embed-plus': 'lilygo-t-embed-plus.md#3-entering-download-mode-on-the-t-embed-plus',
  't-embed-plus-v2': 'lilygo-t-embed-plus-v2.md#3-entering-download-mode-on-the-t-embed-plus-v2',
  't-embed': 'lilygo-t-embed.md#3-entering-download-mode-on-the-t-embed',
};

const initialDevice = devices.find((device) => device.id === 't-embed-plus');
const state = {
  device: initialDevice,
  firmware: null,
  catalogLoading: true,
  catalogError: false,
  customFile: null,
  port: null,
  transport: null,
  loader: null,
  chip: null,
  connected: false,
  busy: false,
  logs: [],
  progress: { percent: 0, title: 'progress.ready', detail: 'progress.chooseDevice', params: {}, size: '—' },
  toast: null,
  toastTimer: null,
};

const elements = Object.fromEntries([
  'browser-pill', 'browser-status', 'device-grid', 'selected-device-summary',
  'firmware-select', 'firmware-meta', 'custom-firmware', 'custom-file',
  'connect-button', 'connect-label', 'flash-button', 'connection-state',
  'progress-title', 'progress-percent', 'progress-bar', 'progress-detail',
  'progress-size', 'log-output', 'clear-log', 'toast', 'environment-note',
  'device-guide', 'transfer-progress', 'clear-custom', 'language-select',
].map((id) => [id, document.getElementById(id)]));

const hasWebSerial = window.isSecureContext && typeof navigator.serial?.requestPort === 'function';
const expectedChip = () => state.device.meta[0];

class FlashError extends Error {
  constructor(key, params = {}) {
    super(key);
    this.translation = { key, params };
  }
}

function errorMessage(error) {
  return error.translation || error.message || String(error);
}

function deviceIcon(device) {
  let shape;
  if (device.id.startsWith('watch')) {
    shape = '<path d="M9 2h6l1 5M9 22h6l1-5"/><rect x="6" y="7" width="12" height="10" rx="3"/><path d="M18 11h2M10 10v4h4"/>';
  } else if (device.id.includes('embed')) {
    shape = '<rect x="6" y="2" width="12" height="20" rx="3"/><rect x="8" y="5" width="8" height="9" rx="1"/><circle cx="12" cy="18" r="2"/>';
  } else {
    shape = '<rect x="3" y="6" width="18" height="14" rx="3"/><path d="M7 6V2M7 14h5M7 17h1m3 0h1m3 0h1"/><rect x="6" y="9" width="11" height="3" rx="1"/>';
  }
  return `<svg viewBox="0 0 24 24" aria-hidden="true">${shape}</svg>`;
}

function formatBytes(bytes) {
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function renderLogEntry(entry) {
  const line = document.createElement('div');
  line.className = `log-line${entry.type ? ` is-${entry.type}` : ''}`;
  const time = document.createElement('span');
  time.className = 'log-time';
  time.textContent = new Date(entry.timestamp).toLocaleTimeString(getLanguage(), { hour12: false });
  const text = document.createElement('span');
  text.textContent = entry.raw ?? t(entry.key, entry.params);
  line.append(time, text);
  return line;
}

function appendLog(entry) {
  entry.timestamp = Date.now();
  state.logs.push(entry);
  elements['log-output'].append(renderLogEntry(entry));
  if (state.logs.length > 200) {
    state.logs.shift();
    elements['log-output'].firstChild.remove();
  }
  elements['log-output'].scrollTop = elements['log-output'].scrollHeight;
}

function addLog(key, params = {}, type = '') { appendLog({ key, params, type }); }

function addRawLog(data) {
  const raw = String(data).trim();
  if (raw) appendLog({ raw });
}

function showToast(key, params = {}) {
  state.toast = { key, params };
  elements.toast.textContent = t(key, params);
  elements.toast.classList.add('is-visible');
  window.clearTimeout(state.toastTimer);
  state.toastTimer = window.setTimeout(() => elements.toast.classList.remove('is-visible'), 4500);
}

function renderProgress() {
  const { percent, title, detail, params, size } = state.progress;
  elements['progress-bar'].style.width = `${percent}%`;
  elements['progress-percent'].textContent = `${percent}%`;
  elements['transfer-progress'].setAttribute('aria-valuenow', percent);
  elements['progress-title'].textContent = t(title, params);
  elements['progress-detail'].textContent = t(detail, params);
  elements['progress-size'].textContent = size;
}

function setProgress(percent, title, detail, params = {}, size = '—') {
  state.progress = { percent: Math.max(0, Math.min(100, Math.round(percent))), title, detail, params, size };
  renderProgress();
}

function setStep(current) {
  document.querySelectorAll('.step').forEach((step) => {
    const number = Number(step.dataset.step);
    step.classList.toggle('is-active', number === current);
    step.classList.toggle('is-done', number < current);
    if (number === current) step.setAttribute('aria-current', 'step');
    else step.removeAttribute('aria-current');
  });
}

function updateControls() {
  const busy = state.busy;
  elements['connect-button'].disabled = busy || !hasWebSerial;
  elements['connect-label'].textContent = t(state.connected ? 'action.disconnect' : 'action.connect');
  elements['connect-button'].classList.toggle('button-disconnect', state.connected);
  elements['flash-button'].disabled = busy || !state.connected || (!state.firmware && !state.customFile);
  elements['firmware-select'].disabled = busy || !state.device.firmwares.length;
  elements['custom-firmware'].disabled = busy;
  elements['clear-custom'].disabled = busy;
  document.querySelector('.flash-panel').classList.toggle('is-busy', busy);
  document.querySelectorAll('[data-device-id]').forEach((card) => { card.disabled = busy; });
  elements['connection-state'].classList.toggle('is-offline', !state.connected);
  elements['connection-state'].innerHTML = `<span class="status-dot"></span>${t(state.connected ? 'connection.connected' : 'connection.disconnected')}`;
}

function setBusy(busy) {
  state.busy = busy;
  updateControls();
}

function renderDeviceCards() {
  elements['device-grid'].innerHTML = devices.map((device) => `
    <button class="device-card${device.id === state.device.id ? ' is-selected' : ''}${!state.catalogLoading && !device.firmwares.length ? ' is-pending' : ''}" type="button" data-device-id="${device.id}" aria-pressed="${device.id === state.device.id}">
      <span class="device-top"><span class="device-icon">${deviceIcon(device)}</span><span class="device-check" aria-hidden="true">✓</span></span>
      <span class="device-name">${device.name}</span>
      <span class="device-meta">${device.meta.map((value) => `<span>${value}</span>`).join('')}${!state.catalogLoading && !device.firmwares.length ? `<span>${t(firmwareStatusKey())}</span>` : ''}</span>
    </button>
  `).join('');
  elements['device-grid'].querySelectorAll('[data-device-id]').forEach((card) => {
    card.addEventListener('click', () => selectDevice(card.dataset.deviceId));
  });
}

function firmwareStatusKey() {
  if (state.catalogLoading) return 'firmware.loading';
  return state.catalogError ? 'firmware.loadFailed' : 'firmware.pending';
}

function parseFirmwareManifest(manifest) {
  if (manifest?.schemaVersion !== 1 || !manifest.devices || typeof manifest.devices !== 'object' || Array.isArray(manifest.devices)) {
    throw new FlashError('error.manifestInvalid');
  }
  const catalog = {};
  for (const device of devices) {
    const entries = manifest.devices[device.id] ?? [];
    if (!Array.isArray(entries)) throw new FlashError('error.manifestInvalid');
    const latest = new Map();
    for (const entry of entries) {
      if (!entry || typeof entry !== 'object'
          || typeof entry.file !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._-]*-0x0\.bin$/i.test(entry.file)
          || entry.chip !== device.meta[0] || entry.offset !== 0
          || !Number.isInteger(entry.size) || entry.size < 0x8002 || entry.size > 16 * 1024 * 1024
          || typeof entry.date !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(entry.date)
          || !Number.isFinite(Date.parse(entry.date)) || new Date(entry.date).toISOString().slice(0, 10) !== entry.date
          || (entry.variant !== undefined && (typeof entry.variant !== 'string' || !/^[a-zA-Z0-9-]+$/.test(entry.variant)))
          || (entry.factory !== undefined && typeof entry.factory !== 'boolean')) {
        throw new FlashError('error.manifestInvalid');
      }
      const previous = latest.get(entry.variant || '');
      if (!previous || entry.date > previous.date || (entry.date === previous.date && entry.file > previous.file)) {
        latest.set(entry.variant || '', entry);
      }
    }
    catalog[device.id] = [...latest.values()];
  }
  return catalog;
}

async function loadFirmwareManifest() {
  try {
    const response = await fetch(`${firmwareRoot}manifest.json`, { cache: 'no-store' });
    if (!response.ok) throw new FlashError('error.manifestHttp', { status: response.status });
    let manifest;
    try { manifest = await response.json(); }
    catch { throw new FlashError('error.manifestInvalid'); }
    const catalog = parseFirmwareManifest(manifest);
    const selectedFile = state.firmware?.file;
    for (const device of devices) device.firmwares = catalog[device.id];
    state.firmware = state.device.firmwares.find((firmware) => firmware.file === selectedFile) || state.device.firmwares[0] || null;
    state.catalogLoading = false;
    addLog('log.manifestLoaded', { count: Object.values(catalog).reduce((sum, entries) => sum + entries.length, 0) });
  } catch (error) {
    state.catalogLoading = false;
    state.catalogError = true;
    addLog('log.manifestFailed', { error: errorMessage(error) }, 'error');
  }
  renderDeviceCards();
  renderSelectedDevice();
  updateControls();
}

function renderFirmwareMeta() {
  const name = document.createElement('span');
  name.textContent = state.customFile?.name || state.firmware?.file || t(state.catalogLoading || state.catalogError ? firmwareStatusKey() : 'firmware.none');
  name.title = name.textContent;
  const address = document.createElement('span');
  address.textContent = t('firmware.address');
  elements['firmware-meta'].replaceChildren(name, address);
  elements['custom-file'].hidden = !state.customFile;
  elements['clear-custom'].hidden = !state.customFile;
  elements['custom-file'].textContent = state.customFile ? t('firmware.custom', { size: formatBytes(state.customFile.size) }) : '';
}

function renderSelectedDevice() {
  elements['selected-device-summary'].innerHTML = `
    <span class="device-icon">${deviceIcon(state.device)}</span>
    <span><strong>${state.device.name}</strong><small>${t(state.device.note)}</small></span>
  `;
  const select = elements['firmware-select'];
  select.replaceChildren();
  if (state.device.firmwares.length) {
    for (const [index, firmware] of state.device.firmwares.entries()) {
      const key = firmware.variant ? (firmware.factory ? 'firmware.variantFactory' : 'firmware.variant') : 'firmware.factory';
      select.add(new Option(`${t(key, { variant: firmware.variant, device: state.device.name })} · ${firmware.date}`, String(index)));
    }
    select.value = String(state.device.firmwares.indexOf(state.firmware));
  } else {
    select.add(new Option(t(firmwareStatusKey()), ''));
  }
  elements['device-guide'].href = guideRoot + deviceGuides[state.device.id];
  document.getElementById('s3-plus-guide').hidden = state.device.id !== 'watch-s3';
  renderFirmwareMeta();
}

function clearCustomFile() {
  state.customFile = null;
  elements['custom-firmware'].value = '';
  renderFirmwareMeta();
  updateControls();
}

function selectDevice(id) {
  if (state.busy) return;
  const selected = devices.find((device) => device.id === id);
  if (!selected) return;
  state.device = selected;
  state.firmware = selected.firmwares[0] || null;
  clearCustomFile();
  document.querySelectorAll('[data-device-id]').forEach((card) => {
    const active = card.dataset.deviceId === selected.id;
    card.classList.toggle('is-selected', active);
    card.setAttribute('aria-pressed', active);
  });
  renderSelectedDevice();
  updateControls();
  setStep(state.connected ? 2 : 1);
  setProgress(0, 'progress.ready', state.connected ? 'progress.confirmVersion' : 'progress.chooseDevice');
  addLog('log.selected', { device: selected.name });
}

function applyLanguage() {
  document.documentElement.lang = getLanguage();
  elements['language-select'].value = getLanguage();
  document.querySelectorAll('[data-i18n]').forEach((element) => {
    element.textContent = t(element.dataset.i18n);
  });
  for (const attribute of ['aria-label', 'content']) {
    document.querySelectorAll(`[data-i18n-${attribute}]`).forEach((element) => {
      element.setAttribute(attribute, t(element.getAttribute(`data-i18n-${attribute}`)));
    });
  }
  renderDeviceCards();
  renderSelectedDevice();
  renderProgress();
  updateControls();
  elements['log-output'].replaceChildren(...state.logs.map(renderLogEntry));
  elements['log-output'].scrollTop = elements['log-output'].scrollHeight;
  if (state.toast) elements.toast.textContent = t(state.toast.key, state.toast.params);
  const environment = hasWebSerial ? 'ready' : (window.isSecureContext ? 'unsupported' : 'https');
  elements['browser-pill'].classList.toggle('is-offline', !hasWebSerial);
  elements['browser-status'].textContent = t(`browser.${environment}`);
  elements['environment-note'].textContent = t(`environment.${environment}`);
  document.getElementById('full-guide').href = `https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/firmware/${getLanguage() === 'zh-CN' ? 'README_CN.MD' : 'README.md'}`;
}

async function closeConnection() {
  const transport = state.transport;
  state.transport = null;
  state.loader = null;
  state.port = null;
  state.chip = null;
  state.connected = false;
  try { if (transport) await transport.disconnect(); }
  catch { addLog('log.portCloseWarning', {}, 'warning'); }
  updateControls();
}

async function connectDevice() {
  if (state.busy || !hasWebSerial) return;
  if (state.connected) {
    setBusy(true);
    await closeConnection();
    setBusy(false);
    setStep(1);
    setProgress(0, 'connection.disconnected', 'progress.reconnect');
    addLog('log.disconnected');
    return;
  }
  setBusy(true);
  setStep(2);
  setProgress(0, 'progress.connecting', 'progress.choosePort');
  try {
    // Keep the port request inside the click gesture before loading the module.
    state.port = await navigator.serial.requestPort();
    addLog('log.loading');
    const { ESPLoader, Transport } = await import(ESPTOOL_URL);
    state.transport = new Transport(state.port, true);
    state.loader = new ESPLoader({
      transport: state.transport,
      baudrate: 115200,
      terminal: { clean: () => undefined, writeLine: addRawLog, write: addRawLog },
      debugLogging: false,
    });
    setProgress(0, 'progress.detecting', 'progress.downloadMode');
    await state.loader.main();
    state.chip = state.loader.chip.CHIP_NAME;
    state.connected = true;
    setProgress(0, 'progress.connected', 'progress.confirmConnected', { chip: state.chip });
    addLog('log.detected', { chip: state.chip }, 'success');
    showToast('toast.connected');
  } catch (error) {
    await closeConnection();
    setStep(1);
    if (error.name === 'NotFoundError') {
      setProgress(0, 'progress.waiting', 'progress.cancelled');
      addLog('log.cancelled');
    } else {
      setProgress(0, 'progress.connectFailed', 'progress.checkConnection');
      addLog('log.connectFailed', { error: errorMessage(error) }, 'error');
      showToast('toast.connectFailed');
    }
  } finally { setBusy(false); }
}

async function readFirmware() {
  if (state.customFile) return new Uint8Array(await state.customFile.arrayBuffer());
  if (!state.firmware) throw new FlashError('error.noFirmware');
  const response = await fetch(`${firmwareRoot}${state.firmware.file}`);
  if (!response.ok) throw new FlashError('error.http', { status: response.status });
  const data = new Uint8Array(await response.arrayBuffer());
  if (data.byteLength !== state.firmware.size) throw new FlashError('error.firmwareSize');
  return data;
}

function validateFirmware(data) {
  if (!data.length || data.length > 16 * 1024 * 1024) throw new FlashError('error.size');
  const chip = expectedChip();
  if (state.chip !== chip) throw new FlashError('error.chip', { actual: state.chip, expected: chip });
  const bootAddress = chip === 'ESP32' ? 0x1000 : 0;
  if (data[bootAddress] !== 0xe9 || data[0x8000] !== 0xaa || data[0x8001] !== 0x50) throw new FlashError('error.merged');
  const imageChip = data[bootAddress + 12] | (data[bootAddress + 13] << 8);
  if (imageChip !== (chip === 'ESP32' ? 0 : 9)) throw new FlashError('error.imageChip');
}

async function flashFirmware() {
  if (state.busy || !state.connected || (!state.firmware && !state.customFile)) return;
  setBusy(true);
  setStep(3);
  const filename = state.customFile?.name || state.firmware.file;
  setProgress(0, 'progress.preparing', 'progress.reading');
  addLog('log.preparing', { device: state.device.name, filename });
  try {
    const data = await readFirmware();
    validateFirmware(data);
    const displaySize = formatBytes(data.byteLength);
    setProgress(0, 'progress.writing', 'progress.stayConnected', {}, displaySize);
    await state.loader.writeFlash({
      fileArray: [{ data, address: 0x0 }],
      flashMode: 'keep',
      flashFreq: 'keep',
      flashSize: 'keep',
      eraseAll: false,
      compress: true,
      reportProgress: (_index, written, total) => {
        setProgress(total ? (written / total) * 99 : 0, 'progress.writing', 'progress.transfer', {
          written: formatBytes(written), total: formatBytes(total),
        }, displaySize);
      },
    });
    setProgress(100, 'progress.complete', 'progress.restart', {}, displaySize);
    addLog('log.complete', { size: displaySize }, 'success');
    try { await state.loader.after('hard_reset'); }
    catch { addLog('log.resetWarning', {}, 'warning'); }
    showToast('toast.complete');
  } catch (error) {
    setProgress(0, 'progress.failed', 'progress.retry');
    addLog('log.flashFailed', { error: errorMessage(error) }, 'error');
    showToast('toast.flashFailed');
  } finally {
    await closeConnection();
    setBusy(false);
  }
}

function chooseCustomFile(file) {
  if (state.busy || !file) return;
  if (!/\.bin$/i.test(file.name) || file.size === 0 || file.size > 16 * 1024 * 1024) {
    showToast('toast.invalidFile');
    elements['custom-firmware'].value = '';
    return;
  }
  state.customFile = file;
  renderFirmwareMeta();
  updateControls();
  addLog('log.custom', { filename: file.name });
}

elements['language-select'].addEventListener('change', (event) => {
  setLanguage(event.target.value);
  applyLanguage();
});
elements['firmware-select'].addEventListener('change', (event) => {
  state.firmware = state.device.firmwares[Number(event.target.value)];
  clearCustomFile();
});
elements['clear-custom'].addEventListener('click', clearCustomFile);
elements['custom-firmware'].addEventListener('change', (event) => chooseCustomFile(event.target.files[0]));
const dropzone = document.querySelector('.dropzone');
for (const eventName of ['dragover', 'dragleave', 'drop']) {
  dropzone.addEventListener(eventName, (event) => {
    event.preventDefault();
    dropzone.classList.toggle('is-dragover', eventName === 'dragover' && !state.busy);
    if (eventName === 'drop') chooseCustomFile(event.dataTransfer.files[0]);
  });
}
elements['connect-button'].addEventListener('click', connectDevice);
elements['flash-button'].addEventListener('click', flashFirmware);
elements['clear-log'].addEventListener('click', () => {
  state.logs.length = 0;
  elements['log-output'].replaceChildren();
});
if (navigator.serial) navigator.serial.addEventListener('disconnect', (event) => {
  if (event.target === state.port || event.port === state.port) {
    void closeConnection();
    addLog('log.unplugged', {}, 'warning');
  }
});
window.addEventListener('beforeunload', (event) => {
  if (state.busy && state.transport) { event.preventDefault(); event.returnValue = ''; }
});

applyLanguage();
setStep(1);
addLog('log.ready');
void loadFirmwareManifest();
