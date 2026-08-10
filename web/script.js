/* ============================================================
   ESP32-S3 Dashboard  —  script.js
   Architecture: API layer → UI controllers → init
   To connect real firmware: update API.BASE and API.request()
   ============================================================ */

'use strict';

// ── 1. API Layer ─────────────────────────────────────────────────────────────
//
//  All HTTP calls route through API.request().
//  Swap the BASE address or the fetch options here to match your firmware.
//  The rest of the application never touches fetch() directly.
//
const API = {
  BASE: 'http://192.168.4.1',

  /**
   * Core request method.
   * @param {string} path   - e.g. '/api/status'
   * @param {object} opts   - fetch options (method, body, etc.)
   * @returns {Promise<any>} parsed JSON
   */
  async request(path, opts = {}) {
    const defaults = {
      signal: AbortSignal.timeout(5000),
      headers: { 'Content-Type': 'application/json', ...(opts.headers || {}) },
    };
    const res = await fetch(API.BASE + path, { ...defaults, ...opts });
    if (!res.ok) throw new Error(`HTTP ${res.status} ${res.statusText}`);
    return res.json();
  },

  // ── Convenience methods ───────────────────────────────────────────────────
  getStatus()        { return API.request('/api/status'); },
  getSettings()      { return API.request('/api/settings'); },
  setRelay(state)    { return API.request('/api/relay',    { method: 'POST', body: JSON.stringify({ state }) }); },
  setPeripheral(k,v) { return API.request('/api/peripheral', { method: 'POST', body: JSON.stringify({ [k]: v }) }); },
  emergencyStop()    { return API.request('/api/estop',    { method: 'POST' }); },
  saveSettings(data) { return API.request('/api/settings', { method: 'POST', body: JSON.stringify(data) }); },
  reboot()           { return API.request('/api/reboot',   { method: 'POST' }); },
  factoryReset()     { return API.request('/api/factory',  { method: 'POST' }); },

  /**
   * Send relay schedule + current browser time so firmware can sync.
   * @param {{ enabled: bool, start: string, stop: string, device_time: string }} data
   */
  setSchedule(data)  { return API.request('/api/relay/schedule', { method: 'POST', body: JSON.stringify(data) }); },
};


// ── 2. DOM Cache ──────────────────────────────────────────────────────────────
//
//  Call cacheDom() once on DOMContentLoaded.
//  All other functions reference DOM via the `$` namespace — no repeated querySelector.
//
const $ = {};

