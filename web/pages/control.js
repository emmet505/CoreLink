/* ============================================================
   pages/control.js  —  Device Control page
   Relay · Schedule · Peripherals · Telemetry · Emergency Stop
   ============================================================ */

'use strict';

// ── DOM refs ──────────────────────────────────────────────────────────────────
const Control = {
  // Relay
  btnRelayOn:      null,
  btnRelayOff:     null,
  relayBadge:      null,
  relayIndicator:  null,

  // Schedule
  scheduleBadge:    null,
  inpScheduleStart: null,
  inpScheduleStop:  null,
  togSchedule:      null,
  btnSaveSchedule:  null,

  // Peripherals
  togLed:   null,
  togFan:   null,
  togLight: null,
  togAuto:  null,

  // Emergency stop
  btnEstop: null,

  // Telemetry
  telDot:         null,
  telLastUpdated: null,
  telHeapTotal:   null,
  telHeapFree:    null,
  telHeapUsed:    null,
  telHeapMinFree: null,
  telHeapUsage:   null,
  telHeapBar:     null,
  telIramTotal:   null,
  telIramFree:    null,
  telIramUsed:    null,
  telIramUsage:   null,
  telIramBar:     null,
  telPsramTotal:  null,
  telPsramFree:   null,
  telPsramUsed:   null,
  telPsramUsage:  null,
  telPsramBar:    null,
  telCpuCores:    null,
  telFlashSize:   null,
  telUptime:      null,

  // Sidebar / topbar status
  sidebarDot:   null,
  sidebarLabel: null,
  topbarDot:    null,
};

// ── Telemetry polling ─────────────────────────────────────────────────────────
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
  setText(Control.telHeapTotal,   bytesToKB(d.heap_total));
  setText(Control.telHeapFree,    bytesToKB(d.heap_free));
  setText(Control.telHeapUsed,    bytesToKB(d.heap_used));
  setText(Control.telHeapMinFree, bytesToKB(d.heap_min_free));
  setText(Control.telHeapUsage,   d.heap_usage.toFixed(1) + ' %');
  applyBar(Control.telHeapBar, d.heap_usage);

  setText(Control.telIramTotal,  bytesToKB(d.internal_heap_total));
  setText(Control.telIramFree,   bytesToKB(d.internal_heap_free));
  setText(Control.telIramUsed,   bytesToKB(d.internal_heap_used));
  setText(Control.telIramUsage,  d.internal_heap_usage.toFixed(1) + ' %');
  applyBar(Control.telIramBar, d.internal_heap_usage);

  setText(Control.telPsramTotal,  bytesToKB(d.psram_total));
  setText(Control.telPsramFree,   bytesToKB(d.psram_free));
  setText(Control.telPsramUsed,   bytesToKB(d.psram_used));
  setText(Control.telPsramUsage,  d.psram_usage.toFixed(1) + ' %');
  applyBar(Control.telPsramBar, d.psram_usage);

  setText(Control.telCpuCores,  d.cpu_cores);
  setText(Control.telFlashSize, bytesToKB(d.flash_size));
  setText(Control.telUptime,    fmtUptime(d.uptime_seconds));
  setText(Control.telLastUpdated, new Date().toLocaleTimeString());

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

// ── Relay ─────────────────────────────────────────────────────────────────────
function setRelayUI(on) {
  Control.relayBadge.textContent = on ? 'ON' : 'OFF';
  Control.relayBadge.classList.toggle('on', on);
  Control.relayIndicator.classList.toggle('on', on);
}

function initRelay() {
  Control.btnRelayOn.addEventListener('click', async () => {
    try {
      await API.setRelay(true);
      setRelayUI(true);
      showToast('Relay turned ON', 'success');
    } catch (err) {
      showToast('Failed to set relay: ' + err.message, 'error');
    }
  });

  Control.btnRelayOff.addEventListener('click', async () => {
    try {
      await API.setRelay(false);
      setRelayUI(false);
      showToast('Relay turned OFF');
    } catch (err) {
      showToast('Failed to set relay: ' + err.message, 'error');
    }
  });
}

// ── Schedule ──────────────────────────────────────────────────────────────────
function setScheduleBadge(enabled) {
  Control.scheduleBadge.textContent = enabled ? 'ON' : 'OFF';
  Control.scheduleBadge.classList.toggle('on', enabled);
}

function initSchedule() {
  Control.togSchedule.addEventListener('change', () => {
    setScheduleBadge(Control.togSchedule.checked);
  });

  Control.btnSaveSchedule.addEventListener('click', async () => {
    const enabled = Control.togSchedule.checked;
    const start   = Control.inpScheduleStart.value;
    const stop    = Control.inpScheduleStop.value;

    if (enabled && (!start || !stop)) {
      showToast('Set both start and stop times first', 'warn');
      return;
    }

    const now = new Date();
    const device_time =
      String(now.getHours()).padStart(2,'0') + ':' +
      String(now.getMinutes()).padStart(2,'0') + ':' +
      String(now.getSeconds()).padStart(2,'0');

    try {
      Control.btnSaveSchedule.disabled = true;
      await API.setSchedule({ enabled, start, stop, device_time });
      setScheduleBadge(enabled);
      showToast(
        enabled ? `Schedule set: ${start} → ${stop}` : 'Schedule disabled',
        'success'
      );
    } catch (err) {
      showToast('Schedule save failed: ' + err.message, 'error');
    } finally {
      Control.btnSaveSchedule.disabled = false;
    }
  });
}

