// MapForge frontend — generate / visualize (3D + 2D) / edit / export strafegen maps.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

THREE.Object3D.DEFAULT_UP.set(0, 0, 1);   // Quake is Z-up

// ----------------------------------------------------------------- state
const S = {
  meta: null,
  base: null,            // pristine scene from server (edits=[]); ids reference this
  entMods: new Map(),    // base entity id -> {origin?, keys?, deleted?}
  brushMods: new Map(),  // base brush id  -> {aabb?, deleted?}
  added: [],             // {localId, type:'ent'|'box', ...}
  addSeq: 0,
  sel: null,             // {kind:'entity'|'brush', id} | {added:localId}
  placing: null,         // classname to place, or '__box__'
  view: '3d',
  layers: { geometry: true, entities: true, flows: true, triggers: false,
            enclosure: false, edges: true },
};

const ENT_COLOR = {
  info_player_deathmatch: '#8fff8f', info_player_start: '#8fff8f',
  item_quad: '#cf57f0', item_health_mega: '#ff7777', item_health: '#ff9b9b',
  item_armor: '#7fb0ff', misc_teleporter_dest: '#54dcf0', target_position: '#c0c0c0',
};
const entColor = cn => cn.startsWith('weapon_') ? '#ffd27f' : (ENT_COLOR[cn] || '#dddddd');

