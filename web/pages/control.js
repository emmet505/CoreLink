/* ============================================================
   pages/control.js  —  Device Control page
   4 Relay cards · Telemetry · Emergency Stop
   ============================================================ */

'use strict';

// ── DOM refs ──────────────────────────────────────────────────
const Control = {
  // Relays (arrays indexed 0–3, mapped to relay 1–4)
  relays: [],

  // Telemetry
  telDot:         null,
  telLastUpdated: null,
  telHeapFree:    null,
  telHeapMinFree: null,
  telHeapUsage:   null,
  telHeapBar:     null,
  telIramFree:    null,
  telIramUsage:   null,
  telIramBar:     null,
  telPsramFree:   null,
  telPsramUsage:  null,
  telPsramBar:    null,
  telCpuCores:    null,
  telFlashSize:   null,
  telUptime:      null,

  // Emergency stop
  btnEstop: null,

  // Sidebar / topbar
  sidebarDot:   null,
  sidebarLabel: null,
  topbarDot:    null,
};

// ── Telemetry ──────────────────────────────────────────────────
const TEL_POLL_MS = 1000;
let telPollTimer  = null;

function bytesToKB(b) { return (b / 1024).toFixed(0) + ' KB'; }

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

function setText(el, val) { if (el) el.textContent = val; }

function applyBar(barEl, pct) {
  if (!barEl) return;
  barEl.style.width = Math.min(pct, 100) + '%';
  barEl.classList.remove('warn', 'crit');
  if      (pct >= 85) barEl.classList.add('crit');
  else if (pct >= 60) barEl.classList.add('warn');
}

function setOnline(online) {
  const cls = 'status-dot ' + (online ? 'online' : 'offline');
  if (Control.telDot)     Control.telDot.className     = cls;
  if (Control.sidebarDot) Control.sidebarDot.className = cls;
  if (Control.topbarDot)  Control.topbarDot.className  = cls;
  if (Control.sidebarLabel)
    Control.sidebarLabel.textContent = online ? 'Online' : 'Offline';
}

function applyTelemetry(d) {
  setText(Control.telHeapFree,    bytesToKB(d.heap_free));
  setText(Control.telHeapMinFree, bytesToKB(d.heap_min_free));
  setText(Control.telHeapUsage,   d.heap_usage.toFixed(1) + '%');
  applyBar(Control.telHeapBar, d.heap_usage);

  setText(Control.telIramFree,   bytesToKB(d.internal_heap_free));
  setText(Control.telIramUsage,  d.internal_heap_usage.toFixed(1) + '%');
  applyBar(Control.telIramBar, d.internal_heap_usage);

  setText(Control.telPsramFree,  bytesToKB(d.psram_free));
  setText(Control.telPsramUsage, d.psram_usage.toFixed(1) + '%');
  applyBar(Control.telPsramBar, d.psram_usage);

  setText(Control.telCpuCores,  d.cpu_cores);
  setText(Control.telFlashSize, bytesToKB(d.flash_size));
  setText(Control.telUptime,    fmtUptime(d.uptime_seconds));
  setText(Control.telLastUpdated, new Date().toLocaleTimeString());

  // sync LED toggle in settings if available
  const togLed = document.getElementById('tog-led');
  if (togLed && d.led_enabled !== undefined) togLed.checked = d.led_enabled;

  setOnline(true);
}

async function fetchStatus() {
  try {
    const data = await API.getStatus();
    applyTelemetry(data);
  } catch (err) {
    console.warn('[telemetry] poll failed:', err.message);
    setOnline(false);
    setText(Control.telLastUpdated, 'error — retrying…');
  }
}

function startTelemetryPolling() {
  fetchStatus();
  telPollTimer = setInterval(fetchStatus, TEL_POLL_MS);
}

function stopTelemetryPolling() {
  clearInterval(telPollTimer);
  telPollTimer = null;
}

// ── Relay helpers ──────────────────────────────────────────────
function setRelayUI(n, on) {
  const r = Control.relays[n];
  if (!r) return;
  r.badge.textContent = on ? 'ON' : 'OFF';
  r.badge.classList.toggle('on', on);
  r.indicator.classList.toggle('on', on);
}

function setScheduleBadge(n, enabled) {
  const r = Control.relays[n];
  if (!r) return;
  r.scheduleBadge.textContent = enabled ? 'ON' : 'OFF';
  r.scheduleBadge.classList.toggle('on', enabled);
}

function applyRelayState(state) {
  const index = state.relay - 1;
  const r = Control.relays[index];
  if (!r) return;

  setRelayUI(index, state.is_on);

  const schedule = state.schedule;
  r.togSchedule.checked = schedule.enabled;
  r.inpStart.value = `${String(schedule.start.hour).padStart(2, '0')}:${String(schedule.start.minute).padStart(2, '0')}`;
  r.inpStop.value = `${String(schedule.stop.hour).padStart(2, '0')}:${String(schedule.stop.minute).padStart(2, '0')}`;
  setScheduleBadge(index, schedule.enabled);
}

async function fetchRelayStates() {
  try {
    const states = await API.getRelayStates();
    states.forEach(applyRelayState);
  } catch (err) {
    console.warn('[relays] state fetch failed:', err.message);
  }
}

