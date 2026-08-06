/* ================================================================
   ESP32-S3 Control Panel — script.js
   Vanilla JavaScript, no framework, no external dependencies.

   Sections:
   1.  Simulated API layer      — swap the bodies for fetch() calls
                                   to the real ESP-IDF HTTP server.
   2.  App state
   3.  DOM cache
   4.  Navigation
   5.  Toast notifications
   6.  Confirmation modal
   7.  Toggle / switch controls
   8.  Emergency stop
   9.  Telemetry simulation
   10. Settings form
   11. Device management (restart / factory reset)
   12. Init
   ================================================================ */
"use strict";

/* ----------------------------------------------------------------
   1. SIMULATED API LAYER
   Every call here logs the request it *would* make and resolves
   after a short delay, standing in for the real ESP-IDF REST API:
     GET  /api/settings
     POST /api/settings
     POST /api/reboot
     POST /api/reset
     GET  /api/status
     POST /api/relay
   Replace `request()`'s body with a real `fetch(url, options)`
   once the firmware endpoints exist — every caller below already
   awaits a promise and handles failure, so no other code changes.
---------------------------------------------------------------- */
const API = {
  BASE_URL: "", // same-origin as the ESP32 HTTP server

  async request(path, options = {}) {
    const body = options.body ? JSON.parse(options.body) : undefined;
    console.log(`[API] would ${options.method || "GET"} ${API.BASE_URL}${path}`, body ?? "");
    await new Promise((resolve) => setTimeout(resolve, 150 + Math.random() * 200));
    return { ok: true };
  },

  getStatus() {
    return API.request("/api/status");
  },
  setRelay(state) {
    return API.request("/api/relay", { method: "POST", body: JSON.stringify({ state }) });
  },
  setOutput(name, state) {
    return API.request(`/api/${name}`, { method: "POST", body: JSON.stringify({ state }) });
  },
  getSettings() {
    return API.request("/api/settings");
  },
  saveSettings(payload) {
    return API.request("/api/settings", { method: "POST", body: JSON.stringify(payload) });
  },
  reboot() {
    return API.request("/api/reboot", { method: "POST" });
  },
  factoryReset() {
    return API.request("/api/reset", { method: "POST" });
  },
};

/* ----------------------------------------------------------------
   2. APP STATE
---------------------------------------------------------------- */
const state = {
  controls: { relay: false, led: false, fan: false, light: false, auto: false },
  estopEngaged: false,
  connectionOnline: true,
  uptimeSeconds: 0,
};

const CONTROL_LABELS = {
  relay: "Relay",
  led: "LED",
  fan: "Fan",
  light: "Light",
  auto: "Auto Mode",
};

/* ----------------------------------------------------------------
   3. DOM CACHE
---------------------------------------------------------------- */
const dom = {};

function cacheDom() {
  const ids = [
    "sidebar", "sidebarBackdrop", "sidebarConnLed", "sidebarConnText",
    "menuToggle", "pageTitle", "deviceClock",
    "relayToggle", "relayStatusText", "relayLed",
    "estopCard", "estopBtn", "estopBanner", "estopClear",
    "statusConnLed", "statusConnText", "statusUptime", "statusTemp",
    "statusCpuText", "statusCpuGauge", "statusMemText", "statusMemGauge",
    "settingsForm", "ssid", "password", "togglePassword",
    "staticIpToggle", "staticIpFields", "staticIp", "gateway", "netmask",
    "httpPort", "maxConn", "cancelSettings", "saveSettings",
    "restartBtn", "factoryResetBtn",
    "toastRegion",
    "modalBackdrop", "modalTitle", "modalMessage", "modalCancel", "modalConfirm",
  ];
  ids.forEach((id) => { dom[id] = document.getElementById(id); });
}

/* ----------------------------------------------------------------
   4. NAVIGATION
---------------------------------------------------------------- */
function initNavigation() {
  document.querySelectorAll(".nav-link").forEach((link) => {
    link.addEventListener("click", () => switchPage(link.dataset.page));
  });

  dom.menuToggle.addEventListener("click", toggleMobileNav);
  dom.sidebarBackdrop.addEventListener("click", closeMobileNav);
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") closeMobileNav();
  });
}