function cacheDom() {
  // Navigation
  $.navItems        = document.querySelectorAll('.nav-item');
  $.pages           = document.querySelectorAll('.page');

  // Sidebar status
  $.sidebarDot      = document.getElementById('sidebar-status-dot');
  $.sidebarLabel    = document.getElementById('sidebar-status-label');
  $.topbarDot       = document.getElementById('topbar-status-dot');

  // Mobile menu
  $.menuBtn         = document.getElementById('menu-btn');
  $.sidebarEl       = document.getElementById('sidebar');
  $.sidebarOverlay  = document.getElementById('sidebar-overlay');

  // Relay
  $.btnRelayOn      = document.getElementById('btn-relay-on');
  $.btnRelayOff     = document.getElementById('btn-relay-off');
  $.relayBadge      = document.getElementById('relay-badge');
  $.relayIndicator  = document.getElementById('relay-indicator');

  // Peripheral toggles
  $.togLed          = document.getElementById('tog-led');
  $.togFan          = document.getElementById('tog-fan');
  $.togLight        = document.getElementById('tog-light');
  $.togAuto         = document.getElementById('tog-auto');

  // Emergency stop
  $.btnEstop        = document.getElementById('btn-estop');

  // Telemetry — status
  $.telDot           = document.getElementById('telemetry-status-dot');
  $.telLastUpdated   = document.getElementById('tel-last-updated');

  // Telemetry — heap (total)
  $.telHeapTotal     = document.getElementById('tel-heap-total');
  $.telHeapFree      = document.getElementById('tel-heap-free');
  $.telHeapUsed      = document.getElementById('tel-heap-used');
  $.telHeapMinFree   = document.getElementById('tel-heap-min-free');
  $.telHeapUsage     = document.getElementById('tel-heap-usage');
  $.telHeapBar       = document.getElementById('tel-heap-bar');

  // Telemetry — internal RAM
  $.telIramTotal     = document.getElementById('tel-iram-total');
  $.telIramFree      = document.getElementById('tel-iram-free');
  $.telIramUsed      = document.getElementById('tel-iram-used');
  $.telIramUsage     = document.getElementById('tel-iram-usage');
  $.telIramBar       = document.getElementById('tel-iram-bar');

  // Telemetry — PSRAM
  $.telPsramTotal    = document.getElementById('tel-psram-total');
  $.telPsramFree     = document.getElementById('tel-psram-free');
  $.telPsramUsed     = document.getElementById('tel-psram-used');
  $.telPsramUsage    = document.getElementById('tel-psram-usage');
  $.telPsramBar      = document.getElementById('tel-psram-bar');

  // Telemetry — device
  $.telCpuCores      = document.getElementById('tel-cpu-cores');
  $.telFlashSize     = document.getElementById('tel-flash-size');
  $.telUptime        = document.getElementById('tel-uptime');

  // Relay schedule
  $.scheduleCard      = document.getElementById('schedule-card');
  $.scheduleBadge     = document.getElementById('schedule-badge');
  $.inpScheduleStart  = document.getElementById('inp-schedule-start');
  $.inpScheduleStop   = document.getElementById('inp-schedule-stop');
  $.togSchedule       = document.getElementById('tog-schedule');
  $.btnSaveSchedule   = document.getElementById('btn-save-schedule');

  // Settings
  $.inpSsid         = document.getElementById('inp-ssid');
  $.inpPass         = document.getElementById('inp-pass');
  $.inpChannel      = document.getElementById('inp-channel');
  $.inpMaxConn      = document.getElementById('inp-max-conn');
  $.togDhcp         = document.getElementById('tog-dhcp');
  $.staticIpFields  = document.getElementById('static-ip-fields');
  $.inpIp           = document.getElementById('inp-ip');
  $.inpMask         = document.getElementById('inp-mask');
  $.inpGw           = document.getElementById('inp-gw');
  $.inpDns          = document.getElementById('inp-dns');
  $.settingsError   = document.getElementById('settings-error');
  $.btnSaveSettings = document.getElementById('btn-save-settings');
  $.btnReboot       = document.getElementById('btn-reboot');
  $.btnFactory      = document.getElementById('btn-factory');

  // Modal
  $.modalBackdrop   = document.getElementById('modal-backdrop');
  $.modalTitle      = document.getElementById('modal-title');
  $.modalBody       = document.getElementById('modal-body');
  $.modalConfirm    = document.getElementById('modal-confirm');
  $.modalCancel     = document.getElementById('modal-cancel');

  // Toast
  $.toast           = document.getElementById('toast');
}


// ── 3. Toast ──────────────────────────────────────────────────────────────────
let toastTimer = null;

/**
 * @param {string} msg
 * @param {'success'|'error'|'warn'|''} type
 * @param {number} duration  ms
 */
function showToast(msg, type = '', duration = 3000) {
  clearTimeout(toastTimer);
  $.toast.textContent = msg;
  $.toast.className   = `toast show ${type}`;
  toastTimer = setTimeout(() => {
    $.toast.classList.remove('show');
  }, duration);
}


// ── 4. Confirm Modal ──────────────────────────────────────────────────────────
let modalResolve = null;

function confirmAction(title, body, dangerLabel = 'Confirm') {
  return new Promise((resolve) => {
    modalResolve = resolve;
    $.modalTitle.textContent   = title;
    $.modalBody.textContent    = body;
    $.modalConfirm.textContent = dangerLabel;
    $.modalBackdrop.classList.add('open');
  });
}

