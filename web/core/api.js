/* ============================================================
   core/api.js  —  HTTP API layer
   All fetch() calls route through API.request().
   ============================================================ */

'use strict';

const API = {
  BASE: window.location.origin,

  async request(path, opts = {}) {
    const defaults = {
      signal: AbortSignal.timeout(5000),
      headers: { 'Content-Type': 'application/json', ...(opts.headers || {}) },
    };
    const res = await fetch(API.BASE + path, { ...defaults, ...opts });
    if (!res.ok) throw new Error(`HTTP ${res.status} ${res.statusText}`);
    return res.json();
  },

  getStatus()        { return API.request('/api/status'); },
  getSettings()      { return API.request('/api/settings'); },
  setRelay(state)    { return API.request('/api/relay',      { method: 'POST', body: JSON.stringify({ state }) }); },
  setPeripheral(k,v) { return API.request('/api/peripheral', { method: 'POST', body: JSON.stringify({ [k]: v }) }); },
  emergencyStop()    { return API.request('/api/estop',      { method: 'POST' }); },
  saveSettings(data) { return API.request('/api/settings',   { method: 'POST', body: JSON.stringify(data) }); },
  reboot()           { return API.request('/api/reboot',     { method: 'POST' }); },
  factoryReset()     { return API.request('/api/factory',    { method: 'POST' }); },
  setSchedule(data)  { return API.request('/api/relay/schedule', { method: 'POST', body: JSON.stringify(data) }); },
  setLedEnabled(enabled) { return API.request('/api/led', { method: 'POST', body: JSON.stringify({ enabled })}); },

  /**
   * Upload a binary file to an OTA endpoint.
   * Uses fetch with raw binary body (no JSON).
   * @param {string} endpoint  - '/api/ota/firmware' or '/api/ota/webfs'
   * @param {File}   file      - File object from <input type="file">
   * @param {function} onProgress - callback(percent: number)
   */
  async otaUpload(endpoint, file, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', API.BASE + endpoint);
      xhr.timeout = 120000; // 2 minutes for large files

      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable && onProgress) {
          onProgress(Math.round((e.loaded / e.total) * 100));
        }
      };

      xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) {
          try { resolve(JSON.parse(xhr.responseText)); }
          catch { resolve({}); }
        } else {
          reject(new Error(`HTTP ${xhr.status}`));
        }
      };

      xhr.onerror   = () => reject(new Error('Network error'));
      xhr.ontimeout = () => reject(new Error('Upload timed out'));

      xhr.send(file);
    });
  },
};
