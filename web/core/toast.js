/* ============================================================
   core/toast.js  —  Toast notification
   ============================================================ */

'use strict';

let _toastEl    = null;
let _toastTimer = null;

function initToast() {
  _toastEl = document.getElementById('toast');
}

/**
 * @param {string} msg
 * @param {'success'|'error'|'warn'|''} type
 * @param {number} duration ms
 */
function showToast(msg, type = '', duration = 3000) {
  if (!_toastEl) return;
  clearTimeout(_toastTimer);
  _toastEl.textContent = msg;
  _toastEl.className   = `toast show ${type}`;
  _toastTimer = setTimeout(() => {
    _toastEl.classList.remove('show');
  }, duration);
}