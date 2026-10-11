const $ = id => document.getElementById(id);

// ---------- state ----------
const state = {
  monitors: [],
  screenW: 0,
  screenH: 0,
  zones: [],        // [{x,y,w,h}] в пикселях монитора, порядок = LED-индексы
  colors: null,     // [r,g,b, r,g,b, ...] в том же порядке
  hover: -1,
  zoneRects: [],    // экранные прямоугольники для hit-test
  running: false,
};

let frameCount = 0;
let lastFpsT = performance.now();

// ---------- params ----------
function getParams() {
  return {
    monitor:     +$('monitor').value | 0,
    top:         +$('top').value    | 0,
    right:       +$('right').value  | 0,
    bottom:      +$('bottom').value | 0,
    left:        +$('left').value   | 0,
    thickness:   +$('thickness').value / 100,
    offset:      +$('offset').value | 0,
    cornerGap:   +$('cornerGap').value / 100,
    startCorner: $('startCorner').value,
    direction:   $('direction').value,
  };
}

// ---------- JS port of C++ buildZones ----------
function buildZonesJs(W, H, p) {
  const cw = [];
  if (W < 4 || H < 4) return cw;

  const minDim = Math.min(W, H);

  let strip = Math.max(1, Math.round(p.thickness * minDim));
  strip = Math.min(strip, Math.floor(minDim / 3));

  let off = Math.max(0, p.offset | 0);
  const maxOff = Math.max(0, Math.floor(minDim / 2) - strip);
  off = Math.min(off, maxOff);

  const range = (total, n, i) => [
    Math.floor(total * i / n),
    Math.floor(total * (i + 1) / n),
  ];

  if (p.top > 0) {
    let gapPx = Math.round(p.cornerGap * W);
    gapPx = Math.min(gapPx, Math.max(0, Math.floor(W / 2) - 1));
    const span = W - 2 * gapPx;
    const y = off;
    const h = Math.max(0, Math.min(strip, H - y));
    if (h > 0 && span > 0)
      for (let i = 0; i < p.top; i++) {
        const [a, b] = range(span, p.top, i);
        cw.push({ x: gapPx + a, y, w: b - a, h });
      }
  }
  if (p.right > 0) {
    let gapPx = Math.round(p.cornerGap * H);
    gapPx = Math.min(gapPx, Math.max(0, Math.floor(H / 2) - 1));
    const span = H - 2 * gapPx;
    const x = (off + strip < W) ? (W - off - strip) : 0;
    const w = Math.min(strip, W - x);
    if (w > 0 && span > 0)
      for (let i = 0; i < p.right; i++) {
        const [a, b] = range(span, p.right, i);
        cw.push({ x, y: gapPx + a, w, h: b - a });
      }
  }
  if (p.bottom > 0) {
    let gapPx = Math.round(p.cornerGap * W);
    gapPx = Math.min(gapPx, Math.max(0, Math.floor(W / 2) - 1));
    const span = W - 2 * gapPx;
    const y = (off + strip < H) ? (H - off - strip) : 0;
    const h = Math.min(strip, H - y);
    if (h > 0 && span > 0)
      for (let i = 0; i < p.bottom; i++) {
        const [a, b] = range(span, p.bottom, i);
        cw.push({ x: W - gapPx - b, y, w: b - a, h });
      }
  }
  if (p.left > 0) {
    let gapPx = Math.round(p.cornerGap * H);
    gapPx = Math.min(gapPx, Math.max(0, Math.floor(H / 2) - 1));
    const span = H - 2 * gapPx;
    const x = off;
    const w = Math.min(strip, W - x);
    if (w > 0 && span > 0)
      for (let i = 0; i < p.left; i++) {
        const [a, b] = range(span, p.left, i);
        cw.push({ x, y: H - gapPx - b, w, h: b - a });
      }
  }

  const n = cw.length;
  if (n === 0) return cw;

  let rot = 0;
  if      (p.startCorner === 'top-right')    rot = p.top;
  else if (p.startCorner === 'bottom-right') rot = p.top + p.right;
  else if (p.startCorner === 'bottom-left')  rot = p.top + p.right + p.bottom;
  rot = ((rot % n) + n) % n;

  const result = [];
  for (let i = 0; i < n; i++) result.push(cw[(rot + i) % n]);

  if (p.direction === 'ccw') result.reverse();
  return result;
}