function closeModal(result) {
  $.modalBackdrop.classList.remove('open');
  if (modalResolve) { modalResolve(result); modalResolve = null; }
}

function initModal() {
  $.modalConfirm.addEventListener('click', () => closeModal(true));
  $.modalCancel.addEventListener('click',  () => closeModal(false));
  $.modalBackdrop.addEventListener('click', (e) => {
    if (e.target === $.modalBackdrop) closeModal(false);
  });
}


// ── 5. Navigation ─────────────────────────────────────────────────────────────

function closeSidebar() {
  $.sidebarEl.classList.remove('open');
  $.sidebarOverlay.classList.remove('open');
  $.menuBtn.classList.remove('open');
}

function toggleSidebar() {
  const isOpen = $.sidebarEl.classList.contains('open');
  if (isOpen) {
    closeSidebar();
  } else {
    $.sidebarEl.classList.add('open');
    $.sidebarOverlay.classList.add('open');
    $.menuBtn.classList.add('open');
  }
}

function initNav() {
  // Mobile hamburger
  $.menuBtn.addEventListener('click', toggleSidebar);
  $.sidebarOverlay.addEventListener('click', closeSidebar);

  $.navItems.forEach((item) => {
    item.addEventListener('click', () => {
      const target = item.dataset.page;

      $.navItems.forEach(i => i.classList.remove('active'));
      $.pages.forEach(p => p.classList.remove('active'));

      item.classList.add('active');
      document.getElementById(`page-${target}`)?.classList.add('active');

      /* Load current settings from firmware when user opens the Settings page */
      if (target === 'settings') loadSettings();

      // Close drawer on mobile after nav
      closeSidebar();
    });
  });
}


// ── 6. Relay Control ──────────────────────────────────────────────────────────
function setRelayUI(on) {
  $.relayBadge.textContent = on ? 'ON' : 'OFF';
  $.relayBadge.classList.toggle('on', on);
  $.relayIndicator.classList.toggle('on', on);
}

function initRelay() {
  $.btnRelayOn.addEventListener('click', async () => {
    try {
      await API.setRelay(true);
      setRelayUI(true);
      showToast('Relay turned ON', 'success');
    } catch (err) {
      showToast('Failed to set relay: ' + err.message, 'error');
    }
  });

  $.btnRelayOff.addEventListener('click', async () => {
    try {
      await API.setRelay(false);
      setRelayUI(false);
      showToast('Relay turned OFF');
    } catch (err) {
      showToast('Failed to set relay: ' + err.message, 'error');
    }
  });
}


// ── 7. Relay Schedule ─────────────────────────────────────────────────────────
//
//  Sends start/stop times (HH:MM) plus current browser time to firmware.
//  Firmware uses device_time to sync its internal clock for comparison.
//
function setScheduleBadge(enabled) {
  $.scheduleBadge.textContent = enabled ? 'ON' : 'OFF';
  $.scheduleBadge.classList.toggle('on', enabled);
}

function initSchedule() {
  // Reflect toggle state in badge immediately
  $.togSchedule.addEventListener('change', () => {
    setScheduleBadge($.togSchedule.checked);
  });

  $.btnSaveSchedule.addEventListener('click', async () => {
    const enabled = $.togSchedule.checked;
    const start   = $.inpScheduleStart.value;  // "HH:MM" or ""
    const stop    = $.inpScheduleStop.value;

    if (enabled && (!start || !stop)) {
      showToast('Set both start and stop times first', 'warn');
      return;
    }

    // Send current browser time so firmware can sync
    const now = new Date();
    const device_time = String(now.getHours()).padStart(2, '0') + ':' +
                        String(now.getMinutes()).padStart(2, '0') + ':' +
                        String(now.getSeconds()).padStart(2, '0');

    const payload = { enabled, start, stop, device_time };

    try {
      $.btnSaveSchedule.disabled = true;
      await API.setSchedule(payload);
      setScheduleBadge(enabled);
      showToast(enabled ? `Schedule set: ${start} → ${stop}` : 'Schedule disabled', 'success');
    } catch (err) {
      showToast('Schedule save failed: ' + err.message, 'error');
    } finally {
      $.btnSaveSchedule.disabled = false;
    }
  });
}