function switchPage(pageId) {
  document.querySelectorAll(".nav-link").forEach((link) => {
    const isActive = link.dataset.page === pageId;
    link.classList.toggle("is-active", isActive);
    link.setAttribute("aria-current", isActive ? "page" : "false");
  });

  document.querySelectorAll(".page").forEach((section) => {
    section.classList.toggle("is-active", section.dataset.page === pageId);
  });

  dom.pageTitle.textContent = pageId === "settings" ? "Settings" : "Device Control";
  closeMobileNav();
}

function toggleMobileNav() {
  const isOpen = dom.sidebar.classList.toggle("is-open");
  dom.sidebarBackdrop.classList.toggle("is-open", isOpen);
  dom.menuToggle.setAttribute("aria-expanded", String(isOpen));
}

function closeMobileNav() {
  dom.sidebar.classList.remove("is-open");
  dom.sidebarBackdrop.classList.remove("is-open");
  dom.menuToggle.setAttribute("aria-expanded", "false");
}

/* ----------------------------------------------------------------
   5. TOAST NOTIFICATIONS
---------------------------------------------------------------- */
function showToast(message, variant = "info") {
  const toast = document.createElement("div");
  toast.className = `toast toast--${variant}`;
  toast.textContent = message;
  dom.toastRegion.appendChild(toast);

  requestAnimationFrame(() => toast.classList.add("is-visible"));

  setTimeout(() => {
    toast.classList.remove("is-visible");
    toast.addEventListener("transitionend", () => toast.remove(), { once: true });
  }, 3200);
}

/* ----------------------------------------------------------------
   6. CONFIRMATION MODAL
   Generic, reusable — used by Restart Device and Factory Reset.
---------------------------------------------------------------- */
function confirmAction({ title, message, confirmLabel = "Confirm", variant = "danger", onConfirm }) {
  dom.modalTitle.textContent = title;
  dom.modalMessage.textContent = message;
  dom.modalConfirm.textContent = confirmLabel;
  dom.modalConfirm.className = `btn btn--${variant}`;

  dom.modalBackdrop.classList.add("is-open");
  document.body.classList.add("modal-open");
  dom.modalConfirm.focus();

  function cleanup() {
    dom.modalBackdrop.classList.remove("is-open");
    document.body.classList.remove("modal-open");
    dom.modalConfirm.removeEventListener("click", handleConfirm);
    dom.modalCancel.removeEventListener("click", handleCancel);
    document.removeEventListener("keydown", handleKeydown);
  }
  function handleConfirm() { cleanup(); onConfirm(); }
  function handleCancel() { cleanup(); }
  function handleKeydown(event) { if (event.key === "Escape") handleCancel(); }

  dom.modalConfirm.addEventListener("click", handleConfirm);
  dom.modalCancel.addEventListener("click", handleCancel);
  document.addEventListener("keydown", handleKeydown);
}

/* ----------------------------------------------------------------
   7. TOGGLE / SWITCH CONTROLS
   Handles relay, LED, fan, light, and auto-mode — all driven by
   [data-control] buttons sharing the same .switch markup.
---------------------------------------------------------------- */
function initSwitches() {
  document.querySelectorAll("[data-control]").forEach((btn) => {
    if (btn.dataset.control === "staticIp") return; // wired separately in Settings
    btn.addEventListener("click", () => handleToggle(btn));
  });
}

async function handleToggle(btn) {
  const control = btn.dataset.control;

  if (state.estopEngaged) {
    showToast("Clear the emergency stop before changing outputs.", "warning");
    return;
  }

  const next = !state.controls[control];
  setSwitchVisual(btn, next);
  state.controls[control] = next;
  updateControlDisplay(control, next);

  try {
    if (control === "relay") {
      await API.setRelay(next);
    } else {
      await API.setOutput(control, next);
    }
    showToast(`${CONTROL_LABELS[control]} turned ${next ? "ON" : "OFF"}`, next ? "success" : "info");
  } catch (error) {
    // Defensive rollback — the simulated API never rejects, but a real
    // network call can, so the UI stays truthful to device state.
    setSwitchVisual(btn, !next);
    state.controls[control] = !next;
    updateControlDisplay(control, !next);
    showToast(`Failed to update ${CONTROL_LABELS[control]}`, "danger");
  }
}

function setSwitchVisual(btn, isOn) {
  btn.classList.toggle("is-on", isOn);
  btn.setAttribute("aria-checked", String(isOn));
}

