// The flying 3D Mitlk: six dot-matrix layers stacked in depth, turning back and forth, flying to the control a hint is about.
(function () {
  const W = 132, H = 138, LAYERS = 7;
  let el, x = 0, y = 0;
  function build() {
    el = document.createElement('div'); el.id = 'pet'; el.className = 'pet';
    const rot = document.createElement('div'); rot.className = 'rot';
    for (let i = 0; i < LAYERS; i++) {
      const c = document.createElement('canvas'); c.width = W; c.height = H;
      const z = (i - (LAYERS - 1)) * 3.2 + 9;   // back layers first, the face on top
      c.style.transform = `translateZ(${z}px)`;
      window.Mitlk.draw(c, { bare: true });
      if (i < LAYERS - 1) {                      // the layers behind are only a darker body
        const g = c.getContext('2d'); g.globalCompositeOperation = 'source-atop'; g.fillStyle = `rgba(4,20,12,${0.78 - i * 0.08})`; g.fillRect(0, 0, W, H);
      }
      rot.appendChild(c);
    }
    el.appendChild(rot); document.body.appendChild(el);
    place(innerWidth - W - 24, innerHeight - 220, true);
  }
  function place(nx, ny, instant) {
    x = Math.max(8, Math.min(innerWidth - W - 8, nx)); y = Math.max(8, Math.min(innerHeight - H - 90, ny));
    if (instant) el.style.transition = 'none';
    el.style.transform = `translate(${x}px, ${y}px)`;
    if (instant) { void el.offsetWidth; el.style.transition = ''; }
  }
  // fly to a control: beside it when there is room on the left, else above it
  function flyTo(target) {
    if (!el) build();
    el.classList.remove('hidden');
    if (!target) { place(innerWidth / 2 - W / 2, 150); return; }
    const r = target.getBoundingClientRect();
    const left = r.left - W - 6, top = r.top + r.height / 2 - H / 2;
    if (left > 4) place(left, top); else place(r.left + r.width / 2 - W / 2, r.top - H - 4);
  }
  function hide() { if (el) el.classList.add('hidden'); }
  function show() { if (!el) build(); el.classList.remove('hidden'); }
  window.Pet = { flyTo, hide, show };
  addEventListener('resize', () => { if (el) place(x, y, true); });
})();