// ── 8. Peripheral Toggles ─────────────────────────────────────────────────────
function initPeripherals() {
  const peripherals = [
    { el: $.togLed,   key: 'led'   },
    { el: $.togFan,   key: 'fan'   },
    { el: $.togLight, key: 'light' },
    { el: $.togAuto,  key: 'auto'  },
  ];

  peripherals.forEach(({ el, key }) => {
    el.addEventListener('change', async () => {
      const on = el.checked;
      try {
        await API.setPeripheral(key, on);
        showToast(`${key.charAt(0).toUpperCase() + key.slice(1)} ${on ? 'enabled' : 'disabled'}`, 'success');
      } catch (err) {
        // Revert on failure
        el.checked = !on;
        showToast(`Failed to toggle ${key}: ` + err.message, 'error');
      }
    });
  });
}


// ── 9. Emergency Stop ─────────────────────────────────────────────────────────
//
//  Deliberately bypasses the confirm modal — immediate safety action.
//
function initEmergencyStop() {
  $.btnEstop.addEventListener('click', async () => {
    try {
      await API.emergencyStop();
      setRelayUI(false);
      // Turn off all peripheral toggles
      [$.togLed, $.togFan, $.togLight, $.togAuto].forEach(t => { t.checked = false; });
      showToast('⬛ Emergency stop triggered — all outputs OFF', 'error', 5000);
    } catch (err) {
      showToast('E-stop failed: ' + err.message, 'error');
    }
  });
}


// ── 10. Telemetry Polling ─────────────────────────────────────────────────────
//
//  Polls GET /api/status every TEL_POLL_MS milliseconds.
//
//  Expected JSON shape (cJSON keys from firmware):
//  {
//    "heap_total":            <bytes>,
//    "heap_free":             <bytes>,
//    "heap_used":             <bytes>,
//    "heap_min_free":         <bytes>,
//    "heap_usage":            <percent float>,
//    "internal_heap_total":   <bytes>,
//    "internal_heap_free":    <bytes>,
//    "internal_heap_used":    <bytes>,
//    "internal_heap_usage":   <percent float>,
//    "psram_total":           <bytes>,
//    "psram_free":            <bytes>,
//    "psram_used":            <bytes>,
//    "psram_usage":           <percent float>,
//    "cpu_cores":             <int>,
//    "flash_size":            <bytes>,
//    "uptime_seconds":        <seconds int>   ← cJSON key from firmware
//  }
//
const TEL_POLL_MS = 1000;  // 1 second
let   telPollTimer = null;

/** bytes → human-readable KB string */
function bytesToKB(b) { return (b / 1024).toFixed(0) + ' KB'; }

/** seconds → "3d 02h 15m 04s" style string */
function fmtUptime(secs) {
  if (secs == null) return '—';
  const d = Math.floor(secs / 86400);
  const h = Math.floor((secs % 86400) / 3600);
  const m = Math.floor((secs % 3600) / 60);
  const s = secs % 60;
  if (d > 0) return `${d}d ${String(h).padStart(2,'0')}h ${String(m).padStart(2,'0')}m`;
  if (h > 0) return `${h}h ${String(m).padStart(2,'0')}m ${String(s).padStart(2,'0')}s`;
  if (m > 0) return `${m}m ${String(s).padStart(2,'0')}s`;
  return `${s}s`;
}

/** Set element textContent safely */
function setText(el, val) { if (el) el.textContent = val; }

/**
 * Apply a usage percentage to a bar element.
 * Colour thresholds: <60% teal · 60–84% amber · ≥85% red
 */