// ---------- recompute layout from inputs ----------
function recomputeLayout() {
  const p = getParams();
  const m = state.monitors.find(x => x.index === p.monitor);
  if (!m) { state.zones = []; state.screenW = state.screenH = 0; return; }
  state.screenW = m.w;
  state.screenH = m.h;
  state.zones = buildZonesJs(m.w, m.h, p);

  $('thicknessVal').textContent = $('thickness').value + '%';
  $('cornerGapVal').textContent = $('cornerGap').value + '%';
  $('total').textContent = state.zones.length + ' LEDs';
}

['monitor', 'top', 'right', 'bottom', 'left',
 'thickness', 'offset', 'cornerGap', 'startCorner', 'direction']
  .forEach(id => $(id).addEventListener('input', recomputeLayout));

// ---------- canvases ----------
const view  = $('view');
const vctx  = view.getContext('2d');
const strip = $('strip');
const sctx  = strip.getContext('2d');

function fitCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const r = canvas.getBoundingClientRect();
  canvas.width  = Math.max(1, Math.round(r.width  * dpr));
  canvas.height = Math.max(1, Math.round(r.height * dpr));
  canvas.getContext('2d').setTransform(dpr, 0, 0, dpr, 0, 0);
}

function resizeAll() { fitCanvas(view); fitCanvas(strip); }
window.addEventListener('resize', resizeAll);

