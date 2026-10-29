(() => {
  const $ = (s, r = document) => r.querySelector(s);
  const store = {
    get(k, d) { try { const v = localStorage.getItem('fi.' + k); return v === null ? d : JSON.parse(v); } catch { return d; } },
    set(k, v) { try { localStorage.setItem('fi.' + k, JSON.stringify(v)); } catch { /* private mode */ } },
  };
  const S = {
    tab: 'home', lang: store.get('lang', (navigator.language || 'en').startsWith('ru') ? 'ru' : (navigator.language || '').startsWith('uk') ? 'ua' : 'en'),
    island: store.get('island', false), theme: store.get('theme', 'auto'), look: store.get('look', 'google'),
    tools: { fastboot: false, adb: false }, device: null, ipsw: null, ipswInfo: null, simg: null, image: null,
    step: 0, busy: false, log: [], progress: 0,
  };
  const t = (k) => (window.I18N[S.lang] && window.I18N[S.lang][k]) || window.I18N.en[k] || k;

  // the logo: a plain green apple with Mitlk's eyes
  const logo = (cls = 'logo') => `<svg class="${cls}" viewBox="0 0 100 100" aria-label="FiMiatoll">
    <defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#7dffb0"/><stop offset="1" stop-color="#1fae62"/></linearGradient></defs>
    <path d="M50 30C38 20 14 24 14 52c0 24 18 40 28 40 4 0 6-2 8-2s4 2 8 2c10 0 28-16 28-40 0-28-24-32-36-22z" fill="url(#g)"/>
    <path d="M52 24C52 12 62 6 70 8c-2 10-10 16-18 16z" fill="#3ddc84"/>
    <rect x="31" y="46" width="11" height="17" rx="3" fill="#0a0f11"/><rect x="58" y="46" width="11" height="17" rx="3" fill="#0a0f11"/>
    <rect x="34" y="49" width="4" height="4" rx="1.5" fill="#d6ffe6"/><rect x="61" y="49" width="4" height="4" rx="1.5" fill="#d6ffe6"/>
    <path d="M42 70q8 7 16 0" stroke="#0a0f11" stroke-width="3.4" fill="none" stroke-linecap="round"/></svg>`;

  // ---------- Dynamic Island ----------
  const isl = { timer: 0 };
  function island(title, sub, opts = {}) {
    const el = $('#island');
    if (!S.island) { el.classList.add('hidden'); return; }
    el.classList.remove('hidden');
    $('#island-title').textContent = title; $('#island-sub').textContent = sub || '';
    el.classList.toggle('prog', !!opts.progress);
    if (opts.progress) $('#island-fill').style.width = S.progress + '%';
    requestAnimationFrame(() => el.classList.add('open'));
    clearTimeout(isl.timer);
    if (!opts.stay) isl.timer = setTimeout(() => el.classList.remove('open'), 2600);
  }
  function applyIsland() {
    const el = $('#island');
    el.classList.toggle('hidden', !S.island);
    if (!S.island) el.classList.remove('open');
  }

  // ---------- helpers ----------
  const ico = (bg, path) => `<span class="ico" style="background:${bg}"><svg viewBox="0 0 24 24"><path d="${path}"/></svg></span>`;
  const P = {
    phone: 'M7 2h10a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2zm5 18a1 1 0 1 0 0-2 1 1 0 0 0 0 2z',
    bolt: 'M13 2 4 14h7l-1 8 10-13h-7z', gear: 'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z', globe: 'M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20z',
    pill: 'M7 3h10a4 4 0 0 1 0 8H7a4 4 0 0 1 0-8zm0 10h10a4 4 0 0 1 0 8H7a4 4 0 0 1 0-8z', check: 'M9 16.2 4.8 12l-1.4 1.4L9 19 21 7l-1.4-1.4z',
    file: 'M6 2h8l6 6v12a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2z',
  };
  function addLog(l) { S.log.push(l); if (S.log.length > 400) S.log.shift(); const el = $('#log'); if (el) { el.textContent = S.log.join('\n'); el.scrollTop = el.scrollHeight; } }
  function modeText(m) { return m === 'fastboot' ? t('modeFastboot') : m === 'recovery' ? t('modeRecovery') : m === 'sideload' ? t('modeSideload') : t('modeAndroid'); }

  function confirmBox(word, body) {
    return new Promise((resolve) => {
      const m = $('#modal');
      m.innerHTML = `<div class="sheet"><h3>${t('confirmTitle')}</h3><p>${body}</p><p><small>${word === 'ERASE' ? t('typeErase') : t('typeFlash')}</small></p>
        <input id="cw" autocomplete="off" spellcheck="false"><div class="btns" style="padding:0"><button class="btn danger" id="cgo" disabled>${t('go')}</button><button class="btn sec" id="cno">${t('cancel')}</button></div></div>`;
      m.classList.remove('hidden');
      const inp = $('#cw', m), go = $('#cgo', m);
      inp.oninput = () => { go.disabled = inp.value.trim() !== word; };
      const close = (v) => { m.classList.add('hidden'); m.innerHTML = ''; resolve(v); };
      go.onclick = () => close(true); $('#cno', m).onclick = () => close(false);
      inp.focus();
    });
  }

  async function run(label, fn) {
    if (S.busy) return;
    S.busy = true; S.progress = 0; S.log = []; render();
    island(label, '', { progress: true, stay: true });
    let ok = false;
    try { ok = await fn(); } catch (e) { addLog('error: ' + e.message); }
    S.busy = false; S.progress = ok ? 100 : 0;
    island(ok ? t('done') : t('failed'), label);
    render();
  }

  // ---------- pages ----------
  function pageHome() {
    const d = S.device;
    return `<div class="hero">${logo()}</div><h1 style="text-align:center">FiMiatoll</h1><p class="sub" style="text-align:center">${t('homeSub')}</p>
      <div class="group">
        <div class="row">${ico(d ? '#34c759' : '#8e8e93', P.phone)}<div class="txt"><b>${d ? 'Redmi Note 9S' : t('noDevice')}</b><small>${d ? `${t('connected')} · ${modeText(d.mode)} · ${d.id}` : t('noDeviceSub')}</small></div><span class="badge ${d ? 'ok' : ''}">${d ? modeText(d.mode) : '–'}</span></div>
        <div class="row">${ico(S.tools.fastboot && S.tools.adb ? '#0a84ff' : '#ff453a', P.bolt)}<div class="txt"><b>platform-tools</b><small>${S.tools.fastboot && S.tools.adb ? t('toolsOk') : t('toolsNo')}</small></div></div>
      </div>
      <div class="group"><div class="btns"><button class="btn" data-go="flash">${t('quickFlash')}</button><button class="btn sec" data-go="learn">${t('quickLearn')}</button>
        <button class="btn sec" id="rb-bl" ${d ? '' : 'disabled'}>${t('rebootBoot')}</button><button class="btn sec" id="rb-sys" ${d ? '' : 'disabled'}>${t('rebootSys')}</button></div></div>`;
  }

  function pageFlash() {
    const fb = S.device && S.device.mode === 'fastboot', rec = S.device && (S.device.mode === 'recovery' || S.device.mode === 'android');
    const ii = S.ipswInfo;
    const looks = ['sf', 'google', 'classic'].map((l) => `<button data-look="${l}" class="${S.look === l ? 'on' : ''}">${t('look' + l[0].toUpperCase() + l.slice(1))}</button>`).join('');
    const win = window.fi.platform === 'win32';
    return `<h1>${t('tabFlash')}</h1><p class="sub">${t('uefiSub')}</p>
      <div class="group-title">${t('uefiTitle')}</div>
      <div class="group"><div class="seg">${looks}</div>
        <div class="row"><div class="txt"><small>${t('lookNote')}</small></div></div>
        <div class="row tap" id="pick-img">${ico('#5e5ce6', P.file)}<div class="txt"><b>${t('chooseImage')}</b><small>${S.image ? S.image.split(/[\\/]/).pop() : '—'}</small></div></div>
        <div class="btns"><button class="btn sec" id="do-backup" ${rec ? '' : 'disabled'}>${t('backup')}</button>
          <button class="btn" id="do-uefi" ${fb && S.image ? '' : 'disabled'}>${t('flashUefi')}</button></div></div>
      <div class="group-title">${t('iosTitle')}</div>
      <div class="group"><div class="row"><div class="txt"><small>${t('iosSub')}</small></div></div>
        <div class="row tap" id="pick-ipsw">${ico('#0a84ff', P.file)}<div class="txt"><b>${t('chooseIpsw')}</b><small>${S.ipsw ? S.ipsw.split(/[\\/]/).pop() : '—'}</small></div>
          ${ii ? `<span class="badge ${ii.ok && ii.supported ? 'ok' : 'bad'}">${ii.ok ? (ii.supported ? t('ipswOk') : t('ipswBad')) : t('ipswUnreadable')}</span>` : ''}</div>
        ${ii && ii.ok ? `<div class="row"><div class="txt"><b>iOS ${ii.version} (${ii.build})</b><small>${ii.types.join(', ')}</small></div></div>` : ''}
        ${win ? `<div class="row"><div class="txt"><small>${t('winNote')}</small></div></div>` : `<div class="btns"><button class="btn sec" id="do-build" ${S.ipsw ? '' : 'disabled'}>${t('build')}</button></div>
        <div class="row" id="cmdrow" style="display:none"><div class="txt"><small>${t('buildHint')}</small><code id="cmd" style="user-select:text;word-break:break-all"></code></div></div>`}
        <div class="row tap" id="pick-simg">${ico('#ff9f0a', P.file)}<div class="txt"><b>${t('chooseSimg')}</b><small>${S.simg ? S.simg.split(/[\\/]/).pop() : '—'}</small></div></div>
        <div class="btns"><button class="btn danger" id="do-ios" ${fb && S.simg ? '' : 'disabled'}>${t('flashIos')}</button></div></div>
      <div class="group-title">${t('logTitle')}</div><div class="group"><div class="bar"><i style="width:${S.progress}%"></i></div><div style="padding:10px 14px"><div class="log" id="log">${S.log.join('\n')}</div></div></div>`;
  }

  function pageLearn() {
    const steps = t('steps'), i = S.step, st = steps[i];
    return `<h1>${t('learnTitle')}</h1><p class="sub">${t('learnSub')}</p><div class="hero"><canvas id="mit" width="352" height="368"></canvas></div>
      <div class="bubble"><b>${st[0]}</b>${st[1]}</div><div class="dots">${steps.map((_, k) => `<i class="${k === i ? 'on' : ''}"></i>`).join('')}</div>
      <div class="group"><div class="btns"><button class="btn" id="st-next">${i === steps.length - 1 ? t('finish') : t('next')}</button><button class="btn sec" id="st-back" ${i === 0 ? 'disabled' : ''}>${t('back')}</button></div></div>`;
  }

  function pageSettings() {
    const langs = [['en', 'English'], ['ru', 'Русский'], ['ua', 'Українська']].map(([c, n]) => `<button data-lang="${c}" class="${S.lang === c ? 'on' : ''}">${n}</button>`).join('');
    const th = ['auto', 'light', 'dark'].map((c) => `<button data-theme="${c}" class="${S.theme === c ? 'on' : ''}">${t(c)}</button>`).join('');
    return `<h1>${t('settingsTitle')}</h1><p class="sub"> </p>
      <div class="group-title">${t('language')}</div><div class="group"><div class="seg">${langs}</div></div>
      <div class="group-title">${t('appearance')}</div><div class="group"><div class="seg">${th}</div></div>
      <div class="group"><div class="row">${ico('#000', P.pill)}<div class="txt"><b>${t('island')}</b><small>${t('islandSub')}</small></div><div class="switch ${S.island ? 'on' : ''}" id="sw-island"></div></div></div>
      <div class="group-title">${t('about')}</div>
      <div class="group"><div style="text-align:center;padding:14px">${logo('logo')}<b>FiMiatoll</b><div class="sub" style="margin:2px 0 0">Flash-iMiatoll · ${t('version')} 1.0.0</div><div class="sub" style="margin-top:8px">${t('aboutText')}</div></div></div>`;
  }

  function render() {
    document.documentElement.lang = S.lang === 'ua' ? 'uk' : S.lang;
    if (S.theme === 'auto') document.documentElement.removeAttribute('data-theme'); else document.documentElement.dataset.theme = S.theme;
    const v = $('#view');
    const keep = v.scrollTop;
    v.innerHTML = S.tab === 'home' ? pageHome() : S.tab === 'flash' ? pageFlash() : S.tab === 'learn' ? pageLearn() : pageSettings();
    v.scrollTop = keep;
    document.querySelectorAll('[data-i]').forEach((e) => { e.textContent = t(e.dataset.i); });
    document.querySelectorAll('#tabbar button').forEach((b) => b.classList.toggle('on', b.dataset.tab === S.tab));
    bind();
  }

  function bind() {
    document.querySelectorAll('[data-go]').forEach((b) => { b.onclick = () => { S.tab = b.dataset.go; render(); }; });
    document.querySelectorAll('[data-lang]').forEach((b) => { b.onclick = () => { S.lang = b.dataset.lang; store.set('lang', S.lang); render(); }; });
    document.querySelectorAll('[data-theme]').forEach((b) => { b.onclick = () => { S.theme = b.dataset.theme; store.set('theme', S.theme); render(); }; });
    document.querySelectorAll('[data-look]').forEach((b) => { b.onclick = async () => { S.look = b.dataset.look; store.set('look', S.look); S.image = await window.fi.bundled(S.look); render(); }; });
    const on = (id, fn) => { const e = $(id); if (e) e.onclick = fn; };
    on('#sw-island', () => { S.island = !S.island; store.set('island', S.island); applyIsland(); if (S.island) island('FiMiatoll', t('island'), {}); render(); });
    on('#rb-bl', () => window.fi.reboot('bootloader'));
    on('#rb-sys', () => window.fi.reboot('system'));
    on('#pick-img', async () => { const f = await window.fi.pick('img'); if (f) { S.image = f; render(); } });
    on('#pick-ipsw', async () => { const f = await window.fi.pick('ipsw'); if (f) { S.ipsw = f; S.ipswInfo = await window.fi.inspectIpsw(f); render(); } });
    on('#pick-simg', async () => { const f = await window.fi.pick('simg'); if (f) { S.simg = f; render(); } });
    on('#do-build', async () => {
      const r = await window.fi.buildCommand(S.ipsw, ''); if (!r.ok) return;
      $('#cmdrow').style.display = ''; $('#cmd').textContent = r.cmd;
      const b = document.createElement('button'); b.className = 'btn sec'; b.textContent = t('openTerm'); b.style.marginTop = '8px';
      b.onclick = () => window.fi.openTerminal(r.cmd.replace('<ios-miatoll>', '$HOME/ios-miatoll')); $('#cmdrow .txt').appendChild(b);
    });
    on('#do-backup', async () => {
      if (!S.device || !['recovery', 'android'].includes(S.device.mode)) { addLog(t('needRecovery')); return; }
      const file = 'boot-backup-' + new Date().toISOString().slice(0, 10) + '.img';
      run(t('backingUp'), async () => { const f = await window.fi.pick('img').then(() => null); return window.fi.backupBoot(file); });
    });
    on('#do-uefi', async () => {
      if (!(await confirmBox('FLASH', t('uefiTitle') + ' → boot'))) return;
      run(t('flashing'), () => window.fi.flash('boot', S.image));
    });
    on('#do-ios', async () => {
      if (!(await confirmBox('ERASE', t('iosSub')))) return;
      run(t('flashing'), () => window.fi.flash('userdata', S.simg));
    });
    on('#st-next', () => { const n = t('steps').length; if (S.step < n - 1) S.step++; else { S.step = 0; S.tab = 'home'; } render(); });
    on('#st-back', () => { if (S.step > 0) S.step--; render(); });
    const mit = $('#mit'); if (mit) window.Mitlk.animate(mit, {});
  }

  // ---------- startup ----------
  document.querySelectorAll('#tabbar button').forEach((b) => { b.onclick = () => { S.tab = b.dataset.tab; render(); }; });
  window.fi.onLog(addLog);
  window.fi.onProgress((p) => { S.progress = p; const bar = document.querySelector('.bar i'); if (bar) bar.style.width = p + '%'; const f = $('#island-fill'); if (f) f.style.width = p + '%'; });
  window.Mitlk.draw($('#island-mascot'), { bare: true });
  S.tab = new URLSearchParams(location.search).get('tab') || S.tab;
  applyIsland(); render();
  window.fi.bundled(S.look).then((p) => { S.image = p; render(); });

  async function poll() {
    try {
      S.tools = await window.fi.tools();
      const list = await window.fi.devices();
      const d = list[0] || null, was = S.device;
      S.device = d;
      if (d && (!was || was.id !== d.id || was.mode !== d.mode)) { island(t('islandConnected'), `Redmi Note 9S · ${modeText(d.mode)}`); if (S.tab === 'home' && !S.busy) render(); }
      if (!d && was) { island(t('islandGone'), ''); if (S.tab === 'home' && !S.busy) render(); }
    } catch (e) { /* no tools yet */ }
    setTimeout(poll, 2500);
  }
  poll();
  window.__fi = { S, render, island };
})();