function applyBar(barEl, pct) {
  if (!barEl) return;
  barEl.style.width = Math.min(pct, 100) + '%';
  barEl.classList.remove('warn', 'crit');
  if      (pct >= 85) barEl.classList.add('crit');
  else if (pct >= 60) barEl.classList.add('warn');
}

function applyTelemetry(d) {
  // ── Heap (total / DRAM + PSRAM combined) ──────────────────────────────────
  setText($.telHeapTotal,   bytesToKB(d.heap_total));
  setText($.telHeapFree,    bytesToKB(d.heap_free));
  setText($.telHeapUsed,    bytesToKB(d.heap_used));
  setText($.telHeapMinFree, bytesToKB(d.heap_min_free));
  setText($.telHeapUsage,   d.heap_usage.toFixed(1) + ' %');
  applyBar($.telHeapBar, d.heap_usage);

  // ── Internal RAM ───────────────────────────────────────────────────────────
  setText($.telIramTotal,   bytesToKB(d.internal_heap_total));
  setText($.telIramFree,    bytesToKB(d.internal_heap_free));
  setText($.telIramUsed,    bytesToKB(d.internal_heap_used));
  setText($.telIramUsage,   d.internal_heap_usage.toFixed(1) + ' %');
  applyBar($.telIramBar, d.internal_heap_usage);

  // ── PSRAM ──────────────────────────────────────────────────────────────────
  setText($.telPsramTotal,  bytesToKB(d.psram_total));
  setText($.telPsramFree,   bytesToKB(d.psram_free));
  setText($.telPsramUsed,   bytesToKB(d.psram_used));
  setText($.telPsramUsage,  d.psram_usage.toFixed(1) + ' %');
  applyBar($.telPsramBar, d.psram_usage);

  // ── Device ─────────────────────────────────────────────────────────────────
  setText($.telCpuCores,  d.cpu_cores);
  setText($.telFlashSize, bytesToKB(d.flash_size));
  setText($.telUptime,    fmtUptime(d.uptime_seconds));

  setText($.telLastUpdated, new Date().toLocaleTimeString());
  setOnline(true);
}

function setOnline(online) {
  const cls = 'status-dot ' + (online ? 'online' : 'offline');
  if ($.telDot)     $.telDot.className     = cls;
  if ($.sidebarDot) $.sidebarDot.className = cls;
  if ($.topbarDot)  $.topbarDot.className  = cls;
  if ($.sidebarLabel) $.sidebarLabel.textContent = online ? 'Online' : 'Offline';
}

async function fetchStatus() {
  try {
    const data = await API.getStatus();
    applyTelemetry(data);
  } catch (err) {
    console.warn('[telemetry] poll failed:', err.message);
    setOnline(false);
    setText($.telLastUpdated, 'error — retrying…');
  }
}

function startTelemetryPolling() {
  fetchStatus();                                        // immediate on load
  telPollTimer = setInterval(fetchStatus, TEL_POLL_MS);
}


// ── 11. Settings ──────────────────────────────────────────────────────────────
const IP_RE = /^(\d{1,3}\.){3}\d{1,3}$/;

function validateIP(str) {
  if (!IP_RE.test(str)) return false;
  return str.split('.').every(n => +n <= 255);
}

function showSettingsError(msg) {
  $.settingsError.textContent = msg;
}

/**
 * Fetch current settings from firmware and pre-fill the form.
 * Called once when the user navigates to the Settings page.
 */
async function loadSettings() {
  try {
    const d = await API.getSettings();
    if (d.ssid)            $.inpSsid.value    = d.ssid;
    if (d.channel)         $.inpChannel.value  = d.channel;
    if (d.max_connections) $.inpMaxConn.value  = d.max_connections;
    if (d.ip)              $.inpIp.value       = d.ip;
    if (d.gateway)         $.inpGw.value       = d.gateway;
    if (d.netmask)         $.inpMask.value     = d.netmask;
    /* password is not returned by firmware — left blank intentionally */
  } catch (err) {
    console.warn('[settings] failed to load current config:', err.message);
  }
}