// ----------------------------------------------------------------- api
const api = {
  meta: () => fetch('/api/meta').then(r => r.json()),
  generate: (body) => fetch('/api/generate', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(async r => { const j = await r.json(); if (!r.ok) throw new Error(j.error || r.status); return j; }),
  export: (body) => fetch('/api/export', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(async r => { const j = await r.json(); if (!r.ok) throw new Error(j.error || r.status); return j; }),
  parts: () => fetch('/api/parts').then(r => r.json()),
  maps: () => fetch('/api/maps').then(r => r.json()),
  analyze: (body) => fetch('/api/analyze', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(async r => { const j = await r.json(); if (!r.ok) throw new Error(j.error || r.status); return j; }),
  calibrate: (body) => fetch('/api/calibrate', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(async r => { const j = await r.json(); if (!r.ok) throw new Error(j.error || r.status); return j; }),
  importBsp: (body) => fetch('/api/import_bsp', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(async r => { const j = await r.json(); if (!r.ok) throw new Error(j.error || r.status); return j; }),
  composeExport: (body) => fetch('/api/compose_export', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then(async r => { const j = await r.json(); if (!r.ok) throw new Error(j.error || r.status); return j; }),
};

const $ = id => document.getElementById(id);
function toast(msg, err = false) {
  const t = $('toast'); t.textContent = msg; t.className = 'toast' + (err ? ' err' : '');
  t.style.display = 'block'; clearTimeout(toast._t);
  toast._t = setTimeout(() => t.style.display = 'none', err ? 6000 : 3000);
}

// ----------------------------------------------------------------- params
function gatherParams() {
  const kind = $('kind').value;
  const p = {
    seed: parseInt($('seed').value) || 0,
    difficulty: parseInt($('difficulty').value),
    length: parseInt($('length').value) || 1,
    archetype: $('archetype').value || null,
    theme: $('theme').value,
    vscale: parseFloat($('vscale').value),
    hscale: parseFloat($('hscale').value),
    density: parseFloat($('density').value),
    void: $('void').checked,
  };
  const secs = [...document.querySelectorAll('#sections input:checked')].map(c => c.value);
  const all = [...document.querySelectorAll('#sections input')].length;
  if (secs.length && secs.length < all) p.sections = secs;
  return { kind, params: p };
}

// ----------------------------------------------------------------- edit model
// Effective scene = base transformed by mods + added. Both views consume this.
function effective() {
  const b = S.base;
  const brushes = [];
  b.brushes.forEach(br => {
    const m = S.brushMods.get(br.id);
    if (m && m.deleted) return;
    const aabb = (m && m.aabb) ? m.aabb : br.aabb;
    brushes.push({ key: 'b' + br.id, id: br.id, role: br.role, color: br.color,
      aabb, editable: br.aabb_editable,
      faces: (m && m.aabb) ? null : br.faces,    // edited box -> rebuild from aabb
      layer: br.role === 'sky/enclosure' ? 'enclosure' : 'geometry' });
  });
  const entities = [];
  b.entities.forEach(e => {
    if (!e.origin) return;                         // worldspawn etc. (no marker)
    const m = S.entMods.get(e.id);
    if (m && m.deleted) return;
    entities.push({ key: 'e' + e.id, id: e.id, classname: e.classname,
      origin: (m && m.origin) ? m.origin : e.origin });
  });
  S.added.forEach(a => {
    if (a.type === 'box')
      brushes.push({ key: 'a' + a.localId, localId: a.localId, role: a.role,
        color: roleColor(a.role), aabb: a.aabb, editable: true, faces: null,
        layer: 'geometry' });
    else
      entities.push({ key: 'a' + a.localId, localId: a.localId,
        classname: a.classname, origin: a.origin });
  });
  return { brushes, entities, flows: b.flows, triggers: b.triggers, bounds: b.bounds };
}

function roleColor(role) {
  const r = (S.meta.legend || []).find(x => x.role === role);
  return r ? r.color : '#8f8f96';
}

function editCount() {
  let n = S.added.length;
  S.entMods.forEach(m => { if (m.deleted || m.origin || m.keys) n++; });
  S.brushMods.forEach(m => { if (m.deleted || m.aabb) n++; });
  return n;
}

function buildEdits() {
  const edits = [];
  S.entMods.forEach((m, id) => {
    if (m.deleted) { edits.push({ op: 'ent.delete', id }); return; }
    if (m.origin) edits.push({ op: 'ent.move', id, origin: m.origin });
    if (m.keys) for (const [k, v] of Object.entries(m.keys))
      edits.push({ op: 'ent.setkey', id, key: k, value: v });
  });
  S.brushMods.forEach((m, id) => {
    if (m.deleted) { edits.push({ op: 'brush.delete', id }); return; }
    if (m.aabb) edits.push({ op: 'brush.resize', id,
      mins: m.aabb.slice(0, 3), maxs: m.aabb.slice(3) });
  });
  S.added.forEach(a => {
    if (a.type === 'box') edits.push({ op: 'brush.add',
      mins: a.aabb.slice(0, 3), maxs: a.aabb.slice(3), role: a.role });
    else edits.push({ op: 'ent.add', classname: a.classname, origin: a.origin });
  });
  return edits;
}

// ----------------------------------------------------------------- three.js
let renderer, scene3, camera, controls, worldGroup, raycaster, pointer;
const pickables = [];

function initGL() {
  const canvas = $('gl');
  renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  scene3 = new THREE.Scene();
  scene3.background = new THREE.Color('#07070b');
  camera = new THREE.PerspectiveCamera(60, 1, 1, 200000);
  camera.up.set(0, 0, 1);
  controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true; controls.dampingFactor = 0.12;
  scene3.add(new THREE.AmbientLight(0xffffff, 0.55));
  const d1 = new THREE.DirectionalLight(0xfff0d8, 0.9); d1.position.set(1, 0.6, 2);
  const d2 = new THREE.DirectionalLight(0x9fb6ff, 0.35); d2.position.set(-1, -0.8, 0.6);
  scene3.add(d1, d2);
  worldGroup = new THREE.Group(); scene3.add(worldGroup);
  raycaster = new THREE.Raycaster(); pointer = new THREE.Vector2();
  resize();
  const loop = () => { controls.update(); renderer.render(scene3, camera); requestAnimationFrame(loop); };
  loop();
}

function resize() {
  const v = $('view'); const w = v.clientWidth, h = v.clientHeight;
  renderer.setSize(w, h, false);
  camera.aspect = w / h; camera.updateProjectionMatrix();
  draw2d();
}
addEventListener('resize', resize);

function clearGroup(g) {
  while (g.children.length) {
    const c = g.children.pop();
    c.geometry?.dispose?.();
    if (c.material) (Array.isArray(c.material) ? c.material : [c.material]).forEach(m => m.dispose());
    g.remove(c);
  }
}

function geomFromFaces(faces) {
  const pos = [], col = [], nor = [], c = new THREE.Color();
  for (const f of faces) {
    c.set(f.color); const p = f.poly, n = f.normal;
    for (let i = 1; i < p.length - 1; i++)
      for (const idx of [0, i, i + 1]) {
        pos.push(p[idx][0], p[idx][1], p[idx][2]);
        col.push(c.r, c.g, c.b); nor.push(n[0], n[1], n[2]);
      }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  g.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
  return g;
}

function boxFaces(aabb, color) {
  const [x0, y0, z0, x1, y1, z1] = aabb;
  const f = (poly, n) => ({ poly, color, normal: n });
  return [
    f([[x0, y0, z0], [x0, y1, z0], [x0, y1, z1], [x0, y0, z1]], [-1, 0, 0]),
    f([[x1, y0, z0], [x1, y0, z1], [x1, y1, z1], [x1, y1, z0]], [1, 0, 0]),
    f([[x0, y0, z0], [x0, y0, z1], [x1, y0, z1], [x1, y0, z0]], [0, -1, 0]),
    f([[x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]], [0, 1, 0]),
    f([[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0]], [0, 0, -1]),
    f([[x0, y0, z1], [x0, y1, z1], [x1, y1, z1], [x1, y0, z1]], [0, 0, 1]),
  ];
}

function render3d() {
  if (S.mode === 'compose') return renderCompose3d();
  if (!S.base) return;
  clearGroup(worldGroup); pickables.length = 0;
  const eff = effective();

  for (const br of eff.brushes) {
    const faces = br.faces || boxFaces(br.aabb, br.color);
    const geo = geomFromFaces(faces);
    const mat = new THREE.MeshLambertMaterial({ vertexColors: true,
      side: THREE.DoubleSide, transparent: br.layer === 'enclosure', opacity: br.layer === 'enclosure' ? 0.12 : 1 });
    const mesh = new THREE.Mesh(geo, mat);
    mesh.userData = { pick: br.key, layer: br.layer };
    mesh.visible = S.layers[br.layer] && S.layers.geometry || (br.layer === 'enclosure' && S.layers.enclosure);
    worldGroup.add(mesh); pickables.push(mesh);
    if (S.layers.edges && br.layer !== 'enclosure') {
      const edges = new THREE.LineSegments(new THREE.EdgesGeometry(geo, 25),
        new THREE.LineBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.35 }));
      edges.userData = { edgeFor: br.key, layer: br.layer };
      worldGroup.add(edges);
    }
  }

  // entity markers
  const mSize = 18;
  for (const e of eff.entities) {
    const geo = new THREE.OctahedronGeometry(mSize);
    const mat = new THREE.MeshBasicMaterial({ color: entColor(e.classname) });
    const m = new THREE.Mesh(geo, mat);
    m.position.set(e.origin[0], e.origin[1], e.origin[2] + mSize);
    m.userData = { pick: e.key, layer: 'entities' };
    m.visible = S.layers.entities;
    worldGroup.add(m); pickables.push(m);
  }

  // flows
  if (S.layers.flows) for (const fl of eff.flows) {
    const col = fl.kind === 'pad' ? 0xffb26b : 0x54dcf0;
    const z = eff.bounds[2] + 8;
    const g = new THREE.BufferGeometry().setFromPoints([
      new THREE.Vector3(fl.from[0], fl.from[1], z),
      new THREE.Vector3(fl.to[0], fl.to[1], fl.to[2])]);
    worldGroup.add(new THREE.Line(g, new THREE.LineBasicMaterial({ color: col })));
  }

  // triggers (wireframe volumes)
  if (S.layers.triggers) for (const t of eff.triggers) {
    const [x0, y0, z0, x1, y1, z1] = t.aabb;
    const box = new THREE.Box3(new THREE.Vector3(x0, y0, z0), new THREE.Vector3(x1, y1, z1));
    const h = new THREE.Box3Helper(box, 0xf2e85a); worldGroup.add(h);
  }

  // bounds + selection highlight
  const [bx0, by0, bz0, bx1, by1, bz1] = eff.bounds;
  worldGroup.add(new THREE.Box3Helper(
    new THREE.Box3(new THREE.Vector3(bx0, by0, bz0), new THREE.Vector3(bx1, by1, bz1)), 0x333344));
  highlight3d(eff);
  ensureGrid(eff.bounds);
}

let _gridKey = null, _grid = null;
function ensureGrid(bounds) {
  const span = Math.max(bounds[3] - bounds[0], bounds[4] - bounds[1]);
  const key = Math.round(span);
  if (_gridKey === key) { _grid.position.set((bounds[0]+bounds[3])/2,(bounds[1]+bounds[4])/2,bounds[2]); return; }
  if (_grid) scene3.remove(_grid);
  const size = Math.ceil(span / 512 + 4) * 512;
  _grid = new THREE.GridHelper(size, size / 256, 0x303040, 0x202028);
  _grid.rotation.x = Math.PI / 2;   // lay flat in XY (Z-up)
  _grid.position.set((bounds[0]+bounds[3])/2, (bounds[1]+bounds[4])/2, bounds[2]);
  _gridKey = key; scene3.add(_grid);
}

let _selBox = null;
function highlight3d(eff) {
  if (_selBox) { worldGroup.remove(_selBox); _selBox = null; }
  if (!S.sel) return;
  const el = findEff(eff, S.sel);
  if (!el) return;
  let box;
  if (el.aabb) box = new THREE.Box3(new THREE.Vector3(...el.aabb.slice(0, 3)),
    new THREE.Vector3(...el.aabb.slice(3)));
  else { const o = el.origin; box = new THREE.Box3(
    new THREE.Vector3(o[0]-24, o[1]-24, o[2]), new THREE.Vector3(o[0]+24, o[1]+24, o[2]+48)); }
  _selBox = new THREE.Box3Helper(box, 0xffb347); worldGroup.add(_selBox);
}

function findEff(eff, sel) {
  const key = sel.added != null ? 'a' + sel.added : (sel.kind === 'brush' ? 'b' : 'e') + sel.id;
  return [...eff.brushes, ...eff.entities].find(x => x.key === key);
}

function frameCamera() {
  const b = S.base.bounds;
  const cx = (b[0]+b[3])/2, cy = (b[1]+b[4])/2, cz = (b[2]+b[5])/2;
  const r = Math.max(b[3]-b[0], b[4]-b[1], b[5]-b[2]) || 1024;
  camera.position.set(cx + r * 0.9, cy - r * 1.3, cz + r * 0.9);
  controls.target.set(cx, cy, cz); controls.update();
}

// ----------------------------------------------------------------- picking
let downXY = null;
$('gl').addEventListener('pointerdown', e => { if (S.mode !== 'generate') return; downXY = [e.clientX, e.clientY]; });
$('gl').addEventListener('pointerup', e => {
  if (S.mode !== 'generate') return;
  if (!downXY) return;
  const moved = Math.hypot(e.clientX - downXY[0], e.clientY - downXY[1]);
  downXY = null;
  if (moved > 6) return;            // it was an orbit drag
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
  if (S.placing) { placeAt(raycaster); return; }
  const hits = raycaster.intersectObjects(pickables.filter(p => p.visible), false);
  if (hits.length) selectKey(hits[0].object.userData.pick);
  else { S.sel = null; renderInspector(); render3d(); }
});

function placeAt(ray) {
  const hits = ray.intersectObjects(pickables.filter(p => p.visible && p.userData.layer === 'geometry'), false);
  let pt;
  if (hits.length) pt = hits[0].point;
  else {
    const plane = new THREE.Plane(new THREE.Vector3(0, 0, 1), -S.base.bounds[2]);
    pt = new THREE.Vector3(); ray.ray.intersectPlane(plane, pt);
    if (!pt) return;
  }
  doPlace([Math.round(pt.x), Math.round(pt.y), Math.round(pt.z)]);
}

function selectKey(key) {
  if (key[0] === 'a') S.sel = { added: parseInt(key.slice(1)) };
  else S.sel = { kind: key[0] === 'b' ? 'brush' : 'entity', id: parseInt(key.slice(1)) };
  renderInspector(); render3d(); draw2d();
}

// ----------------------------------------------------------------- placement / add
function doPlace(origin) {
  if (S.placing === '__box__') {
    const a = [origin[0]-32, origin[1]-32, origin[2], origin[0]+32, origin[1]+32, origin[2]+64];
    const id = S.addSeq++;
    S.added.push({ localId: id, type: 'box', role: 'structure', aabb: a });
    S.sel = { added: id };
  } else {
    const id = S.addSeq++;
    S.added.push({ localId: id, type: 'ent', classname: S.placing, origin });
    S.sel = { added: id };
  }
  S.placing = null; $('addHint').textContent = '';
  refresh();
}

// ----------------------------------------------------------------- inspector
function renderInspector() {
  const el = $('inspector');
  $('editN').textContent = `(${editCount()})`;
  if (!S.sel) { el.innerHTML = '<div class="muted">Click a brush or entity to select. '
    + 'Drag to orbit, scroll to zoom.</div>'; return; }
  const eff = effective();
  const x = findEff(eff, S.sel);
  if (!x) { S.sel = null; return renderInspector(); }
  const isAdded = S.sel.added != null;
  let h = '';
  if (x.aabb) {
    h += `<div class="kv"><span>brush</span><b>${x.role}</b></div>`;
    h += `<div class="kv"><span>id</span><b>${isAdded ? 'new#' + x.localId : x.id}</b></div>`;
    if (!x.editable && !isAdded) {
      h += '<div class="muted">Non-box brush (ramp/prism) — read-only. Delete is allowed.</div>';
    } else {
      const lbl = ['min X','min Y','min Z','max X','max Y','max Z'];
      h += '<div class="grid3">' + x.aabb.map((v, i) =>
        `<div><label>${lbl[i]}</label><input type="number" step="8" data-aabb="${i}" value="${Math.round(v)}"></div>`).join('') + '</div>';
      if (isAdded) {
        h += '<label>role</label><select id="boxRole">' + S.meta.addRoles.map(r =>
          `<option ${r===x.role?'selected':''}>${r}</option>`).join('') + '</select>';
      }
    }
  } else {
    h += `<div class="kv"><span>entity</span><b>${x.classname}</b></div>`;
    h += `<div class="kv"><span>id</span><b>${isAdded ? 'new#' + x.localId : x.id}</b></div>`;
    const lbl = ['X', 'Y', 'Z'];
    h += '<div class="grid3">' + x.origin.map((v, i) =>
      `<div><label>${lbl[i]}</label><input type="number" step="8" data-org="${i}" value="${Math.round(v)}"></div>`).join('') + '</div>';
  }
  h += `<button class="sm danger" id="del" style="width:100%;margin-top:8px">delete</button>`;
  el.innerHTML = h;

  el.querySelectorAll('[data-aabb]').forEach(inp => inp.onchange = () => {
    const a = [...x.aabb]; a[+inp.dataset.aabb] = parseFloat(inp.value); setBrushAabb(a);
  });
  el.querySelectorAll('[data-org]').forEach(inp => inp.onchange = () => {
    const o = [...x.origin]; o[+inp.dataset.org] = parseFloat(inp.value); setEntOrigin(o);
  });
  const rs = $('boxRole'); if (rs) rs.onchange = () => {
    S.added.find(a => a.localId === S.sel.added).role = rs.value; refresh();
  };
  $('del').onclick = deleteSel;
}

function setBrushAabb(aabb) {
  if (S.sel.added != null) S.added.find(a => a.localId === S.sel.added).aabb = aabb;
  else { const m = S.brushMods.get(S.sel.id) || {}; m.aabb = aabb; S.brushMods.set(S.sel.id, m); }
  refresh();
}
function setEntOrigin(o) {
  if (S.sel.added != null) S.added.find(a => a.localId === S.sel.added).origin = o;
  else { const m = S.entMods.get(S.sel.id) || {}; m.origin = o; S.entMods.set(S.sel.id, m); }
  refresh();
}
function deleteSel() {
  if (S.sel.added != null) S.added = S.added.filter(a => a.localId !== S.sel.added);
  else if (S.sel.kind === 'brush') { const m = S.brushMods.get(S.sel.id) || {}; m.deleted = true; S.brushMods.set(S.sel.id, m); }
  else { const m = S.entMods.get(S.sel.id) || {}; m.deleted = true; S.entMods.set(S.sel.id, m); }
  S.sel = null; refresh();
}

// re-render views from local state (no server round-trip)
function refresh() { render3d(); draw2d(); renderInspector(); }

// ----------------------------------------------------------------- 2D plan + elevation
function draw2d() {
  if (S.mode === 'compose') return drawCompose2d();
  if (!S.base) return;
  const cv = $('view2d'); const v = $('view');
  cv.width = v.clientWidth; cv.height = v.clientHeight;
  const ctx = cv.getContext('2d');
  ctx.fillStyle = '#07070b'; ctx.fillRect(0, 0, cv.width, cv.height);
  const eff = effective();
  const [x0, y0, z0, x1, y1, z1] = eff.bounds;
  const W = x1 - x0, D = y1 - y0, H = z1 - z0 || 1;
  const pad = 30, gap = 24;
  const planH = cv.height * 0.62 - pad * 1.5;
  const s = Math.min((cv.width - pad * 2) / W, planH / D);
  const px = x => pad + (x - x0) * s, py = y => pad + (y1 - y) * s;       // +Y up
  const planBottom = pad + D * s;
  const elevTop = planBottom + gap;
  const es = Math.min((cv.width - pad * 2) / W, (cv.height - elevTop - pad) / H);
  const ex = x => pad + (x - x0) * es, ez = z => elevTop + (z1 - z) * es; // Z up

  ctx.font = '11px monospace'; ctx.fillStyle = '#8a8a99';
  ctx.fillText('PLAN (top-down, +Y up)', pad, 16);
  const drawRect = (X, Y, Wd, Ht, fill, op, stroke) => {
    ctx.globalAlpha = op; ctx.fillStyle = fill; ctx.fillRect(X, Y, Wd, Ht);
    if (stroke) { ctx.globalAlpha = 1; ctx.strokeStyle = stroke; ctx.lineWidth = 2; ctx.strokeRect(X, Y, Wd, Ht); }
    ctx.globalAlpha = 1;
  };
  const sortByZ = [...eff.brushes].sort((a, b) => a.aabb[2] - b.aabb[2]);
  // plan
  for (const br of sortByZ) {
    if (br.layer === 'enclosure' && !S.layers.enclosure) continue;
    if (!S.layers.geometry) break;
    const a = br.aabb;
    drawRect(px(a[0]), py(a[4]), (a[3]-a[0])*s, (a[4]-a[1])*s, br.color,
      br.role.startsWith('deck') ? 0.30 : 0.82, selStroke(br));
  }
  if (S.layers.flows) for (const fl of eff.flows) {
    ctx.strokeStyle = fl.kind === 'pad' ? '#ffb26b' : '#54dcf0'; ctx.lineWidth = 1.4;
    ctx.beginPath(); ctx.moveTo(px(fl.from[0]), py(fl.from[1])); ctx.lineTo(px(fl.to[0]), py(fl.to[1])); ctx.stroke();
  }
  if (S.layers.entities) for (const e of eff.entities) {
    ctx.fillStyle = entColor(e.classname);
    ctx.beginPath(); ctx.arc(px(e.origin[0]), py(e.origin[1]), 5, 0, 7); ctx.fill();
    if (isSel(e)) { ctx.strokeStyle = '#ffb347'; ctx.lineWidth = 2; ctx.stroke(); }
  }
  // elevation
  ctx.fillStyle = '#8a8a99'; ctx.fillText('ELEVATION (side, X × Z)', pad, elevTop - 8);
  for (const br of sortByZ) {
    if (br.layer === 'enclosure' && !S.layers.enclosure) continue;
    if (!S.layers.geometry) break;
    const a = br.aabb;
    drawRect(ex(a[0]), ez(a[5]), (a[3]-a[0])*es, Math.max((a[5]-a[2])*es, 1), br.color,
      br.role.startsWith('deck') ? 0.25 : 0.7, selStroke(br));
  }
  if (S.layers.entities) for (const e of eff.entities) {
    ctx.fillStyle = entColor(e.classname);
    ctx.beginPath(); ctx.arc(ex(e.origin[0]), ez(e.origin[2]), 3.5, 0, 7); ctx.fill();
  }
  // stash transforms for click hit-testing
  cv._t = { px, py, ex, ez, s, es, planBottom, elevTop };
}
function selStroke(br) { return isSel(br) ? '#ffb347' : null; }
function isSel(x) {
  if (!S.sel) return false;
  if (S.sel.added != null) return x.key === 'a' + S.sel.added;
  return x.key === (S.sel.kind === 'brush' ? 'b' : 'e') + S.sel.id;
}

$('view2d').addEventListener('click', ev => {
  if (S.mode !== 'generate') return;
  const cv = $('view2d'); const t = cv._t; if (!t) return;
  const rect = cv.getBoundingClientRect();
  const mx = ev.clientX - rect.left, my = ev.clientY - rect.top;
  const inPlan = my < t.planBottom;
  // invert plan transform
  const wx = (mx - 30) / t.s + S.base.bounds[0];
  const wy = S.base.bounds[4] - (my - 30) / t.s;
  if (S.placing && inPlan) { doPlace([Math.round(wx), Math.round(wy), Math.round(S.base.bounds[2])]); return; }
  if (!inPlan) return;
  // pick nearest entity, else topmost brush footprint
  const eff = effective();
  let best = null, bestd = 16;
  if (S.layers.entities) for (const e of eff.entities) {
    const d = Math.hypot(t.px(e.origin[0]) - mx, t.py(e.origin[1]) - my);
    if (d < bestd) { bestd = d; best = e.key; }
  }
  if (!best && S.layers.geometry) {
    const cands = eff.brushes.filter(br => (br.layer !== 'enclosure' || S.layers.enclosure)
      && wx >= br.aabb[0] && wx <= br.aabb[3] && wy >= br.aabb[1] && wy <= br.aabb[4]);
    cands.sort((a, b) => b.aabb[5] - a.aabb[5]);
    if (cands.length) best = cands[0].key;
  }
  if (best) selectKey(best); else { S.sel = null; refresh(); }
});

// ----------------------------------------------------------------- generate / export
async function loadMapList() {
  try {
    const r = await api.maps();
    const sel = $('mapSel');
    sel.innerHTML = '<option value="">— pick a .bsp / .pk3 —</option>'
      + r.maps.map(m => `<option value="${m.path}">${m.label}</option>`).join('');
    if (!r.maps.length) sel.innerHTML = '<option value="">(no maps found)</option>';
  } catch (e) { /* server may lack the endpoint */ }
}

async function analyzeMaps() {
  const path = $('mapSel').value;
  $('busy').style.display = 'block';
  try {
    const res = await api.analyze(path ? { path } : {});
    const c = res.corpus;
    S.learned = c.suggest;       // calibrates new-brush defaults (learn -> build)
    const d = (k) => c[k] ? `${c[k].median}` : '—';
    $('learned').innerHTML = `<b style="color:var(--amber)">learned from ${c.maps} map(s)</b><br>`
      + `platform ~${d('platform_min_side')}×${d('platform_max_side')}u · `
      + `wall ~${d('wall_height')}u · decks ~${d('decks')}<br>`
      + `map ~${d('map_width')}×${d('map_depth')}×${d('map_height')}u · `
      + `item h ~${d('item_height')}u`;
    toast(`learned from ${c.maps} map(s) — new boxes now sized to taste`);
  } catch (e) { toast('analyze failed: ' + e.message, true); }
  finally { $('busy').style.display = 'none'; }
}

const hexToRgb = h => { const n = parseInt(h.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };
const rgbHex = c => `#${c.map(v => Math.max(0, Math.min(255, v | 0)).toString(16).padStart(2, '0')).join('')}`;

function importToCompose() {
  if (!S.base || !S.imported) { toast('decompile a map first (Import), then edit it here', true); return; }
  C.placed = []; C.brushes = []; C.entities = []; C.brushSeq = 0; C.sel = null;
  let note = '';
  if (S.base.edit_boxes && S.base.edit_boxes.length) {
    // clean exact solids decompiled from the collision brush lump
    for (const b of S.base.edit_boxes)
      C.brushes.push({ id: C.brushSeq++, aabb: [...b.aabb], role: 'structure', color: hexToRgb(b.color) });
  } else {
    // fallback: thicken drawn surfaces into solids
    const T = 16, CAP = 800;
    const src = S.base.brushes.filter(b => b.role !== 'sky/enclosure').slice(0, CAP);
    for (const b of src) {
      let [x0, y0, z0, x1, y1, z1] = b.aabb;
      if (z1 - z0 < T) z0 = z1 - T;
      if (x1 - x0 < T) { x0 -= T / 2; x1 += T / 2; }
      if (y1 - y0 < T) { y0 -= T / 2; y1 += T / 2; }
      C.brushes.push({ id: C.brushSeq++, aabb: [x0, y0, z0, x1, y1, z1], role: 'structure', color: hexToRgb(b.color) });
    }
    note = ' (surface trace)';
  }
  switchMode('compose');
  toast(`traced ${C.brushes.length} solid brushes from ${S.base.name}${note}`);
}

async function calibrateGen() {
  const path = $('mapSel').value;
  $('busy').style.display = 'block';
  try {
    const c = await api.calibrate(path ? { path } : {});
    const setSlider = (id, v) => { $(id).value = v; $(id + 'V').textContent = v.toFixed(2); };
    setSlider('hscale', c.hscale); setSlider('vscale', c.vscale);
    toast(`calibrated to ${c.basis.maps} map(s): hscale ${c.hscale}, vscale ${c.vscale}`);
    await generate();
  } catch (e) { toast('calibrate failed: ' + e.message, true); }
  finally { $('busy').style.display = 'none'; }
}

async function importMap() {
  const path = $('mapSel').value;
  if (!path) { toast('pick a map first', true); return; }
  $('busy').style.display = 'block';
  try {
    S.base = await api.importBsp({ path });
    S.imported = true;
    S.entMods.clear(); S.brushMods.clear(); S.added = []; S.sel = null; S.placing = null;
    $('export').disabled = true; $('export').title = 'imported maps are view-only';
    frameCamera(); refresh(); status();
    const c = S.base.counts;
    toast(`decompiled ${S.base.name} · ${c.triangles} triangles`
      + (c.truncated ? ' (truncated)' : ''));
  } catch (e) { toast('import failed: ' + e.message, true); }
  finally { $('busy').style.display = 'none'; }
}

async function generate() {
  const { kind, params } = gatherParams();
  $('busy').style.display = 'block';
  try {
    S.base = await api.generate({ kind, params, edits: [] });
    S.imported = false; $('export').disabled = false; $('export').title = '';
    S.entMods.clear(); S.brushMods.clear(); S.added = []; S.sel = null; S.placing = null;
    frameCamera(); refresh(); status();
    toast(`built ${kind} · seed ${params.seed} · ${S.base.counts.brushes} brushes`);
  } catch (e) { toast('generate failed: ' + e.message, true); }
  finally { $('busy').style.display = 'none'; }
}

function status() {
  if (S.mode === 'compose') {
    const b = composeBounds().map(Math.round);
    $('status').textContent = `${C.placed.length} sections · `
      + `${(b[3] - b[0])}×${(b[4] - b[1])}×${(b[5] - b[2])}u · drag to move, snaps to dots`;
    return;
  }
  if (!S.base) return;
  const c = S.base.counts; const b = S.base.bounds.map(Math.round);
  const dims = `${(b[3]-b[0])}×${(b[4]-b[1])}×${(b[5]-b[2])}u`;
  if (S.imported) {
    $('status').textContent = `imported ${S.base.name} · ${c.brushes} surfaces · `
      + `${c.triangles} triangles · ${c.entities} entities · ${c.spawns} spawns · ${dims}`
      + (c.truncated ? ' · TRUNCATED' : '');
    return;
  }
  $('status').textContent = `${c.brushes} brushes · ${c.entities} entities · ${c.spawns} spawns `
    + `· ${c.triggers || 0} triggers · ${dims} · ${editCount()} edits`;
}

async function doExport() {
  const { kind, params } = gatherParams();
  const formats = [];
  if ($('fmtBsp').checked) formats.push('bsp');
  if ($('fmtMap').checked) formats.push('map');
  if ($('fmtPk3').checked) formats.push('pk3');
  if (!formats.length) { toast('pick at least one format', true); return; }
  $('busy').style.display = 'block';
  try {
    const res = await api.export({ kind, params, edits: buildEdits(), formats, name: $('exName').value });
    const st = res.stats;
    $('exResult').innerHTML = `<b style="color:var(--green)">✓ ${res.name}</b><br>`
      + res.outputs.map(o => `<span class="pill">${o}</span>`).join(' ')
      + `<br><span class="muted">${st.brushes} brushes · ${st.surfaces} surfaces · `
      + `${Math.round(st.bytes/1024)} KB${res.bots ? ' · bots ✓' : ''}</span>`;
    toast(`exported ${res.name}`);
  } catch (e) {
    $('exResult').innerHTML = `<span style="color:var(--red)">✗ ${e.message}</span>`;
    toast('export failed: ' + e.message, true);
  } finally { $('busy').style.display = 'none'; }
}

// ----------------------------------------------------------------- UI build
function buildUI() {
  const m = S.meta;
  $('kind').innerHTML = m.kinds.map(k => `<option value="${k.id}">${k.label}</option>`).join('');
  $('archetype').innerHTML = '<option value="">(seed-random)</option>'
    + m.archetypes.map(a => `<option>${a}</option>`).join('');
  $('theme').innerHTML = m.themes.map(t => `<option>${t}</option>`).join('');
  $('sections').innerHTML = m.sections.map(s =>
    `<label class="chk"><input type="checkbox" value="${s}" checked> ${s}</label>`).join('');
  const entOpts = m.entityPalette.map(e => `<option value="${e.classname}">${e.label}</option>`).join('');
  $('addEnt').innerHTML = entOpts;
  $('addEntC').innerHTML = entOpts;
  $('legend').innerHTML = m.legend.map(l =>
    `<div class="legend-row"><span class="swatch" style="background:${l.color}"></span>${l.role}</div>`).join('');
  syncKindUI();
}

function syncKindUI() {
  const k = S.meta.kinds.find(x => x.id === $('kind').value);
  const has = p => k.params.includes(p);
  $('lengthWrap').style.display = has('length') ? '' : 'none';
  $('diffRow').style.display = has('difficulty') ? '' : 'none';
  $('archWrap').style.display = has('archetype') ? '' : 'none';
  $('theme').parentElement; $('theme').style.display = has('theme') ? '' : 'none';
  $('themeLbl').style.display = has('theme') ? '' : 'none';
  $('sectionsWrap').style.display = has('sections') ? '' : 'none';
}

function wire() {
  $('generate').onclick = generate;
  $('export').onclick = doExport;
  $('kind').onchange = syncKindUI;
  $('rand').onclick = () => { $('seed').value = Math.floor(Math.random() * 1e6); };
  $('daily').onclick = () => { const d = new Date();
    $('seed').value = `${d.getFullYear()}${String(d.getMonth()+1).padStart(2,'0')}${String(d.getDate()).padStart(2,'0')}`; };
  for (const id of ['vscale', 'hscale', 'density'])
    $(id).oninput = () => $(id + 'V').textContent = (+$(id).value).toFixed(2);
  $('resetEdits').onclick = () => { S.entMods.clear(); S.brushMods.clear(); S.added = []; S.sel = null; refresh(); status(); };
  $('placeEnt').onclick = () => { S.placing = $('addEnt').value;
    $('addHint').textContent = `click in the view to place a ${$('addEnt').value}`; };
  $('addBox').onclick = () => { S.placing = '__box__';
    $('addHint').textContent = 'click in the view to drop a 64×64×64 box'; };
  $('t3d').onclick = () => setView('3d');
  $('t2d').onclick = () => setView('2d');
  $('modeGen').onclick = () => switchMode('generate');
  $('modeCompose').onclick = () => switchMode('compose');
  $('clearCompose').onclick = () => { C.placed = []; C.brushes = []; C.entities = []; C.sel = null; composeRefresh(); };
  $('addBoxC').onclick = addBoxC;
  $('placeEntC').onclick = () => { C.placingEnt = $('addEntC').value; toast(`click in the view to place a ${C.placingEnt}`); };
  $('autoBtn').onclick = () => autoLayout(parseInt($('autoSeed').value) || 0);
  $('autoRand').onclick = () => { $('autoSeed').value = Math.floor(Math.random() * 1e6); autoLayout(parseInt($('autoSeed').value)); };
  $('cExport').onclick = composeExport;
  $('importBtn').onclick = importMap;
  $('analyzeBtn').onclick = analyzeMaps;
  $('calibBtn').onclick = calibrateGen;
  $('editImportBtn').onclick = importToCompose;
  $('mapRefresh').onclick = loadMapList;
  document.querySelectorAll('[data-layer]').forEach(btn => btn.onclick = () => {
    const l = btn.dataset.layer; S.layers[l] = !S.layers[l];
    btn.classList.toggle('on', S.layers[l]); refresh();
  });
}

function setView(v) {
  S.view = v;
  $('t3d').classList.toggle('on', v === '3d'); $('t2d').classList.toggle('on', v === '2d');
  $('gl').style.display = v === '3d' ? '' : 'none';
  $('view2d').style.display = v === '2d' ? 'block' : 'none';
  if (v === '2d') draw2d(); else resize();
}

// ================================================================= COMPOSE
// Kit-bash mode: place section "parts" and snap their entry/exit connectors.
const C = { catalog: null, byKey: {}, placed: [], seq: 0, sel: null, drag: null,
            brushes: [], brushSeq: 0, entities: [], entSeq: 0, placingEnt: null };
const SNAP = 160;   // world-unit snap radius for connectors

const yawOf = d => d[0] === 1 ? 0 : d[1] === 1 ? 90 : d[0] === -1 ? 180 : 270;
function rotxy(x, y, yaw) {
  yaw = ((yaw % 360) + 360) % 360;
  if (yaw === 0) return [x, y];
  if (yaw === 90) return [-y, x];
  if (yaw === 180) return [-x, -y];
  if (yaw === 270) return [y, -x];
  const a = yaw * Math.PI / 180, c = Math.cos(a), s = Math.sin(a);
  return [x * c - y * s, x * s + y * c];
}
const rotDir = (d, yaw) => { const [x, y] = rotxy(d[0], d[1], yaw); return [Math.round(x), Math.round(y)]; };
function worldConn(inst, c) {
  const [rx, ry] = rotxy(c.pos[0], c.pos[1], inst.yaw);
  return { pos: [rx + inst.translate[0], ry + inst.translate[1], c.pos[2] + inst.translate[2]],
           dir: rotDir(c.dir, inst.yaw), id: c.id, kind: c.kind || (c.id === 'in' ? 'in' : 'out') };
}
function mate(localConn, target) {       // place so localConn mates target (world)
  const yaw = ((yawOf(target.dir) - yawOf(localConn.dir)) % 360 + 360) % 360;
  const [rx, ry] = rotxy(localConn.pos[0], localConn.pos[1], yaw);
  return { yaw, translate: [target.pos[0] - rx, target.pos[1] - ry, target.pos[2] - localConn.pos[2]] };
}
const dist3 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);

function partConns(inst) { return C.byKey[inst.key].connectors.map(c => worldConn(inst, c)); }
// a connector is "connected" if a compatible (in<->out) connector of another
// instance sits on top of it
function isConnected(inst, wc) {
  return C.placed.some(o => o.id !== inst.id &&
    partConns(o).some(oc => oc.kind !== wc.kind && dist3(oc.pos, wc.pos) < 4));
}
function freeOutSlots(exceptId) {     // world exit connectors not yet connected
  const out = [];
  for (const o of C.placed) {
    if (o.id === exceptId) continue;
    for (const wc of partConns(o)) if (wc.kind === 'out' && !isConnected(o, wc)) out.push(wc);
  }
  return out;
}

async function loadCatalog() {
  if (C.catalog) return;
  const r = await api.parts();
  C.catalog = r.parts; r.parts.forEach(p => C.byKey[p.key] = p);
  buildPartLib();
}
function buildPartLib() {
  const groups = {};
  C.catalog.forEach(p => (groups[p.group] = groups[p.group] || []).push(p));
  const order = ['start', 'opener', 'flow', 'spice', 'climb', 'finish', 'primitive'];
  $('partLib').innerHTML = order.filter(g => groups[g]).map(g =>
    `<div style="margin:6px 0 2px;color:var(--dim);text-transform:uppercase;font-size:10px">${g}</div>`
    + groups[g].map(p => `<button class="sm" data-part="${p.key}" style="width:100%;text-align:left;margin:2px 0">${p.label}</button>`).join('')
  ).join('');
  $('partLib').querySelectorAll('[data-part]').forEach(b => b.onclick = () => addPart(b.dataset.part));
}

function _placePart(key) {
  // auto-chain: mate this part's 'in' to the most recent free 'out'
  const inst = { id: C.seq++, key, yaw: 0, translate: [0, 0, 0] };
  const slots = freeOutSlots(inst.id);
  if (slots.length) {
    const target = slots[slots.length - 1];
    const localIn = C.byKey[key].connectors.find(c => (c.kind || c.id) === 'in');
    const mt = mate(localIn, target); inst.yaw = mt.yaw; inst.translate = mt.translate;
  }
  C.placed.push(inst);
  return inst;
}
function addPart(key) {
  const inst = _placePart(key);
  C.sel = { t: 'part', id: inst.id };
  composeRefresh(); frameCompose();
}

// seeded procedural assembly over the part kit (deterministic per seed)
function mulberry32(a) {
  return function () {
    a |= 0; a = a + 0x6D2B79F5 | 0;
    let t = Math.imul(a ^ a >>> 15, 1 | a);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}
function autoLayout(seed) {
  const rng = mulberry32(seed | 0);
  const pick = arr => arr[Math.floor(rng() * arr.length)];
  const ri = (lo, hi) => lo + Math.floor(rng() * (hi - lo + 1));
  C.placed = []; C.brushes = []; C.entities = []; C.sel = null; C.seq = 0;
  const seq = ['start'];
  for (let i = 0; i < ri(1, 2); i++) seq.push(pick(['gaps', 'bhop']));
  if (rng() < 0.6) seq.push(pick(['turn_left', 'turn_right']));
  for (let i = 0; i < 2; i++) seq.push(pick(['slalom', 'slide', 'walls', 'floor', 'fork']));
  if (rng() < 0.6) seq.push(pick(['turn_left', 'turn_right']));
  for (let i = 0; i < ri(1, 2); i++) seq.push(pick(['hurdles', 'hazard', 'movers']));
  seq.push(pick(['ramp_up', 'stairs', 'tower']));
  seq.push('finish');
  for (const k of seq) _placePart(k);
  composeRefresh(); frameCompose();
  toast(`auto-laid ${seq.length} sections (seed ${seed})`);
}

function addBoxC() {
  const b = composeBounds();
  const cx = (b[0] + b[3]) / 2, cy = (b[1] + b[4]) / 2, cz = b[2];
  // size to learned real-world dimensions when available (learn -> build loop)
  const hw = (S.learned?.platform_w || 256) / 2, hl = (S.learned?.platform_l || 256) / 2;
  const h = S.learned?.wall_h || 64;
  const id = C.brushSeq++;
  C.brushes.push({ id, aabb: [cx - hw, cy - hl, cz, cx + hw, cy + hl, cz + h], role: 'structure' });
  C.sel = { t: 'brush', id }; composeRefresh();
}
const selPart = () => C.sel && C.sel.t === 'part' ? C.placed.find(p => p.id === C.sel.id) : null;
const selBrush = () => C.sel && C.sel.t === 'brush' ? C.brushes.find(b => b.id === C.sel.id) : null;
const selEnt = () => C.sel && C.sel.t === 'ent' ? C.entities.find(e => e.id === C.sel.id) : null;

function composeBounds() {
  let b = null;
  for (const inst of C.placed) {
    const part = C.byKey[inst.key];
    for (const br of part.brushes) {
      const [x0, y0, z0, x1, y1, z1] = br.aabb;
      for (const corner of [[x0, y0, z0], [x1, y1, z1], [x0, y1, z0], [x1, y0, z1]]) {
        const [wx, wy] = rotxy(corner[0], corner[1], inst.yaw);
        const p = [wx + inst.translate[0], wy + inst.translate[1], corner[2] + inst.translate[2]];
        if (!b) b = [p[0], p[1], p[2], p[0], p[1], p[2]];
        b = [Math.min(b[0], p[0]), Math.min(b[1], p[1]), Math.min(b[2], p[2]),
             Math.max(b[3], p[0]), Math.max(b[4], p[1]), Math.max(b[5], p[2])];
      }
    }
  }
  for (const fb of C.brushes) {
    const a = fb.aabb;
    if (!b) b = [...a];
    b = [Math.min(b[0], a[0]), Math.min(b[1], a[1]), Math.min(b[2], a[2]),
         Math.max(b[3], a[3]), Math.max(b[4], a[4]), Math.max(b[5], a[5])];
  }
  return b || [-512, -512, -256, 512, 512, 256];
}

let composePickables = [];
function renderCompose3d() {
  clearGroup(worldGroup); composePickables = [];
  for (const inst of C.placed) {
    const part = C.byKey[inst.key];
    const grp = new THREE.Group();
    grp.position.set(inst.translate[0], inst.translate[1], inst.translate[2]);
    grp.rotation.z = inst.yaw * Math.PI / 180;
    const sel = C.sel && C.sel.t === 'part' && C.sel.id === inst.id;
    for (const br of part.brushes) {
      const geo = geomFromFaces(br.faces);
      const mat = new THREE.MeshLambertMaterial({ vertexColors: true, side: THREE.DoubleSide });
      const mesh = new THREE.Mesh(geo, mat); mesh.userData = { instId: inst.id };
      grp.add(mesh); composePickables.push(mesh);
      if (S.layers.edges) grp.add(new THREE.LineSegments(new THREE.EdgesGeometry(geo, 25),
        new THREE.LineBasicMaterial({ color: sel ? 0xffb347 : 0x000000, transparent: true, opacity: sel ? 0.9 : 0.3 })));
    }
    if (S.layers.entities) for (const e of part.entities) {
      if (!e.origin) continue;
      const m = new THREE.Mesh(new THREE.OctahedronGeometry(16),
        new THREE.MeshBasicMaterial({ color: entColor(e.classname) }));
      m.position.set(e.origin[0], e.origin[1], e.origin[2] + 16); grp.add(m);
    }
    // connection dots (entry red, exit green; dim when already connected)
    for (const c of part.connectors) {
      const wc = worldConn(inst, c);
      const conn = isConnected(inst, wc);
      const col = wc.kind === 'in' ? 0xff6b6b : 0x8fff8f;
      const dot = new THREE.Mesh(new THREE.SphereGeometry(conn ? 14 : 22, 12, 12),
        new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: conn ? 0.35 : 1 }));
      dot.position.set(c.pos[0], c.pos[1], c.pos[2] + 24);
      dot.userData = { instId: inst.id }; grp.add(dot); composePickables.push(dot);
      const dir = new THREE.ArrowHelper(new THREE.Vector3(c.dir[0], c.dir[1], 0).normalize(),
        new THREE.Vector3(c.pos[0], c.pos[1], c.pos[2] + 24), 90, col, 30, 18);
      grp.add(dir);
    }
    worldGroup.add(grp);
  }
  // free-form box brushes (world space, no part transform)
  for (const fb of C.brushes) {
    const geo = geomFromFaces(boxFaces(fb.aabb, fb.color ? rgbHex(fb.color) : roleColor(fb.role)));
    const mesh = new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ vertexColors: true, side: THREE.DoubleSide }));
    mesh.userData = { brushId: fb.id }; worldGroup.add(mesh); composePickables.push(mesh);
    const selB = C.sel && C.sel.t === 'brush' && C.sel.id === fb.id;
    if (S.layers.edges) worldGroup.add(new THREE.LineSegments(new THREE.EdgesGeometry(geo, 25),
      new THREE.LineBasicMaterial({ color: selB ? 0xffb347 : 0x000000, transparent: true, opacity: selB ? 0.9 : 0.35 })));
  }
  // placed entities (items / weapons / spawns)
  if (S.layers.entities) for (const e of C.entities) {
    const m = new THREE.Mesh(new THREE.OctahedronGeometry(18),
      new THREE.MeshBasicMaterial({ color: entColor(e.classname) }));
    m.position.set(e.origin[0], e.origin[1], e.origin[2] + 18);
    m.userData = { entId: e.id }; worldGroup.add(m); composePickables.push(m);
    if (C.sel && C.sel.t === 'ent' && C.sel.id === e.id)
      worldGroup.add(new THREE.Box3Helper(new THREE.Box3(
        new THREE.Vector3(e.origin[0] - 24, e.origin[1] - 24, e.origin[2]),
        new THREE.Vector3(e.origin[0] + 24, e.origin[1] + 24, e.origin[2] + 48)), 0xffb347));
  }
  // selection bbox
  if (_selBox) { worldGroup.remove(_selBox); _selBox = null; }
  const b = composeBounds();
  worldGroup.add(new THREE.Box3Helper(new THREE.Box3(
    new THREE.Vector3(b[0], b[1], b[2]), new THREE.Vector3(b[3], b[4], b[5])), 0x333344));
  ensureGrid(b);
}

function frameCompose() {
  const b = composeBounds();
  const cx = (b[0] + b[3]) / 2, cy = (b[1] + b[4]) / 2, cz = (b[2] + b[5]) / 2;
  const r = Math.max(b[3] - b[0], b[4] - b[1], b[5] - b[2]) || 1024;
  camera.position.set(cx + r * 0.8, cy - r * 1.2, cz + r * 0.9);
  controls.target.set(cx, cy, cz); controls.update();
}

// ---- compose drag + snap ----
function groundPoint(e, z) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
  const plane = new THREE.Plane(new THREE.Vector3(0, 0, 1), -z);
  const pt = new THREE.Vector3();
  return raycaster.ray.intersectPlane(plane, pt) ? pt : null;
}
$('gl').addEventListener('pointerdown', e => {
  if (S.mode !== 'compose') return;
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
  // placement mode: next click drops the chosen entity
  if (C.placingEnt) {
    const hit0 = raycaster.intersectObjects(composePickables.filter(p => p.userData.instId != null || p.userData.brushId != null), false)[0];
    let pt;
    if (hit0) pt = hit0.point;
    else { const pl = new THREE.Plane(new THREE.Vector3(0, 0, 1), -composeBounds()[2]); pt = new THREE.Vector3(); raycaster.ray.intersectPlane(pl, pt); }
    if (pt) {
      const id = C.entSeq++;
      C.entities.push({ id, classname: C.placingEnt, origin: [Math.round(pt.x), Math.round(pt.y), Math.round(pt.z) + 24] });
      C.sel = { t: 'ent', id };
    }
    C.placingEnt = null; composeRefresh();
    return;
  }
  const hit = raycaster.intersectObjects(composePickables, false)[0];
  if (!hit) return;
  const ud = hit.object.userData;
  if (ud.entId != null) {                         // placed entity
    const ent = C.entities.find(x => x.id === ud.entId);
    C.sel = { t: 'ent', id: ent.id };
    const gp = groundPoint(e, ent.origin[2]);
    C.drag = { ent, start: gp ? [gp.x, gp.y] : null, o0: [...ent.origin] };
  } else if (ud.brushId != null) {                // free box brush
    const box = C.brushes.find(b => b.id === ud.brushId);
    C.sel = { t: 'brush', id: box.id };
    const gp = groundPoint(e, box.aabb[2]);
    C.drag = { box, start: gp ? [gp.x, gp.y] : null, a0: [...box.aabb] };
  } else {                                        // section/primitive part
    const inst = C.placed.find(p => p.id === ud.instId);
    C.sel = { t: 'part', id: inst.id };
    const gp = groundPoint(e, inst.translate[2]);
    C.drag = { inst, start: gp ? [gp.x, gp.y] : null, t0: [...inst.translate] };
  }
  controls.enabled = false;
  composeRefresh();
});
$('gl').addEventListener('pointermove', e => {
  if (S.mode !== 'compose' || !C.drag || !C.drag.start) return;
  if (C.drag.ent) {
    const gp = groundPoint(e, C.drag.o0[2]); if (!gp) return;
    C.drag.ent.origin = [C.drag.o0[0] + (gp.x - C.drag.start[0]),
                         C.drag.o0[1] + (gp.y - C.drag.start[1]), C.drag.o0[2]];
    composeRefresh(); return;
  }
  if (C.drag.box) {
    const gp = groundPoint(e, C.drag.a0[2]); if (!gp) return;
    const dx = gp.x - C.drag.start[0], dy = gp.y - C.drag.start[1];
    const a = C.drag.a0;
    C.drag.box.aabb = [a[0] + dx, a[1] + dy, a[2], a[3] + dx, a[4] + dy, a[5]];
    composeRefresh(); return;
  }
  const gp = groundPoint(e, C.drag.t0[2]); if (!gp) return;
  const inst = C.drag.inst;
  inst.translate = [C.drag.t0[0] + (gp.x - C.drag.start[0]),
                    C.drag.t0[1] + (gp.y - C.drag.start[1]), C.drag.t0[2]];
  inst.snapped = trySnap(inst);
  composeRefresh();
});
addEventListener('pointerup', () => {
  if (C.drag) { C.drag = null; controls.enabled = true; composeRefresh(); }
});