// ── Init one relay card ────────────────────────────────────────
function initRelayCard(n) {
  const i = n - 1; // array index

  const r = {
    badge:         document.getElementById(`relay-badge-${n}`),
    indicator:     document.getElementById(`relay-indicator-${n}`),
    btnOn:         document.getElementById(`btn-relay-${n}-on`),
    btnOff:        document.getElementById(`btn-relay-${n}-off`),
    scheduleBadge: document.getElementById(`schedule-badge-${n}`),
    inpStart:      document.getElementById(`inp-schedule-${n}-start`),
    inpStop:       document.getElementById(`inp-schedule-${n}-stop`),
    togSchedule:   document.getElementById(`tog-schedule-${n}`),
    btnApply:      document.getElementById(`btn-save-schedule-${n}`),
  };

  Control.relays[i] = r;

  r.btnOn.addEventListener('click', async () => {
    try {
      await API.setRelay(n, true);
      setRelayUI(i, true);
      showToast(`Relay ${n} ON`, 'success');
    } catch (err) {
      showToast(`Relay ${n} failed: ` + err.message, 'error');
    }
  });

  r.btnOff.addEventListener('click', async () => {
    try {
      await API.setRelay(n, false);
      setRelayUI(i, false);
      showToast(`Relay ${n} OFF`);
    } catch (err) {
      showToast(`Relay ${n} failed: ` + err.message, 'error');
    }
  });

  r.togSchedule.addEventListener('change', () => {
    setScheduleBadge(i, r.togSchedule.checked);
  });

  r.btnApply.addEventListener('click', async () => {
    const enabled = r.togSchedule.checked;
    const start   = r.inpStart.value;
    const stop    = r.inpStop.value;

    if (enabled && (!start || !stop)) {
      showToast('Set start and stop times first', 'warn');
      return;
    }
    if (!enabled) {
      try {
        r.btnApply.disabled = true;
        await API.setSchedule({ relay: n, enabled: false });
        setRelayUI(i, false);
        setScheduleBadge(i, false);
        showToast(`Relay ${n} schedule off`, 'success');
      } catch (err) {
        showToast('Schedule failed: ' + err.message, 'error');
      } finally {
        r.btnApply.disabled = false;
      }
      return;
    }
    const [startHour, startMinute] = start.split(':').map(Number);
    const [stopHour,  stopMinute]  = stop.split(':').map(Number);
    try {
      r.btnApply.disabled = true;
      await API.setSchedule({
        relay:   n,
        enabled,
        start:   { hour: startHour, minute: startMinute },
        stop:    { hour: stopHour,  minute: stopMinute  },
      });
      setScheduleBadge(i, enabled);
      showToast(
        enabled ? `Relay ${n} schedule: ${start} → ${stop}` : `Relay ${n} schedule off`,
        'success'
      );
    } catch (err) {
      showToast('Schedule failed: ' + err.message, 'error');
    } finally {
      r.btnApply.disabled = false;
    }
  });
}

// ── Emergency Stop ─────────────────────────────────────────────
function initEmergencyStop() {
  Control.btnEstop.addEventListener('click', async () => {
    try {
      await API.emergencyStop();
      Control.relays.forEach((_, i) => setRelayUI(i, false));
      showToast('⬛ Emergency stop — all relays OFF', 'error', 5000);
    } catch (err) {
      showToast('E-stop failed: ' + err.message, 'error');
    }
  });
}

// ── Init ───────────────────────────────────────────────────────
function initControl() {
  // Telemetry
  Control.telDot         = document.getElementById('telemetry-status-dot');
  Control.telLastUpdated = document.getElementById('tel-last-updated');
  Control.telHeapFree    = document.getElementById('tel-heap-free');
  Control.telHeapMinFree = document.getElementById('tel-heap-min-free');
  Control.telHeapUsage   = document.getElementById('tel-heap-usage');
  Control.telHeapBar     = document.getElementById('tel-heap-bar');
  Control.telIramFree    = document.getElementById('tel-iram-free');
  Control.telIramUsage   = document.getElementById('tel-iram-usage');
  Control.telIramBar     = document.getElementById('tel-iram-bar');
  Control.telPsramFree   = document.getElementById('tel-psram-free');
  Control.telPsramUsage  = document.getElementById('tel-psram-usage');
  Control.telPsramBar    = document.getElementById('tel-psram-bar');
  Control.telCpuCores    = document.getElementById('tel-cpu-cores');
  Control.telFlashSize   = document.getElementById('tel-flash-size');
  Control.telUptime      = document.getElementById('tel-uptime');

  // Emergency stop
  Control.btnEstop = document.getElementById('btn-estop');

  // Sidebar / topbar
  Control.sidebarDot   = document.getElementById('sidebar-status-dot');
  Control.sidebarLabel = document.getElementById('sidebar-status-label');
  Control.topbarDot    = document.getElementById('topbar-status-dot');

  // Init 4 relay cards
  [1, 2, 3, 4].forEach(initRelayCard);

  initEmergencyStop();
  fetchRelayStates();
  startTelemetryPolling();
}