function updateControlDisplay(control, isOn) {
  if (control === "relay") {
    dom.relayStatusText.textContent = isOn ? "ON" : "OFF";
    dom.relayStatusText.classList.toggle("is-on", isOn);
    dom.relayStatusText.classList.toggle("is-off", !isOn);
    dom.relayLed.classList.toggle("led--on", isOn);
    return;
  }
  const stateLabel = document.querySelector(`[data-state-for="${control}"]`);
  if (stateLabel) stateLabel.textContent = isOn ? "ON" : "OFF";
}

/* ----------------------------------------------------------------
   8. EMERGENCY STOP
---------------------------------------------------------------- */
function initEstop() {
  dom.estopBtn.addEventListener("click", engageEstop);
  dom.estopClear.addEventListener("click", disengageEstop);
}

function engageEstop() {
  state.estopEngaged = true;

  Object.keys(state.controls).forEach((control) => {
    state.controls[control] = false;
    const btn = document.querySelector(`[data-control="${control}"]`);
    if (btn) setSwitchVisual(btn, false);
    updateControlDisplay(control, false);
  });

  dom.estopBanner.hidden = false;
  dom.estopCard.classList.add("is-engaged");
  API.request("/api/estop", { method: "POST", body: JSON.stringify({ engaged: true }) });
  showToast("Emergency stop engaged — all outputs disabled.", "danger");
}

function disengageEstop() {
  state.estopEngaged = false;
  dom.estopBanner.hidden = true;
  dom.estopCard.classList.remove("is-engaged");
  API.request("/api/estop", { method: "POST", body: JSON.stringify({ engaged: false }) });
  showToast("Emergency stop cleared.", "info");
}

/* ----------------------------------------------------------------
   9. TELEMETRY SIMULATION
   Fake but plausible fluctuating values for temperature, CPU, and
   memory usage, plus a running uptime clock and device clock.
   Replace with periodic GET /api/status polling on real hardware.
---------------------------------------------------------------- */
function startTelemetry() {
  updateClock();
  setInterval(updateClock, 1000);

  updateTelemetry();
  setInterval(updateTelemetry, 2500);
}

function updateClock() {
  state.uptimeSeconds += 1;
  dom.statusUptime.textContent = formatUptime(state.uptimeSeconds);
  dom.deviceClock.textContent = new Date().toLocaleTimeString([], { hour12: false });
}

function formatUptime(totalSeconds) {
  const h = String(Math.floor(totalSeconds / 3600)).padStart(2, "0");
  const m = String(Math.floor((totalSeconds % 3600) / 60)).padStart(2, "0");
  const s = String(totalSeconds % 60).padStart(2, "0");
  return `${h}:${m}:${s}`;
}

function updateTelemetry() {
  const temp = (38 + Math.random() * 7).toFixed(1);
  const cpu = Math.round(15 + Math.random() * 35);
  const mem = Math.round(38 + Math.random() * 22);

  dom.statusTemp.textContent = `${temp}\u00B0C`;
  dom.statusCpuText.textContent = `${cpu}%`;
  dom.statusMemText.textContent = `${mem}%`;
  dom.statusCpuGauge.style.width = `${cpu}%`;
  dom.statusMemGauge.style.width = `${mem}%`;

  // Occasionally simulate a brief connection blip for realism.
  if (Math.random() < 0.03 && state.connectionOnline) {
    setConnectionStatus(false);
    setTimeout(() => setConnectionStatus(true), 1800);
  }
}

function setConnectionStatus(isOnline) {
  state.connectionOnline = isOnline;
  [dom.statusConnLed, dom.sidebarConnLed].forEach((led) => led.classList.toggle("led--on", isOnline));
  dom.statusConnText.textContent = isOnline ? "Online" : "Reconnecting\u2026";
  dom.sidebarConnText.textContent = isOnline ? "Online" : "Offline";
}

/* ----------------------------------------------------------------
   10. SETTINGS FORM
---------------------------------------------------------------- */
let currentSettings = defaultSettings();

function defaultSettings() {
  return {
    ssid: "ESP32-S3-Device",
    password: "",
    useStaticIp: false,
    staticIp: "192.168.1.100",
    gateway: "192.168.1.1",
    netmask: "255.255.255.0",
    httpPort: 80,
    maxConn: 4,
  };
}

