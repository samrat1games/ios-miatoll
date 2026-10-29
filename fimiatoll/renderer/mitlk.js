// Mitlk, the mascot: a dot-matrix picture (green LEDs on a dark board). Same shape as shim/menu.c (mit_px).
(function () {
  const W = 44, H = 46;
  function inEll(x, y, cx2, cy2, rx2, ry2) {
    const dx = 2 * x + 1 - cx2, dy = 2 * y + 1 - cy2;
    return dx * dx * ry2 * ry2 + dy * dy * rx2 * rx2 <= rx2 * rx2 * ry2 * ry2;
  }
  function nearSeg(x, y, x0, y0, x1, y1) {
    const px = 2 * x + 1, py = 2 * y + 1, ax = 2 * x0, ay = 2 * y0, bx = 2 * x1, by = 2 * y1;
    const vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    let t = vx * wx + vy * wy; const l = vx * vx + vy * vy;
    t = Math.max(0, Math.min(l, t));
    const qx = ax + Math.trunc(vx * t / l), qy = ay + Math.trunc(vy * t / l);
    return (px - qx) ** 2 + (py - qy) ** 2 <= 5;
  }
  // 0 off, 1 body, 2 dark (eyes, mouth), 3 shine
  function px(x, y, blink) {
    if ((x === 17 && y === 13) || (x === 26 && y === 13)) return blink ? 2 : 3;
    if ((x >= 16 && x <= 18 && y >= 13 && y <= 17) || (x >= 25 && x <= 27 && y >= 13 && y <= 17)) return blink && y < 16 ? 1 : 2;
    if ((y === 20 && x >= 20 && x <= 23) || (y === 19 && (x === 19 || x === 24))) return 2;
    if (inEll(x, y, 44, 31, 26, 24)) return 1;
    if (nearSeg(x, y, 14, 8, 10, 1) || nearSeg(x, y, 30, 8, 34, 1)) return 1;
    if (x >= 13 && x <= 30 && y >= 27 && y <= 40) return 1;
    if (inEll(x, y, 44, 78, 18, 12)) return 1;
    if (((x >= 8 && x <= 11) || (x >= 32 && x <= 35)) && y >= 28 && y <= 38) return 1;
    return 0;
  }
  const SHADE = ['#9bffc4', '#3ddc84', '#2ab56a', '#1d8a50'];
  function draw(canvas, opt) {
    opt = opt || {};
    const cell = Math.floor(canvas.width / W), ctx = canvas.getContext('2d');
    const ox = Math.floor((canvas.width - W * cell) / 2);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (!opt.bare) { ctx.fillStyle = '#10171a'; ctx.fillRect(0, 0, canvas.width, canvas.height); }
    for (let gy = 0; gy < H; gy++) for (let gx = 0; gx < W; gx++) {
      const v = px(gx, gy, opt.blink), t = (gx + gy) * 100 / (W + H);
      if (v === 0 && opt.bare) continue;
      ctx.fillStyle = v === 0 ? '#1a2428' : v === 2 ? '#0a0f11' : v === 3 ? '#d6ffe6' : SHADE[t < 30 ? 0 : t < 52 ? 1 : t < 74 ? 2 : 3];
      ctx.beginPath(); ctx.arc(ox + gx * cell + cell / 2, gy * cell + cell / 2, v === 0 ? cell * .16 : cell * .46, 0, 6.2832); ctx.fill();
    }
  }
  // blinks now and then
  function animate(canvas, opt) {
    draw(canvas, opt);
    setInterval(() => { draw(canvas, Object.assign({}, opt, { blink: true })); setTimeout(() => draw(canvas, opt), 140); }, 3600 + Math.random() * 1500);
  }
  window.Mitlk = { draw, animate };
})();
