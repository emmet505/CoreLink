/* ============================================================
   pages/update.js  —  Firmware Update (OTA) page
   Firmware upload · WebFS upload · Progress · Reboot
   ============================================================ */

'use strict';

const Update = {
  fileFirmware:     null,
  fileWebfs:        null,
  btnUpdate:        null,
  firmwareStatus:   null,
  webfsStatus:      null,
  firmwareProgress: null,
  webfsProgress:    null,
  firmwareBar:      null,
  webfsBar:         null,
};

// ── Helpers ───────────────────────────────────────────────────────────────────
function setUpdateStatus(el, barEl, msg, state) {
  // state: '' | 'uploading' | 'success' | 'error'
  if (el)  { el.textContent = msg; el.className = `ota-status ${state}`; }
  if (barEl) barEl.parentElement.classList.toggle('visible', state === 'uploading');
}

function setBarProgress(barEl, pct) {
  if (barEl) barEl.style.width = pct + '%';
}

// ── Upload one file ───────────────────────────────────────────────────────────
async function uploadFile(file, endpoint, statusEl, barEl) {
  setUpdateStatus(statusEl, barEl, 'Uploading…', 'uploading');
  setBarProgress(barEl, 0);

  try {
    await API.otaUpload(endpoint, file, (pct) => {
      setBarProgress(barEl, pct);
      statusEl.textContent = `Uploading… ${pct}%`;
    });
    setUpdateStatus(statusEl, barEl, '✓ Done', 'success');
    return true;
  } catch (err) {
    setUpdateStatus(statusEl, barEl, '✗ ' + err.message, 'error');
    return false;
  }
}

// ── Main upload handler ───────────────────────────────────────────────────────
async function handleUpdate() {
  const firmware = Update.fileFirmware.files[0] || null;
  const webfs    = Update.fileWebfs.files[0]    || null;

  if (!firmware && !webfs) {
    showToast('Select at least one file to upload', 'warn');
    return;
  }

  const ok = await confirmAction(
    'Start Firmware Update',
    'The device will be unavailable during upload and will reboot when done.',
    'Upload & Reboot'
  );
  if (!ok) return;

  Update.btnUpdate.disabled = true;
  stopTelemetryPolling();

  let allOk = true;

  if (firmware) {
    const ok = await uploadFile(
      firmware,
      '/api/ota/firmware',
      Update.firmwareStatus,
      Update.firmwareBar
    );
    if (!ok) allOk = false;
  }

  if (webfs && allOk) {
    const ok = await uploadFile(
      webfs,
      '/api/ota/webfs',
      Update.webfsStatus,
      Update.webfsBar
    );
    if (!ok) allOk = false;
  }

  Update.btnUpdate.disabled = false;

  if (allOk) {
    setOnline(false);
    showToast('Update complete — device is rebooting…', 'success', 8000);
    await showInfoModal(
      'Update Complete',
      'All files uploaded successfully.<br><br>' +
      'The device is rebooting. Wait a few seconds, then refresh this page.',
      'OK'
    );
    window.location.reload();
  } else {
    showToast('Update failed — check status above', 'error');
    startTelemetryPolling();
  }
}

// ── File input label helper ───────────────────────────────────────────────────
function bindFileLabel(inputEl, statusEl) {
  inputEl.addEventListener('change', () => {
    const file = inputEl.files[0];
    if (file) {
      const kb = (file.size / 1024).toFixed(0);
      setUpdateStatus(statusEl, null, `${file.name} (${kb} KB)`, '');
    } else {
      setUpdateStatus(statusEl, null, 'No file selected', '');
    }
  });
}

// ── Init ──────────────────────────────────────────────────────────────────────
function initUpdate() {
  Update.fileFirmware     = document.getElementById('file-firmware');
  Update.fileWebfs        = document.getElementById('file-webfs');
  Update.btnUpdate        = document.getElementById('btn-ota-update');
  Update.firmwareStatus   = document.getElementById('ota-firmware-status');
  Update.webfsStatus      = document.getElementById('ota-webfs-status');
  Update.firmwareBar      = document.getElementById('ota-firmware-bar');
  Update.webfsBar         = document.getElementById('ota-webfs-bar');

  bindFileLabel(Update.fileFirmware, Update.firmwareStatus);
  bindFileLabel(Update.fileWebfs,    Update.webfsStatus);

  Update.btnUpdate.addEventListener('click', handleUpdate);
}