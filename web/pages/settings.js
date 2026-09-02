/* ============================================================
   pages/settings.js  —  Settings page
   Network config · LED toggle · Device actions
   ============================================================ */

'use strict';

const Settings = {
  inpSsid:        null,
  inpPassOld:     null,
  inpPassNew:     null,
  inpChannel:     null,
  inpMaxConn:     null,
  togDhcp:        null,
  staticIpFields: null,
  inpIp:          null,
  inpMask:        null,
  inpGw:          null,
  inpDns:         null,
  settingsError:  null,
  btnSave:        null,
  togLed:         null,
  btnReboot:      null,
  btnFactory:     null,
};

const IP_RE = /^(\d{1,3}\.){3}\d{1,3}$/;
function validateIP(str) {
  if (!IP_RE.test(str)) return false;
  return str.split('.').every(n => +n <= 255);
}

function showSettingsError(msg) {
  Settings.settingsError.textContent = msg;
}

async function loadSettings() {
  try {
    const d = await API.getSettings();
    if (d.ssid)            Settings.inpSsid.value    = d.ssid;
    if (d.channel)         Settings.inpChannel.value = d.channel;
    if (d.max_connections) Settings.inpMaxConn.value = d.max_connections;
    if (d.ip)              Settings.inpIp.value      = d.ip;
    if (d.gateway)         Settings.inpGw.value      = d.gateway;
    if (d.netmask)         Settings.inpMask.value    = d.netmask;
  } catch (err) {
    console.warn('[settings] failed to load:', err.message);
  }
}

function initSettings() {
  Settings.inpSsid        = document.getElementById('inp-ssid');
  Settings.inpPassOld     = document.getElementById('inp-pass-old');
  Settings.inpPassNew     = document.getElementById('inp-pass-new');
  Settings.inpChannel     = document.getElementById('inp-channel');
  Settings.inpMaxConn     = document.getElementById('inp-max-conn');
  Settings.togDhcp        = document.getElementById('tog-dhcp');
  Settings.staticIpFields = document.getElementById('static-ip-fields');
  Settings.inpIp          = document.getElementById('inp-ip');
  Settings.inpMask        = document.getElementById('inp-mask');
  Settings.inpGw          = document.getElementById('inp-gw');
  Settings.inpDns         = document.getElementById('inp-dns');
  Settings.settingsError  = document.getElementById('settings-error');
  Settings.btnSave        = document.getElementById('btn-save-settings');
  Settings.togLed         = document.getElementById('tog-led');
  Settings.btnReboot      = document.getElementById('btn-reboot');
  Settings.btnFactory     = document.getElementById('btn-factory');

  // DHCP toggle
  Settings.togDhcp.addEventListener('change', () => {
    Settings.staticIpFields.classList.toggle('visible', !Settings.togDhcp.checked);
    if (Settings.togDhcp.checked) {
      [Settings.inpIp, Settings.inpMask, Settings.inpGw, Settings.inpDns]
        .forEach(el => el.classList.remove('invalid'));
      showSettingsError('');
    }
  });

  // LED toggle
  Settings.togLed.addEventListener('change', async () => {
    const on = Settings.togLed.checked;
    try {
      await API.setLedEnabled(on);
      showToast(on ? 'Status LED enabled' : 'Status LED disabled', 'success');
    } catch (err) {
      Settings.togLed.checked = !on;
      showToast('Failed: ' + err.message, 'error');
    }
  });

  // Save network settings
  Settings.btnSave.addEventListener('click', async () => {
    showSettingsError('');
    [Settings.inpIp, Settings.inpMask, Settings.inpGw, Settings.inpDns,
     Settings.inpChannel, Settings.inpMaxConn]
      .forEach(el => el.classList.remove('invalid'));

    const dhcp    = Settings.togDhcp.checked;
    const channel = parseInt(Settings.inpChannel.value, 10);
    const maxConn = parseInt(Settings.inpMaxConn.value, 10);

    if (isNaN(channel) || channel < 1 || channel > 13) {
      showSettingsError('Channel must be between 1 and 13.');
      Settings.inpChannel.classList.add('invalid');
      return;
    }
    if (isNaN(maxConn) || maxConn < 1 || maxConn > 10) {
      showSettingsError('Max connections must be between 1 and 10.');
      Settings.inpMaxConn.classList.add('invalid');
      return;
    }

    const payload = {
      ssid:            Settings.inpSsid.value.trim(),
      channel,
      max_connections: maxConn,
      dhcp,
    };

    if (Settings.inpPassNew.value) {
      payload.password     = Settings.inpPassNew.value;
      payload.old_password = Settings.inpPassOld.value;
    }

    if (!dhcp) {
      const fields = [
        { el: Settings.inpIp,   key: 'ip',      label: 'Static IP'   },
        { el: Settings.inpMask, key: 'netmask',  label: 'Subnet Mask' },
        { el: Settings.inpGw,   key: 'gateway',  label: 'Gateway'     },
        { el: Settings.inpDns,  key: 'dns',      label: 'DNS Server'  },
      ];
      for (const f of fields) {
        const val = f.el.value.trim();
        if (!validateIP(val)) {
          f.el.classList.add('invalid');
          showSettingsError(`Invalid ${f.label} address.`);
          return;
        }
        payload[f.key] = val;
      }
    }

    try {
      Settings.btnSave.disabled = true;
      const result = await API.saveSettings(payload);

      if (result.reboot) {
        const newSsid = payload.ssid || Settings.inpSsid.value.trim();
        const newIp   = (!payload.dhcp && payload.ip) ? payload.ip : window.location.hostname;
        stopTelemetryPolling();
        setOnline(false);
        await showInfoModal(
          'Device Rebooting',
          `Settings saved. The device is restarting.<br><br>` +
          `<strong>Network:</strong> ${newSsid}<br>` +
          `<strong>Address:</strong> http://${newIp}<br><br>` +
          `Reconnect to the WiFi network above, then open the address.`,
          'OK'
        );
        window.location.href = `http://${newIp}`;
      } else {
        showToast('Settings saved', 'success');
      }
    } catch (err) {
      showToast('Save failed: ' + err.message, 'error');
    } finally {
      Settings.btnSave.disabled = false;
    }
  });

  // Reboot
  Settings.btnReboot.addEventListener('click', async () => {
    const ok = await confirmAction(
      'Restart Device',
      'The device will restart and apply saved settings.',
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
  Settings.btnFactory.addEventListener('click', async () => {
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