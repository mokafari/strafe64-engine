/* ============================================================
   STRAFE 64 — landing page behaviour
   ============================================================ */
(() => {
  'use strict';
  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---------- BOOT LOG (hero, types out) ---------- */
  const bootLines = [
    '> MAGI defense terminal // PROJECT No.666',
    '> mounting baseoa ......... OK',
    '> render path: PSX/N64 crunch + GL2',
    '> time-bind engine ....... ARMED',
    '> pilot kit .............. 9/9 LOADED',
    '> WARNING: void collapse imminent',
    '> SPEED = LIFE. STILLNESS = DEATH.',
    '> simulation online _',
  ];
  const bootEl = document.getElementById('boot');
  if (bootEl && !reduce) {
    let li = 0, ci = 0, buf = '';
    const type = () => {
      if (li >= bootLines.length) return;
      const line = bootLines[li];
      buf = bootLines.slice(0, li).join('\n') + (li ? '\n' : '') + line.slice(0, ci);
      bootEl.textContent = buf;
      ci++;
      if (ci > line.length) { li++; ci = 0; setTimeout(type, 280); }
      else setTimeout(type, 16 + Math.random() * 26);
    };
    setTimeout(type, 500);
  } else if (bootEl) {
    bootEl.textContent = bootLines.join('\n');
  }

  /* ---------- MARQUEE ---------- */
  const mq = document.getElementById('marquee');
  if (mq) {
    const items = ['VEL <b>0964</b> u/s','SYNC <b>100%</b>','VOID COLLAPSE <b>240m</b>',
      'HOP <b>×27</b>','FLOW <b>NOMINAL</b>','SWORD <b>READY</b>','GHOST <b>−0.42s</b>',
      'AIR-STRAFE <b>OPTIMAL</b>','TARGET <b>ASSASSIN</b>','SYS <b>6.66</b>'];
    const row = '<span>' + items.join('</span><span>') + '</span>';
    mq.innerHTML = row + row; // doubled for seamless loop
  }

  /* ---------- HERO STARFIELD + NEBULA ---------- */
  const cv = document.getElementById('stars');
  if (cv && !reduce) {
    const ctx = cv.getContext('2d');
    let w, h, stars, dpr;
    const resize = () => {
      dpr = Math.min(devicePixelRatio || 1, 2);
      w = cv.width = innerWidth * dpr;
      h = cv.height = cv.offsetHeight * dpr;
      const n = Math.round((w * h) / (9000 * dpr));
      stars = Array.from({ length: n }, () => ({
        x: Math.random() * w, y: Math.random() * h,
        z: Math.random() * 0.8 + 0.2, r: Math.random() * 1.3 + 0.2,
        tw: Math.random() * Math.PI * 2,
      }));
    };
    let t = 0;
    const draw = () => {
      t += 0.016;
      ctx.clearRect(0, 0, w, h);
      // faint nebula
      const g = ctx.createRadialGradient(w * 0.7, h * 0.25, 0, w * 0.7, h * 0.25, h * 0.9);
      g.addColorStop(0, 'rgba(255,107,13,0.05)');
      g.addColorStop(0.5, 'rgba(115,80,255,0.03)');
      g.addColorStop(1, 'rgba(0,0,0,0)');
      ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
      for (const s of stars) {
        s.x -= s.z * 0.25 * dpr; if (s.x < 0) s.x = w;
        const a = 0.4 + Math.sin(t * 2 + s.tw) * 0.35;
        ctx.globalAlpha = Math.max(0, a) * s.z;
        ctx.fillStyle = s.z > 0.7 ? '#cfe6ff' : '#8C6B33';
        ctx.fillRect(s.x, s.y, s.r * dpr, s.r * dpr);
      }
      ctx.globalAlpha = 1;
      requestAnimationFrame(draw);
    };
    addEventListener('resize', resize);
    resize(); draw();
  }

  /* ---------- REVEAL ON SCROLL ---------- */
  const io = new IntersectionObserver((entries) => {
    for (const e of entries) if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); }
  }, { threshold: 0.12, rootMargin: '0px 0px -8% 0px' });
  document.querySelectorAll('.reveal').forEach(el => io.observe(el));

  /* ---------- TOPBAR SOLIDIFY + VOID COLLAPSE GAUGE ---------- */
  const topbar = document.querySelector('.topbar');
  const fill = document.getElementById('collapseFill');
  const cm = document.getElementById('collapseM');
  const onScroll = () => {
    const y = scrollY;
    const max = document.body.scrollHeight - innerHeight;
    const p = max > 0 ? y / max : 0;
    topbar.classList.toggle('solid', y > 80);
    if (fill) fill.style.height = (p * 100).toFixed(1) + '%';
    if (cm) cm.textContent = String(Math.round(p * 666)).padStart(3, '0');
  };
  addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /* ---------- NAV DOTS ---------- */
  const sections = [...document.querySelectorAll('section[id], footer')];
  const dots = document.getElementById('dots');
  if (dots) {
    sections.forEach((sec) => {
      const b = document.createElement('button');
      b.title = (sec.id || 'end').toUpperCase();
      b.addEventListener('click', () => sec.scrollIntoView({ behavior: reduce ? 'auto' : 'smooth' }));
      dots.appendChild(b);
    });
    const dotEls = [...dots.children];
    const spy = new IntersectionObserver((entries) => {
      for (const e of entries) {
        if (e.isIntersecting) {
          const i = sections.indexOf(e.target);
          dotEls.forEach((d, j) => d.classList.toggle('on', j === i));
        }
      }
    }, { threshold: 0.5 });
    sections.forEach(s => spy.observe(s));
  }

  /* ---------- COUNT-UP READOUTS ---------- */
  const countUp = (el, to, dur = 1400, pad = 0) => {
    if (reduce) { el.textContent = pad ? String(to).padStart(pad, '0') : to; return; }
    const start = performance.now();
    const step = (now) => {
      const k = Math.min(1, (now - start) / dur);
      const e = 1 - Math.pow(1 - k, 3);
      const v = Math.round(to * e);
      el.textContent = pad ? String(v).padStart(pad, '0') : v;
      if (k < 1) requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  };
  const vel = document.getElementById('velNum');
  const hop = document.getElementById('hopNum');
  const flow = document.getElementById('flowBar');
  // run after hero settles
  setTimeout(() => {
    if (vel) countUp(vel, 964, 1600, 4);
    if (hop) countUp(hop, 27, 1600);
    if (flow && !reduce) { flow.style.transition = 'width 1.6s cubic-bezier(.2,.7,.2,1)'; flow.style.width = '0'; requestAnimationFrame(() => flow.style.width = '88%'); }
  }, 700);

  // idle velocity flicker (the world is alive)
  if (vel && !reduce) {
    setInterval(() => {
      const base = 940 + Math.round(Math.random() * 48);
      vel.textContent = String(base).padStart(4, '0');
    }, 1800);
  }

  /* ---------- WORDMARK GLITCH ---------- */
  const wm = document.querySelector('.hero .wordmark');
  if (wm && !reduce) {
    setInterval(() => {
      wm.classList.add('glitch');
      setTimeout(() => wm.classList.remove('glitch'), 240);
    }, 4200);
  }

  /* ---------- EXTERNAL LINKS (edit these) ---------- */
  // TODO: replace with real store / demo URLs.
  const LINKS = {
    buy:  'https://strafe64.gumroad.com',   // checkout / store page
    demo: 'https://strafe64.itch.io',       // free demo download
  };
  document.querySelectorAll('.tier__cta').forEach(a => { a.href = LINKS.buy; a.target = '_blank'; a.rel = 'noopener'; });
  document.querySelectorAll('.js-demo').forEach(a => { a.href = LINKS.demo; a.target = '_blank'; a.rel = 'noopener'; });

  /* ---------- TRAILER MODAL ---------- */
  const modal = document.getElementById('trailer');
  const tvid = document.getElementById('trailerVid');
  const openModal = () => { modal.classList.add('open'); modal.setAttribute('aria-hidden', 'false'); if (tvid) { tvid.currentTime = 0; tvid.play().catch(() => {}); } };
  const closeModal = () => { modal.classList.remove('open'); modal.setAttribute('aria-hidden', 'true'); if (tvid) tvid.pause(); };
  document.querySelectorAll('.js-trailer').forEach(b => b.addEventListener('click', openModal));
  if (modal) modal.querySelectorAll('[data-close]').forEach(el => el.addEventListener('click', closeModal));
  addEventListener('keydown', e => { if (e.key === 'Escape') closeModal(); });

  /* ---------- SOUNDTRACK PLAYER + VISUALIZER (mokafari) ---------- */
  // Drop tracks into web/assets/music/ and flip `available:true` + set `src`.
  const TRACKS = [
    { title: 'MACH 64 — Main Theme', dur: '3:48', src: 'assets/music/mach64.mp3',       available: false },
    { title: 'Void Collapse',        dur: '4:12', src: 'assets/music/void-collapse.mp3', available: false },
    { title: 'Bullet-Time',          dur: '3:21', src: 'assets/music/bullet-time.mp3',   available: false },
    { title: 'Slipstream',           dur: '2:57', src: 'assets/music/slipstream.mp3',    available: false },
  ];

  const viz = document.getElementById('viz');
  const playBtn = document.getElementById('playBtn');
  const listEl = document.getElementById('trackList');
  const nowTitle = document.getElementById('nowTitle');
  const nowTime = document.getElementById('nowTime');
  const playerNote = document.getElementById('playerNote');

  if (viz && listEl) {
    const audio = new Audio();
    audio.crossOrigin = 'anonymous';
    audio.preload = 'none';
    let cur = -1, actx = null, analyser = null, freq = null;

    // build track list
    TRACKS.forEach((t, i) => {
      const li = document.createElement('li');
      if (!t.available) li.classList.add('soon');
      li.innerHTML = `<span class="tno">${String(i + 1).padStart(2, '0')}</span>`
        + `<span class="tname">${t.title}</span>`
        + (t.available ? `<span class="tdur">${t.dur}</span>` : `<span class="tsoon">SOON</span>`);
      if (t.available) li.addEventListener('click', () => select(i));
      listEl.appendChild(li);
    });
    const liEls = [...listEl.children];

    const initAudioGraph = () => {
      if (actx) return;
      try {
        actx = new (window.AudioContext || window.webkitAudioContext)();
        const srcNode = actx.createMediaElementSource(audio);
        analyser = actx.createAnalyser();
        analyser.fftSize = 128;
        freq = new Uint8Array(analyser.frequencyBinCount);
        srcNode.connect(analyser); analyser.connect(actx.destination);
      } catch (e) { /* visualizer falls back to idle */ }
    };

    const fmt = (s) => { s = Math.floor(s || 0); return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0'); };

    const select = (i) => {
      const t = TRACKS[i]; if (!t || !t.available) return;
      initAudioGraph();
      if (actx && actx.state === 'suspended') actx.resume();
      cur = i;
      liEls.forEach((el, j) => el.classList.toggle('on', j === i));
      nowTitle.textContent = t.title;
      audio.src = t.src;
      audio.play().then(() => setPlaying(true)).catch(() => {
        playerNote.innerHTML = '▸ Could not load <b>' + t.title + '</b> — check the file in <code>assets/music/</code>.';
      });
    };

    const setPlaying = (p) => { playBtn.textContent = p ? '❚❚' : '▶'; playBtn.classList.toggle('playing', p); };

    playBtn.addEventListener('click', () => {
      if (cur < 0) { const first = TRACKS.findIndex(t => t.available); if (first >= 0) return select(first); return; }
      if (audio.paused) { if (actx && actx.state === 'suspended') actx.resume(); audio.play(); setPlaying(true); }
      else { audio.pause(); setPlaying(false); }
    });
    audio.addEventListener('play', () => setPlaying(true));
    audio.addEventListener('pause', () => setPlaying(false));
    audio.addEventListener('timeupdate', () => { nowTime.textContent = fmt(audio.currentTime); });
    audio.addEventListener('ended', () => {
      const next = TRACKS.findIndex((t, j) => j > cur && t.available);
      if (next >= 0) select(next); else setPlaying(false);
    });

    // ---- visualizer (real FFT when playing, synthetic idle otherwise) ----
    const vctx = viz.getContext('2d');
    let vw, vh, vdpr, vt = 0;
    const vsize = () => { vdpr = Math.min(devicePixelRatio || 1, 2); vw = viz.width = viz.offsetWidth * vdpr; vh = viz.height = viz.offsetHeight * vdpr; };
    addEventListener('resize', vsize); vsize();

    const BARS = 48;
    const vdraw = () => {
      vt += 0.04;
      vctx.clearRect(0, 0, vw, vh);
      const live = analyser && !audio.paused;
      if (live) analyser.getByteFrequencyData(freq);
      const bw = vw / BARS;
      for (let i = 0; i < BARS; i++) {
        let v;
        if (live) {
          v = (freq[Math.floor(i / BARS * freq.length)] / 255);
        } else {
          // synthetic idle — a living breathing spectrum (so it never looks dead)
          const env = 0.55 + 0.45 * Math.sin(i / BARS * Math.PI); // arch across the band
          v = env * (0.34 + 0.26 * Math.sin(vt * 1.3 + i * 0.45) + 0.18 * Math.sin(vt * 2.7 + i * 0.19));
          v = Math.max(0.05, v);
        }
        const bh = v * vh * 0.92;
        const g = vctx.createLinearGradient(0, vh, 0, vh - bh);
        g.addColorStop(0, '#8CF2FF');     // ac-cyan (bass)
        g.addColorStop(0.6, '#66FF80');   // green (mid)
        g.addColorStop(1, '#FF990F');     // amber (high)
        vctx.fillStyle = g;
        const x = i * bw;
        vctx.fillRect(x + bw * 0.18, vh - bh, bw * 0.64, bh);
        // mirrored reflection
        vctx.globalAlpha = 0.12;
        vctx.fillRect(x + bw * 0.18, vh, bw * 0.64, bh * 0.3);
        vctx.globalAlpha = 1;
      }
      requestAnimationFrame(vdraw);
    };
    if (!reduce) vdraw(); else { vctx.fillStyle = '#0a1014'; vctx.fillRect(0, 0, vw, vh); }
  }

  /* ---------- TIME-BIND VIDEO: play on view, pause off ---------- */
  const vid = document.getElementById('btVideo');
  if (vid) {
    const vio = new IntersectionObserver((entries) => {
      for (const e of entries) {
        if (e.isIntersecting) vid.play().catch(() => {});
        else vid.pause();
      }
    }, { threshold: 0.4 });
    vio.observe(vid);
  }
})();