// ---------- draw perimeter — зоны ВНУТРИ экрана в реальном масштабе ----------
function drawView() {
  const r = view.getBoundingClientRect();
  const W = r.width, H = r.height;
  vctx.clearRect(0, 0, W, H);

  const zones = state.zones;
  const n = zones.length;
  if (n === 0 || !state.screenW || !state.screenH) return;

  // Проекция экрана монитора в канвас, сохраняя aspect ratio
  const aspect = state.screenW / state.screenH;
  const pad = 30;
  let sw = W - pad * 2;
  let sh = sw / aspect;
  if (sh > H - pad * 2) { sh = H - pad * 2; sw = sh * aspect; }
  const sx = (W - sw) / 2;
  const sy = (H - sh) / 2;
  const scale = sw / state.screenW;   // == sh / state.screenH

  // Ambient glow позади экрана — средний цвет всех LED
  if (state.colors && state.colors.length >= 3) {
    let ar = 0, ag = 0, ab = 0;
    const cnt = state.colors.length / 3;
    for (let i = 0; i < cnt; i++) {
      ar += state.colors[i*3]; ag += state.colors[i*3+1]; ab += state.colors[i*3+2];
    }
    ar = (ar / cnt) | 0; ag = (ag / cnt) | 0; ab = (ab / cnt) | 0;
    const cx = sx + sw / 2, cy = sy + sh / 2;
    const rad = Math.max(sw, sh) * 0.85;
    const grd = vctx.createRadialGradient(cx, cy, Math.min(sw, sh) * 0.4, cx, cy, rad);
    grd.addColorStop(0, `rgba(${ar},${ag},${ab},0.35)`);
    grd.addColorStop(1, `rgba(${ar},${ag},${ab},0)`);
    vctx.fillStyle = grd;
    vctx.fillRect(0, 0, W, H);
  }

  // Прямоугольник экрана — фон + клип
  vctx.save();
  vctx.beginPath();
  vctx.roundRect(sx, sy, sw, sh, 4);
  vctx.fillStyle = '#050608';
  vctx.fill();
  vctx.clip();

  // Тонкая сетка внутри — помогает оценить реальные пропорции
  vctx.strokeStyle = 'rgba(255,255,255,0.022)';
  vctx.lineWidth = 1;
  vctx.beginPath();
  for (let i = 1; i < 16; i++) {
    const x = sx + sw * i / 16;
    vctx.moveTo(x, sy); vctx.lineTo(x, sy + sh);
  }
  for (let i = 1; i < 10; i++) {
    const y = sy + sh * i / 10;
    vctx.moveTo(sx, y); vctx.lineTo(sx + sw, y);
  }
  vctx.stroke();

  // Разрешение по центру — под зонами (не мешает, если зоны на краях)
  vctx.fillStyle = 'rgba(255,255,255,0.10)';
  vctx.font = '11px ui-monospace, Consolas, monospace';
  vctx.textAlign = 'center';
  vctx.textBaseline = 'middle';
  vctx.fillText(`${state.screenW} × ${state.screenH}`, sx + sw / 2, sy + sh / 2);

  // LED-зоны в реальном масштабе
  state.zoneRects = [];
  for (let i = 0; i < n; i++) {
    const z = zones[i];
    const zx = sx + z.x * scale;
    const zy = sy + z.y * scale;
    const zw = Math.max(1, z.w * scale);
    const zh = Math.max(1, z.h * scale);
    const cx = zx + zw / 2;
    const cy = zy + zh / 2;

    state.zoneRects.push({ x0: zx, y0: zy, x1: zx + zw, y1: zy + zh, cx, cy, w: zw, h: zh });

    let cr = 26, cg = 26, cb = 28;
    if (state.colors && state.colors.length >= (i + 1) * 3) {
      cr = state.colors[i*3]; cg = state.colors[i*3+1]; cb = state.colors[i*3+2];
    }

    // Мягкое свечение наружу от зоны (рисуется до прямоугольника — под ним)
    if (state.colors) {
      const glowR = Math.max(zw, zh) * 2.2;
      const gg = vctx.createRadialGradient(cx, cy, 0, cx, cy, glowR);
      gg.addColorStop(0, `rgba(${cr},${cg},${cb},0.45)`);
      gg.addColorStop(1, `rgba(${cr},${cg},${cb},0)`);
      vctx.fillStyle = gg;
      vctx.beginPath();
      vctx.arc(cx, cy, glowR, 0, Math.PI * 2);
      vctx.fill();
    }

    // Сама зона — прямоугольник реального размера
    vctx.fillStyle = `rgb(${cr},${cg},${cb})`;
    vctx.fillRect(zx, zy, zw, zh);

    // Тонкая окантовка, чтобы соседние зоны не сливались
    vctx.strokeStyle = 'rgba(255,255,255,0.10)';
    vctx.lineWidth = 1;
    vctx.strokeRect(zx + 0.5, zy + 0.5, Math.max(0, zw - 1), Math.max(0, zh - 1));

    // Hover-рамка
    if (i === state.hover) {
      vctx.strokeStyle = '#fff';
      vctx.lineWidth = 2;
      vctx.strokeRect(zx + 1, zy + 1, Math.max(0, zw - 2), Math.max(0, zh - 2));
    }
  }

  // Маркер LED 0 — белая точка и подпись в центре зоны
  if (state.zoneRects.length > 0) {
    const p0 = state.zoneRects[0];
    const rr = Math.max(3, Math.min(6, Math.min(p0.w, p0.h) * 0.35));
    vctx.fillStyle = '#fff';
    vctx.beginPath();
    vctx.arc(p0.cx, p0.cy, rr, 0, Math.PI * 2);
    vctx.fill();

    vctx.font = 'bold 10px ui-monospace, monospace';
    vctx.textAlign = 'center';
    vctx.textBaseline = 'middle';
    // Подпись сдвигаем внутрь экрана, чтобы не вылезала за пределы
    const dx = (p0.cx < sx + sw / 2) ? 14 : -14;
    const dy = (p0.cy < sy + sh / 2) ? 14 : -14;
    vctx.fillStyle = 'rgba(0,0,0,0.6)';
    vctx.fillRect(p0.cx + dx - 6, p0.cy + dy - 7, 12, 14);
    vctx.fillStyle = '#fff';
    vctx.fillText('0', p0.cx + dx, p0.cy + dy);
  }

  vctx.restore();

  // Рамка экрана поверх зон — чтобы край был чётким
  vctx.strokeStyle = '#2a2f38';
  vctx.lineWidth = 1.5;
  vctx.beginPath();
  vctx.roundRect(sx, sy, sw, sh, 4);
  vctx.stroke();
}

// ---------- linear strip ----------
function drawStrip() {
  const r = strip.getBoundingClientRect();
  const W = r.width, H = r.height;
  sctx.clearRect(0, 0, W, H);

  const n = state.zones.length;
  if (n === 0) return;

  const w = W / n;
  for (let i = 0; i < n; i++) {
    let cr = 22, cg = 22, cb = 24;
    if (state.colors && state.colors.length >= (i + 1) * 3) {
      cr = state.colors[i*3]; cg = state.colors[i*3+1]; cb = state.colors[i*3+2];
    }
    sctx.fillStyle = `rgb(${cr},${cg},${cb})`;
    sctx.fillRect(i * w, 0, Math.ceil(w) + 1, H);

    if (i === state.hover) {
      sctx.fillStyle = 'rgba(255,255,255,0.5)';
      sctx.fillRect(i * w, 0, Math.max(1, w), 2);
      sctx.fillRect(i * w, H - 2, Math.max(1, w), 2);
    }
  }

  const step = Math.max(1, Math.round(n / 24));
  sctx.fillStyle = 'rgba(255,255,255,0.35)';
  sctx.font = '9px ui-monospace, monospace';
  sctx.textBaseline = 'bottom';
  sctx.textAlign = 'center';
  for (let i = 0; i < n; i += step) {
    sctx.fillText(String(i), i * w + w / 2, H - 3);
  }
}

