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
$('gl').addEventListener('pointerdown', e => downXY = [e.clientX, e.clientY]);
$('gl').addEventListener('pointerup', e => {
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
async function generate() {
  const { kind, params } = gatherParams();
  $('busy').style.display = 'block';
  try {
    S.base = await api.generate({ kind, params, edits: [] });
    S.entMods.clear(); S.brushMods.clear(); S.added = []; S.sel = null; S.placing = null;
    frameCamera(); refresh(); status();
    toast(`built ${kind} · seed ${params.seed} · ${S.base.counts.brushes} brushes`);
  } catch (e) { toast('generate failed: ' + e.message, true); }
  finally { $('busy').style.display = 'none'; }
}

function status() {
  if (!S.base) return;
  const c = S.base.counts; const b = S.base.bounds.map(Math.round);
  $('status').textContent = `${c.brushes} brushes · ${c.entities} entities · ${c.spawns} spawns `
    + `· ${c.triggers} triggers · ${(b[3]-b[0])}×${(b[4]-b[1])}×${(b[5]-b[2])}u · ${editCount()} edits`;
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
  $('addEnt').innerHTML = m.entityPalette.map(e => `<option value="${e.classname}">${e.label}</option>`).join('');
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

// ----------------------------------------------------------------- boot
(async function main() {
  try {
    S.meta = await api.meta();
    buildUI(); wire(); initGL(); setView('3d');
    await generate();
  } catch (e) { toast('init failed: ' + e.message, true); console.error(e); }
})();