function trySnap(inst) {
  const mine = partConns(inst);
  const targets = [];
  for (const o of C.placed) {
    if (o.id === inst.id) continue;
    for (const wc of partConns(o)) if (!isConnected(o, wc)) targets.push(wc);
  }
  let best = null, bd = SNAP;
  for (const m of mine) for (const t of targets) {
    if (m.kind === t.kind) continue;             // entry <-> exit only
    const d = dist3(m.pos, t.pos);
    if (d < bd) { bd = d; best = { local: C.byKey[inst.key].connectors.find(c => c.id === m.id), target: t }; }
  }
  if (best) { const mt = mate(best.local, best.target); inst.yaw = mt.yaw; inst.translate = mt.translate; return true; }
  return false;
}

function composeRefresh() { renderCompose3d(); drawCompose2d(); renderComposeInspector(); renderPlacedList(); status(); }

function renderComposeInspector() {
  const el = $('composeInspector');
  const ent = selEnt();
  if (ent) {
    const lbl = ['X', 'Y', 'Z'];
    el.innerHTML = `<div class="kv"><span>entity</span><b>${ent.classname}</b></div>`
      + '<div class="grid3">' + ent.origin.map((v, i) =>
        `<div><label>${lbl[i]}</label><input type="number" step="8" data-eo="${i}" value="${Math.round(v)}"></div>`).join('') + '</div>'
      + '<button class="sm danger" id="delEnt" style="width:100%;margin-top:8px">delete entity</button>';
    el.querySelectorAll('[data-eo]').forEach(inp => inp.onchange = () => {
      ent.origin[+inp.dataset.eo] = parseFloat(inp.value); composeRefresh();
    });
    $('delEnt').onclick = () => { C.entities = C.entities.filter(x => x.id !== ent.id); C.sel = null; composeRefresh(); };
    updateConnStatus(); return;
  }
  const box = selBrush();
  if (box) {
    const lbl = ['min X', 'min Y', 'min Z', 'max X', 'max Y', 'max Z'];
    el.innerHTML = `<div class="kv"><span>box brush</span><b>${box.role}</b></div>`
      + '<div class="grid3">' + box.aabb.map((v, i) =>
        `<div><label>${lbl[i]}</label><input type="number" step="8" data-ba="${i}" value="${Math.round(v)}"></div>`).join('') + '</div>'
      + '<label>role</label><select id="bRole">' + S.meta.addRoles.map(r =>
        `<option ${r === box.role ? 'selected' : ''}>${r}</option>`).join('') + '</select>'
      + '<button class="sm danger" id="delBox" style="width:100%;margin-top:8px">delete box</button>';
    el.querySelectorAll('[data-ba]').forEach(inp => inp.onchange = () => {
      box.aabb[+inp.dataset.ba] = parseFloat(inp.value); composeRefresh();
    });
    $('bRole').onchange = () => { box.role = $('bRole').value; composeRefresh(); };
    $('delBox').onclick = () => { C.brushes = C.brushes.filter(b => b.id !== box.id); C.sel = null; composeRefresh(); };
    updateConnStatus(); return;
  }
  const inst = selPart();
  if (!inst) { el.innerHTML = '<div class="muted">Click a section or box to select; drag to move (sections snap to dots).</div>'; updateConnStatus(); return; }
  const part = C.byKey[inst.key];
  el.innerHTML =
    `<div class="kv"><span>section</span><b>${part.label}</b></div>`
    + `<div class="kv"><span>yaw</span><b>${inst.yaw}°</b></div>`
    + `<div class="row" style="margin-top:6px">`
    + `<button class="sm" id="rotL">⟲ -90°</button><button class="sm" id="rotR">⟳ +90°</button></div>`
    + `<div class="row" style="margin-top:4px">`
    + `<button class="sm" id="zDn">z −64</button><button class="sm" id="zUp">z +64</button></div>`
    + `<button class="sm danger" id="delPart" style="width:100%;margin-top:8px">delete section</button>`;
  $('rotL').onclick = () => { inst.yaw = (inst.yaw + 270) % 360; composeRefresh(); };
  $('rotR').onclick = () => { inst.yaw = (inst.yaw + 90) % 360; composeRefresh(); };
  $('zUp').onclick = () => { inst.translate[2] += 64; composeRefresh(); };
  $('zDn').onclick = () => { inst.translate[2] -= 64; composeRefresh(); };
  $('delPart').onclick = () => { C.placed = C.placed.filter(p => p.id !== inst.id); C.sel = null; composeRefresh(); };
  updateConnStatus();
}
function renderPlacedList() {
  const el = $('placedList');
  if (!C.placed.length) { el.innerHTML = '<div class="muted">no sections placed yet</div>'; return; }
  el.innerHTML = C.placed.map(p => {
    const lab = C.byKey[p.key].label;
    const on = C.sel && C.sel.t === 'part' && C.sel.id === p.id;
    return `<div class="legend-row" style="${on ? 'color:var(--amber)' : ''}" data-inst="${p.id}">`
      + `<span class="pill">${p.yaw}°</span>${lab}</div>`;
  }).join('');
  el.querySelectorAll('[data-inst]').forEach(d => d.onclick = () => { C.sel = { t: 'part', id: +d.dataset.inst }; composeRefresh(); });
}
function updateConnStatus() {
  let total = 0, conn = 0;
  for (const inst of C.placed) for (const wc of partConns(inst)) { total++; if (isConnected(inst, wc)) conn++; }
  const open = total - conn;
  const hasStart = C.placed.some(p => p.key === 'start');
  const hasFinish = C.placed.some(p => p.key === 'finish');
  $('connStatus').innerHTML = `${conn / 2 | 0} joint(s) · ${open} open connector(s)<br>`
    + `<span class="pill" style="${hasStart ? 'color:var(--green)' : ''}">start ${hasStart ? '✓' : '—'}</span> `
    + `<span class="pill" style="${hasFinish ? 'color:#96ffdc' : ''}">finish ${hasFinish ? '✓' : '—'}</span>`;
}