// ── Peripherals ───────────────────────────────────────────────────────────────
function initPeripherals() {
  const peripherals = [
    { el: Control.togLed,   key: 'led'   },
    { el: Control.togFan,   key: 'fan'   },
    { el: Control.togLight, key: 'light' },
    { el: Control.togAuto,  key: 'auto'  },
  ];

  peripherals.forEach(({ el, key }) => {
    el.addEventListener('change', async () => {
      const on = el.checked;
      try {
        await API.setPeripheral(key, on);
        showToast(
          `${key.charAt(0).toUpperCase() + key.slice(1)} ${on ? 'enabled' : 'disabled'}`,
          'success'
        );
      } catch (err) {
        el.checked = !on;
        showToast(`Failed to toggle ${key}: ` + err.message, 'error');
      }
    });
  });
}

// ── Emergency Stop ────────────────────────────────────────────────────────────
function initEmergencyStop() {
  Control.btnEstop.addEventListener('click', async () => {
    try {
      await API.emergencyStop();
      setRelayUI(false);
      [Control.togLed, Control.togFan, Control.togLight, Control.togAuto]
        .forEach(t => { t.checked = false; });
      showToast('⬛ Emergency stop triggered — all outputs OFF', 'error', 5000);
    } catch (err) {
      showToast('E-stop failed: ' + err.message, 'error');
    }
  });
}

// ── Init ──────────────────────────────────────────────────────────────────────
function initControl() {
  // Relay
  Control.btnRelayOn     = document.getElementById('btn-relay-on');
  Control.btnRelayOff    = document.getElementById('btn-relay-off');
  Control.relayBadge     = document.getElementById('relay-badge');
  Control.relayIndicator = document.getElementById('relay-indicator');

  // Schedule
  Control.scheduleBadge    = document.getElementById('schedule-badge');
  Control.inpScheduleStart = document.getElementById('inp-schedule-start');
  Control.inpScheduleStop  = document.getElementById('inp-schedule-stop');
  Control.togSchedule      = document.getElementById('tog-schedule');
  Control.btnSaveSchedule  = document.getElementById('btn-save-schedule');

  // Peripherals
  Control.togLed   = document.getElementById('tog-led');
  Control.togFan   = document.getElementById('tog-fan');
  Control.togLight = document.getElementById('tog-light');
  Control.togAuto  = document.getElementById('tog-auto');

  // Emergency stop
  Control.btnEstop = document.getElementById('btn-estop');

  // Telemetry
  Control.telDot         = document.getElementById('telemetry-status-dot');
  Control.telLastUpdated = document.getElementById('tel-last-updated');
  Control.telHeapTotal   = document.getElementById('tel-heap-total');
  Control.telHeapFree    = document.getElementById('tel-heap-free');
  Control.telHeapUsed    = document.getElementById('tel-heap-used');
  Control.telHeapMinFree = document.getElementById('tel-heap-min-free');
  Control.telHeapUsage   = document.getElementById('tel-heap-usage');
  Control.telHeapBar     = document.getElementById('tel-heap-bar');
  Control.telIramTotal   = document.getElementById('tel-iram-total');
  Control.telIramFree    = document.getElementById('tel-iram-free');
  Control.telIramUsed    = document.getElementById('tel-iram-used');
  Control.telIramUsage   = document.getElementById('tel-iram-usage');
  Control.telIramBar     = document.getElementById('tel-iram-bar');
  Control.telPsramTotal  = document.getElementById('tel-psram-total');
  Control.telPsramFree   = document.getElementById('tel-psram-free');
  Control.telPsramUsed   = document.getElementById('tel-psram-used');
  Control.telPsramUsage  = document.getElementById('tel-psram-usage');
  Control.telPsramBar    = document.getElementById('tel-psram-bar');
  Control.telCpuCores    = document.getElementById('tel-cpu-cores');
  Control.telFlashSize   = document.getElementById('tel-flash-size');
  Control.telUptime      = document.getElementById('tel-uptime');

  // Sidebar / topbar
  Control.sidebarDot   = document.getElementById('sidebar-status-dot');
  Control.sidebarLabel = document.getElementById('sidebar-status-label');
  Control.topbarDot    = document.getElementById('topbar-status-dot');

  initRelay();
  initSchedule();
  initPeripherals();
  initEmergencyStop();
  startTelemetryPolling();
}