// ---------- render loop ----------
function loop() {
  const now = performance.now();
  if (now - lastFpsT > 500) {
    const fps = frameCount / ((now - lastFpsT) / 1000);
    $('fps').textContent = fps > 0.05 ? fps.toFixed(0) + ' fps' : '—';
    frameCount = 0;
    lastFpsT = now;
  }
  drawView();
  drawStrip();
  requestAnimationFrame(loop);
}

// ---------- hover по прямоугольникам зон ----------
view.addEventListener('mousemove', e => {
  const r = view.getBoundingClientRect();
  const mx = e.clientX - r.left;
  const my = e.clientY - r.top;

  let found = -1;
  for (let i = 0; i < state.zoneRects.length; i++) {
    const z = state.zoneRects[i];
    if (mx >= z.x0 && mx <= z.x1 && my >= z.y0 && my <= z.y1) { found = i; break; }
  }
  state.hover = found;

  const tip = $('tooltip');
  if (found < 0) {
    tip.classList.add('hidden');
  } else {
    const z = state.zones[found];
    const cr = state.colors ? state.colors[found*3]   : 0;
    const cg = state.colors ? state.colors[found*3+1] : 0;
    const cb = state.colors ? state.colors[found*3+2] : 0;
    tip.textContent = `#${found}  ${z.x},${z.y} ${z.w}×${z.h}  rgb(${cr}, ${cg}, ${cb})`;
    tip.style.left = state.zoneRects[found].cx + 'px';
    tip.style.top  = state.zoneRects[found].cy + 'px';
    tip.classList.remove('hidden');
  }
});
view.addEventListener('mouseleave', () => {
  state.hover = -1;
  $('tooltip').classList.add('hidden');
});

// ---------- IPC ----------
function log(msg, cls = '') {
  const el = document.createElement('div');
  if (cls) el.className = cls;
  el.textContent = msg;
  $('log').appendChild(el);
  $('log').scrollTop = $('log').scrollHeight;
  while ($('log').children.length > 500) $('log').firstChild.remove();
}

function setRunning(running) {
  state.running = running;
  $('start').disabled = running;
  $('stop').disabled  = !running;
  ['top', 'right', 'bottom', 'left', 'monitor',
   'thickness', 'offset', 'cornerGap', 'startCorner', 'direction']
    .forEach(id => $(id).disabled = running);
}

window.api.on('monitors', list => {
  state.monitors = list;
  const sel = $('monitor');
  sel.innerHTML = '';
  for (const m of list) {
    const o = document.createElement('option');
    o.value = m.index;
    o.textContent = `#${m.index} · ${m.w}×${m.h}${m.primary ? '  (primary)' : ''}  ${m.name}`;
    sel.appendChild(o);
  }
  recomputeLayout();
});

window.api.on('status', s => {
  if (s.running) {
    setRunning(true);
    if (s.w && s.h) { state.screenW = s.w; state.screenH = s.h; }
    if (s.zones && s.zones.length) state.zones = s.zones;
    log(`started · ${s.w}×${s.h} · ${state.zones.length} LEDs`, 'ok');
  } else {
    setRunning(false);
    if (s.exited) log(`capture exited (code ${s.code})`, 'err');
  }
});

window.api.on('frame', f => {
  state.colors = f.leds;
  frameCount++;
});

window.api.on('log', msg => log(msg));

// ---------- buttons ----------
$('start').onclick = () => {
  const p = getParams();
  if (p.top + p.right + p.bottom + p.left === 0) {
    log('configure at least one LED', 'err');
    return;
  }
  window.api.send({ cmd: 'start', ...p });
};
$('stop').onclick = () => window.api.send({ cmd: 'stop' });

// ---------- init ----------
resizeAll();
recomputeLayout();
requestAnimationFrame(loop);