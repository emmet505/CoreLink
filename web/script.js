/* ============================================================
   script.js  —  Navigation + App init
   All logic lives in core/ and pages/ modules.
   ============================================================ */

'use strict';

// ── Navigation ────────────────────────────────────────────────────────────────
let _menuBtn        = null;
let _sidebarEl      = null;
let _sidebarOverlay = null;
let _navItems       = null;
let _pages          = null;


async function syncDeviceTime() {
  //if (sessionStorage.getItem('time_synced')) return;

  try {
    const status = await API.getStatus();
    if (status.sta_connected) {
      console.log('[time] STA connected; using NTP time');
      return;
    }

    const now = new Date();
    await API.request('/api/time', {
      method: 'POST',
      body: JSON.stringify({
        hour:   now.getHours(),
        minute: now.getMinutes(),
        second: now.getSeconds(),
      }),
    });
    sessionStorage.setItem('time_synced', '1');
    console.log('[time] fallback sync sent; STA is unavailable');
  } catch (err) {
    console.warn('[time] sync failed:', err.message);
  }
}


function closeSidebar() {
  _sidebarEl.classList.remove('open');
  _sidebarOverlay.classList.remove('open');
  _menuBtn.classList.remove('open');
}

function toggleSidebar() {
  const isOpen = _sidebarEl.classList.contains('open');
  if (isOpen) { closeSidebar(); }
  else {
    _sidebarEl.classList.add('open');
    _sidebarOverlay.classList.add('open');
    _menuBtn.classList.add('open');
  }
}

function initNav() {
  _menuBtn        = document.getElementById('menu-btn');
  _sidebarEl      = document.getElementById('sidebar');
  _sidebarOverlay = document.getElementById('sidebar-overlay');
  _navItems       = document.querySelectorAll('.nav-item');
  _pages          = document.querySelectorAll('.page');

  _menuBtn.addEventListener('click', toggleSidebar);
  _sidebarOverlay.addEventListener('click', closeSidebar);

  _navItems.forEach((item) => {
    item.addEventListener('click', () => {
      const target = item.dataset.page;

      _navItems.forEach(i => i.classList.remove('active'));
      _pages.forEach(p => p.classList.remove('active'));

      item.classList.add('active');
      document.getElementById(`page-${target}`)?.classList.add('active');

      if (target === 'settings') loadSettings();

      closeSidebar();
    });
  });
}

// ── Init ──────────────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  initToast();
  initModal();
  initNav();
  initControl();
  initSettings();
  initUpdate();
  syncDeviceTime();
});