function drawCompose2d() {
  const cv = $('view2d'); const v = $('view');
  cv.width = v.clientWidth; cv.height = v.clientHeight;
  const ctx = cv.getContext('2d');
  ctx.fillStyle = '#07070b'; ctx.fillRect(0, 0, cv.width, cv.height);
  if (!C.placed.length && !C.brushes.length) { ctx.fillStyle = '#8a8a99'; ctx.font = '12px monospace';
    ctx.fillText('Compose: add sections from the library (top-down view).', 30, 30); return; }
  const b = composeBounds(); const pad = 40;
  const s = Math.min((cv.width - pad * 2) / (b[3] - b[0] || 1), (cv.height - pad * 2) / (b[4] - b[1] || 1));
  const px = x => pad + (x - b[0]) * s, py = y => cv.height - pad - (y - b[1]) * s;
  for (const inst of C.placed) {
    const part = C.byKey[inst.key];
    for (const br of part.brushes) {
      const [x0, y0, , x1, y1] = br.aabb;
      const pts = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]].map(([x, y]) => {
        const [wx, wy] = rotxy(x, y, inst.yaw); return [px(wx + inst.translate[0]), py(wy + inst.translate[1])];
      });
      ctx.globalAlpha = 0.8; ctx.fillStyle = br.color;
      ctx.beginPath(); ctx.moveTo(pts[0][0], pts[0][1]); pts.slice(1).forEach(p => ctx.lineTo(p[0], p[1])); ctx.closePath(); ctx.fill();
      if (inst.id === C.sel) { ctx.globalAlpha = 1; ctx.strokeStyle = '#ffb347'; ctx.lineWidth = 1.5; ctx.stroke(); }
    }
    ctx.globalAlpha = 1;
    for (const c of part.connectors) {
      const wc = worldConn(inst, c);
      ctx.fillStyle = wc.kind === 'in' ? '#ff6b6b' : '#8fff8f';
      ctx.beginPath(); ctx.arc(px(wc.pos[0]), py(wc.pos[1]), isConnected(inst, wc) ? 3 : 6, 0, 7); ctx.fill();
    }
  }
  for (const fb of C.brushes) {
    const a = fb.aabb;
    ctx.globalAlpha = 0.8; ctx.fillStyle = fb.color ? rgbHex(fb.color) : roleColor(fb.role);
    ctx.fillRect(px(a[0]), py(a[4]), (a[3] - a[0]) * s, (a[4] - a[1]) * s);
    if (C.sel && C.sel.t === 'brush' && C.sel.id === fb.id) {
      ctx.globalAlpha = 1; ctx.strokeStyle = '#ffb347'; ctx.lineWidth = 1.5;
      ctx.strokeRect(px(a[0]), py(a[4]), (a[3] - a[0]) * s, (a[4] - a[1]) * s);
    }
    ctx.globalAlpha = 1;
  }
  for (const e of C.entities) {
    ctx.fillStyle = entColor(e.classname);
    ctx.beginPath(); ctx.arc(px(e.origin[0]), py(e.origin[1]), 5, 0, 7); ctx.fill();
    if (C.sel && C.sel.t === 'ent' && C.sel.id === e.id) {
      ctx.strokeStyle = '#ffb347'; ctx.lineWidth = 2; ctx.stroke();
    }
  }
}