function initSettings() {
  // DHCP toggle — show/hide static IP fields
  $.togDhcp.addEventListener('change', () => {
    $.staticIpFields.classList.toggle('visible', !$.togDhcp.checked);
    if ($.togDhcp.checked) {
      [$.inpIp, $.inpMask, $.inpGw, $.inpDns].forEach(el => el.classList.remove('invalid'));
      showSettingsError('');
    }
  });

  // Save
  $.btnSaveSettings.addEventListener('click', async () => {
    showSettingsError('');
    [$.inpIp, $.inpMask, $.inpGw, $.inpDns, $.inpChannel, $.inpMaxConn]
      .forEach(el => el.classList.remove('invalid'));

    const dhcp    = $.togDhcp.checked;
    const channel = parseInt($.inpChannel.value, 10);
    const maxConn = parseInt($.inpMaxConn.value, 10);

    if (isNaN(channel) || channel < 1 || channel > 13) {
      showSettingsError('Channel must be between 1 and 13.');
      $.inpChannel.classList.add('invalid');
      return;
    }
    if (isNaN(maxConn) || maxConn < 1 || maxConn > 10) {
      showSettingsError('Max connections must be between 1 and 10.');
      $.inpMaxConn.classList.add('invalid');
      return;
    }

    const payload = {
      ssid:            $.inpSsid.value.trim(),
      password:        $.inpPass.value,
      channel,
      max_connections: maxConn,
      dhcp,
    };

    // Validate static IP fields when DHCP is off
    if (!dhcp) {
      const fields = [
        { el: $.inpIp,   key: 'ip',      label: 'Static IP'   },
        { el: $.inpMask, key: 'mask',    label: 'Subnet Mask' },
        { el: $.inpGw,   key: 'gateway', label: 'Gateway'     },
        { el: $.inpDns,  key: 'dns',     label: 'DNS Server'  },
      ];
      let valid = true;
      for (const f of fields) {
        const val = f.el.value.trim();
        if (!validateIP(val)) {
          f.el.classList.add('invalid');
          showSettingsError(`Invalid ${f.label} address.`);
          valid = false;
          break;
        }
        payload[f.key] = val;
      }
      if (!valid) return;
    }

    try {
      $.btnSaveSettings.disabled = true;
      await API.saveSettings(payload);
      showToast('Settings saved', 'success');
    } catch (err) {
      showToast('Save failed: ' + err.message, 'error');
    } finally {
      $.btnSaveSettings.disabled = false;
    }
  });

  // Reboot
  $.btnReboot.addEventListener('click', async () => {
    const ok = await confirmAction(
      'Restart Device',
      'The device will restart and apply saved settings. Connection will be lost briefly.',
      'Restart'
    );
    if (!ok) return;
    try {
      await API.reboot();
      showToast('Restarting…', 'warn', 5000);
      setOnline(false);
    } catch (err) {
      showToast('Reboot failed: ' + err.message, 'error');
    }
  });

  // Factory Reset
  $.btnFactory.addEventListener('click', async () => {
    const ok = await confirmAction(
      'Factory Reset',
      'This will erase ALL configuration and restart the device. This cannot be undone.',
      '⚠ Reset Everything'
    );
    if (!ok) return;
    try {
      await API.factoryReset();
      showToast('Factory reset triggered', 'error', 6000);
      setOnline(false);
    } catch (err) {
      showToast('Factory reset failed: ' + err.message, 'error');
    }
  });
}


// ── 12. Init ──────────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  cacheDom();
  initModal();
  initNav();
  initRelay();
  initSchedule();
  initPeripherals();
  initEmergencyStop();
  initSettings();
  startTelemetryPolling();   // begins polling 192.168.4.1/api/status immediately
});