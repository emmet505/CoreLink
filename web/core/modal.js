/* ============================================================
   core/modal.js  —  Confirm modal
   ============================================================ */

'use strict';

let _modal         = {};
let _modalResolve  = null;

function initModal() {
  _modal.backdrop = document.getElementById('modal-backdrop');
  _modal.title    = document.getElementById('modal-title');
  _modal.body     = document.getElementById('modal-body');
  _modal.confirm  = document.getElementById('modal-confirm');
  _modal.cancel   = document.getElementById('modal-cancel');

  _modal.confirm.addEventListener('click', () => closeModal(true));
  _modal.cancel.addEventListener('click',  () => closeModal(false));
  _modal.backdrop.addEventListener('click', (e) => {
    if (e.target === _modal.backdrop) closeModal(false);
  });
}

function confirmAction(title, body, dangerLabel = 'Confirm') {
  return new Promise((resolve) => {
    _modalResolve = resolve;
    _modal.title.textContent   = title;
    _modal.body.textContent    = body;
    _modal.confirm.textContent = dangerLabel;
    _modal.backdrop.classList.add('open');
  });
}

function closeModal(result) {
  _modal.backdrop.classList.remove('open');
  if (_modalResolve) { _modalResolve(result); _modalResolve = null; }
}

function showInfoModal(title, bodyHtml, confirmLabel = 'OK') {
  return new Promise((resolve) => {
    _modalResolve = resolve;
    _modal.title.textContent  = title;
    _modal.body.innerHTML     = bodyHtml;
    _modal.confirm.textContent = confirmLabel;
    _modal.cancel.style.display = 'none';
    _modal.backdrop.classList.add('open');
  }).finally(() => {
    _modal.cancel.style.display = '';
  });
}