function initSettingsForm() {
  loadSettingsIntoForm(currentSettings);

  dom.staticIpToggle.addEventListener("click", () => {
    const enabled = !dom.staticIpToggle.classList.contains("is-on");
    setSwitchVisual(dom.staticIpToggle, enabled);
    toggleStaticIpFields(enabled);
  });

  dom.togglePassword.addEventListener("click", togglePasswordVisibility);
  dom.settingsForm.addEventListener("submit", handleSaveSettings);
  dom.cancelSettings.addEventListener("click", handleCancelSettings);
}

function toggleStaticIpFields(enabled) {
  ["staticIp", "gateway", "netmask"].forEach((id) => {
    dom[id].disabled = !enabled;
    dom[id].required = enabled;
    if (!enabled) dom[id].value = dom[id].value; // keep value, just stop validating/submitting it
  });
}

function loadSettingsIntoForm(settings) {
  dom.ssid.value = settings.ssid;
  dom.password.value = settings.password;
  dom.staticIp.value = settings.staticIp;
  dom.gateway.value = settings.gateway;
  dom.netmask.value = settings.netmask;
  dom.httpPort.value = settings.httpPort;
  dom.maxConn.value = settings.maxConn;
  setSwitchVisual(dom.staticIpToggle, settings.useStaticIp);
  toggleStaticIpFields(settings.useStaticIp);
}

function collectSettingsFromForm() {
  return {
    ssid: dom.ssid.value.trim(),
    password: dom.password.value,
    useStaticIp: dom.staticIpToggle.classList.contains("is-on"),
    staticIp: dom.staticIp.value.trim(),
    gateway: dom.gateway.value.trim(),
    netmask: dom.netmask.value.trim(),
    httpPort: Number(dom.httpPort.value),
    maxConn: Number(dom.maxConn.value),
  };
}

async function handleSaveSettings(event) {
  event.preventDefault();

  if (!dom.settingsForm.checkValidity()) {
    dom.settingsForm.reportValidity();
    showToast("Please fix the highlighted fields.", "danger");
    return;
  }

  const payload = collectSettingsFromForm();
  console.log("[Settings] Saving configuration:", payload);

  dom.saveSettings.disabled = true;
  try {
    await API.saveSettings(payload);
    currentSettings = payload;
    showToast("Settings saved (simulated).", "success");
  } finally {
    dom.saveSettings.disabled = false;
  }
}

function handleCancelSettings() {
  loadSettingsIntoForm(currentSettings);
  showToast("Changes discarded.", "info");
}

function togglePasswordVisibility() {
  const showing = dom.password.type === "text";
  dom.password.type = showing ? "password" : "text";
  dom.togglePassword.querySelector(".icon-eye").hidden = !showing;
  dom.togglePassword.querySelector(".icon-eye-off").hidden = showing;
  dom.togglePassword.setAttribute("aria-label", showing ? "Show password" : "Hide password");
  dom.togglePassword.classList.toggle("is-active", !showing);
}

/* ----------------------------------------------------------------
   11. DEVICE MANAGEMENT (Restart / Factory Reset)
---------------------------------------------------------------- */
function initDeviceManagement() {
  dom.restartBtn.addEventListener("click", () => {
    confirmAction({
      title: "Restart device?",
      message: "The device will reboot and briefly disconnect from the network.",
      confirmLabel: "Restart",
      variant: "warning",
      onConfirm: async () => {
        console.log("[Device] Restart requested");
        await API.reboot();
        showToast("Restart command sent (simulated).", "warning");
      },
    });
  });

  dom.factoryResetBtn.addEventListener("click", () => {
    confirmAction({
      title: "Factory reset device?",
      message: "This erases all saved settings and cannot be undone.",
      confirmLabel: "Factory Reset",
      variant: "danger",
      onConfirm: async () => {
        console.log("[Device] Factory reset requested");
        await API.factoryReset();
        showToast("Factory reset command sent (simulated).", "danger");
      },
    });
  });
}

/* ----------------------------------------------------------------
   12. INIT
---------------------------------------------------------------- */
function init() {
  cacheDom();
  initNavigation();
  initSwitches();
  initEstop();
  initSettingsForm();
  initDeviceManagement();
  startTelemetry();

  API.getStatus(); // placeholder for an initial real status fetch
}

document.addEventListener("DOMContentLoaded", init);