async function composeExport() {
  if (!C.placed.length && !C.brushes.length) { toast('add a section or a box first', true); return; }
  const placedEnts = C.entities.map(e => ({ classname: e.classname, origin: e.origin }));
  const formats = [];
  if ($('cBsp').checked) formats.push('bsp');
  if ($('cMap').checked) formats.push('map');
  if ($('cPk3').checked) formats.push('pk3');
  if (!formats.length) { toast('pick a format', true); return; }
  $('busy').style.display = 'block';
  try {
    const placed = C.placed.map(p => ({ key: p.key, yaw: p.yaw, translate: p.translate }));
    const brushes = C.brushes.map(b => ({ aabb: b.aabb, role: b.role, color: b.color }));
    const res = await api.composeExport({ placed, brushes, entities: placedEnts, opts: { void: true }, formats, name: $('cName').value });
    const st = res.stats;
    $('cResult').innerHTML = `<b style="color:var(--green)">✓ ${res.name}</b><br>`
      + res.outputs.map(o => `<span class="pill">${o}</span>`).join(' ')
      + `<br><span class="muted">${st.brushes} brushes · ${Math.round(st.bytes / 1024)} KB${res.bots ? ' · bots ✓' : ''}</span>`;
    toast(`exported ${res.name}`);
  } catch (e) {
    $('cResult').innerHTML = `<span style="color:var(--red)">✗ ${e.message}</span>`;
    toast('export failed: ' + e.message, true);
  } finally { $('busy').style.display = 'none'; }
}

async function switchMode(m) {
  S.mode = m;
  $('modeGen').classList.toggle('on', m === 'generate');
  $('modeCompose').classList.toggle('on', m === 'compose');
  $('genControls').style.display = m === 'generate' ? '' : 'none';
  $('composeControls').style.display = m === 'compose' ? '' : 'none';
  $('genRight').style.display = m === 'generate' ? '' : 'none';
  $('composeRight').style.display = m === 'compose' ? '' : 'none';
  if (m === 'compose') {
    await loadCatalog();
    if (!C.placed.length) { addPart('start'); }   // seed with a start pad
    composeRefresh(); frameCompose();
  } else { if (S.base) { render3d(); draw2d(); } else generate(); }
  status();
}

// ----------------------------------------------------------------- boot
(async function main() {
  try {
    S.meta = await api.meta();
    buildUI(); wire(); initGL(); setView('3d');
    loadMapList();
    await generate();
  } catch (e) { toast('init failed: ' + e.message, true); console.error(e); }
})();
