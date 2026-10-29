// Four assistant-presence concepts, each drawing idle / listening / thinking / speaking
// from blended state weights, so a change of state morphs instead of cutting.
(function () {
  const TAU = Math.PI * 2;
  const KEYS = ['idle', 'listen', 'think', 'speak'];
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const lerp = (a, b, t) => a + (b - a) * t;
  const mix = (w, cols) => {
    const o = [0, 0, 0];
    for (const k of KEYS) { o[0] += cols[k][0] * w[k]; o[1] += cols[k][1] * w[k]; o[2] += cols[k][2] * w[k]; }
    return o;
  };
  const rgba = (c, a) => `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${clamp(a, 0, 1).toFixed(3)})`;
  const rng = (seed) => {
    let s = seed >>> 0;
    return () => {
      s = (s + 0x6d2b79f5) >>> 0;
      let t = s;
      t = Math.imul(t ^ (t >>> 15), t | 1);
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  };

  // Speech-like loudness: syllables in phrases, separated by pauses (a stand-in for real audio).
  class VoiceSim {
    constructor(seed, rate, phrase) {
      this.r = rng(seed); this.rate = rate; this.phrase = phrase;
      this.syl = []; this.next = 0; this.phraseEnd = 0; this.level = 0; this.started = false;
    }
    sample(t) {
      if (!this.started) { this.started = true; this.next = t + 0.15; this.phraseEnd = this.next + this.span(); }
      if (this.next < t - 5) { this.next = t; this.phraseEnd = t + this.span(); }
      while (this.next < t) {
        if (this.next > this.phraseEnd) {
          this.next = this.phraseEnd + 0.3 + this.r() * 0.55;
          this.phraseEnd = this.next + this.span();
          continue;
        }
        this.syl.push({ t: this.next, a: 0.45 + this.r() * 0.55, d: 0.06 + this.r() * 0.08 });
        this.next += (1 / this.rate) * (0.65 + this.r() * 0.7);
      }
      this.syl = this.syl.filter((s) => t - s.t < 0.7);
      let v = 0;
      for (const s of this.syl) {
        const dt = t - s.t;
        if (dt < 0) continue;
        v = Math.max(v, s.a * (dt < 0.035 ? dt / 0.035 : Math.exp(-(dt - 0.035) / s.d)));
      }
      this.level += (v - this.level) * (v > this.level ? 0.55 : 0.13);
      return this.level;
    }
    span() { return this.phrase[0] + this.r() * (this.phrase[1] - this.phrase[0]); }
  }

  // Optional real microphone for "listening" (opened only after the viewer asks).
  const Mic = {
    on: false, level: 0, floor: 0.004, peak: 0.05,
    async start() {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true } });
      const ctx = new AudioContext();
      const an = ctx.createAnalyser();
      an.fftSize = 1024;
      ctx.createMediaStreamSource(stream).connect(an);
      Object.assign(this, { stream, ctx, an, buf: new Float32Array(an.fftSize), on: true, level: 0 });
    },
    stop() {
      this.on = false;
      if (this.stream) this.stream.getTracks().forEach((tr) => tr.stop());
      if (this.ctx) this.ctx.close();
      this.stream = this.ctx = null;
    },
    sample() {
      this.an.getFloatTimeDomainData(this.buf);
      let s = 0;
      for (let i = 0; i < this.buf.length; i++) s += this.buf[i] * this.buf[i];
      const rms = Math.sqrt(s / this.buf.length);
      this.floor = rms < this.floor ? lerp(this.floor, rms, 0.3) : lerp(this.floor, rms, 0.003);
      this.peak = Math.max(this.peak * 0.996, rms, this.floor * 3 + 0.01);
      const v = clamp((rms - this.floor * 1.6) / (this.peak - this.floor * 1.6), 0, 1);
      this.level += (v - this.level) * (v > this.level ? 0.6 : 0.15);
      return this.level;
    },
  };

  const radial = (ctx, x, y, r0, r1, stops) => {
    const g = ctx.createRadialGradient(x, y, r0, x, y, r1);
    for (const [o, c] of stops) g.addColorStop(o, c);
    return g;
  };

  /* ---------- 1. Μαργαριτάρι ---------- */
  const Pearl = {
    id: 'pearl', name: 'Μαργαριτάρι', layout: 'orb',
    desc: 'Μια σφαίρα με απαλό φως μαργαριταριού. Αναπνέει όταν περιμένει, απλώνει κύματα όταν ακούει, γυρίζει φως μέσα της όταν σκέφτεται και πάλλεται με τη φωνή όταν μιλάει.',
    cost: 'Στη συσκευή: μία έτοιμη εικόνα της σφαίρας που αλλάζει μόνο μέγεθος και φωτεινότητα, και λεπτά δαχτυλίδια. Χαμηλό κόστος, πιο ελαφρύ από το σημερινό Mercury.',
    init: () => ({ ripples: [], last: -9, prev: 0 }),
    draw(ctx, s, st) {
      const { t, w, lin, lout } = s;
      const cx = 240, cy = 236;
      const drive = w.listen * lin + w.speak * lout;
      const breath = Math.sin(t * TAU / 6.5);
      const R = 80 * (1 + 0.02 * breath * w.idle + 0.05 * lin * w.listen + 0.08 * lout * w.speak - 0.04 * w.think);
      const glow = mix(w, { idle: [232, 206, 166], listen: [110, 224, 236], think: [168, 132, 255], speak: [255, 150, 190] });
      const ga = 0.17 + 0.06 * breath * w.idle + 0.34 * drive + 0.16 * w.think;
      ctx.fillStyle = radial(ctx, cx, cy, R * 0.7, R * 2.7, [[0, rgba(glow, ga)], [0.42, rgba(glow, ga * 0.33)], [1, rgba(glow, 0)]]);
      ctx.fillRect(0, 0, 480, 480);

      const rippleW = w.listen + w.speak;
      if (rippleW > 0.3 && drive > 0.5 && st.prev <= 0.5 && t - st.last > 0.16) { st.ripples.push(t); st.last = t; }
      st.prev = drive;
      st.ripples = st.ripples.filter((t0) => t - t0 < 1.6);
      for (const t0 of st.ripples) {
        const age = (t - t0) / 1.6;
        ctx.strokeStyle = rgba(glow, (1 - age) * (1 - age) * 0.55 * rippleW);
        ctx.lineWidth = 2;
        ctx.beginPath(); ctx.arc(cx, cy, R + 10 + 125 * (1 - Math.pow(1 - age, 2.2)), 0, TAU); ctx.stroke();
      }
      if (w.listen > 0.01) {
        ctx.strokeStyle = rgba(glow, w.listen * (0.3 + 0.6 * lin));
        ctx.lineWidth = 2 + 2.5 * lin;
        ctx.beginPath(); ctx.arc(cx, cy, R + 14 + 26 * lin, 0, TAU); ctx.stroke();
      }
      if (w.think > 0.01) {
        const head = t * 2.6, segs = 40, span = 2.2;
        ctx.lineCap = 'round';
        for (let i = 0; i < segs; i++) {
          const f = i / segs;
          ctx.strokeStyle = rgba([200, 172, 255], w.think * Math.pow(1 - f, 1.6));
          ctx.lineWidth = 3.4 * (1 - f) + 0.5;
          ctx.beginPath(); ctx.arc(cx, cy, R + 17, head - (f + 1 / segs) * span, head - f * span); ctx.stroke();
        }
      }

      ctx.save();
      ctx.beginPath(); ctx.arc(cx, cy, R, 0, TAU); ctx.clip();
      const body = ctx.createRadialGradient(cx - R * 0.32, cy - R * 0.36, R * 0.04, cx - R * 0.08, cy - R * 0.04, R * 1.2);
      [[0, '#fffaf2'], [0.28, '#efe4d4'], [0.62, '#cbbba5'], [0.86, '#7f6e5a'], [1, '#2e261f']].forEach(([o, c]) => body.addColorStop(o, c));
      ctx.fillStyle = body; ctx.fillRect(cx - R, cy - R, 2 * R, 2 * R);
      const blob = (x, y, r, col, a) => {
        ctx.fillStyle = radial(ctx, x, y, 0, r, [[0, rgba(col, a)], [1, rgba(col, 0)]]);
        ctx.fillRect(cx - R, cy - R, 2 * R, 2 * R);
      };
      ctx.globalCompositeOperation = 'screen';
      const ph = t * 0.25;
      blob(cx + Math.cos(ph) * R * 0.5, cy + Math.sin(ph) * R * 0.45 + R * 0.2, R * 1.0, [150, 176, 255], 0.34);
      blob(cx + Math.cos(ph + 2.6) * R * 0.55, cy + Math.sin(ph + 2.6) * R * 0.45, R * 0.85, [255, 170, 192], 0.28);
      blob(cx + Math.cos(ph + 4.4) * R * 0.5, cy + Math.sin(ph + 4.4) * R * 0.5, R * 0.7, [170, 255, 226], 0.14);
      blob(cx, cy + R * 0.32, R * 1.25, mix(w, { idle: [0, 0, 0], listen: [60, 200, 230], think: [140, 100, 255], speak: [255, 110, 170] }), 0.42 * (1 - w.idle));
      if (w.think > 0.01) {
        const a1 = t * 1.7;
        blob(cx + Math.cos(a1) * R * 0.42, cy + Math.sin(a1) * R * 0.42, R * 0.72, [190, 150, 255], 0.7 * w.think);
        blob(cx + Math.cos(a1 + Math.PI) * R * 0.42, cy + Math.sin(a1 + Math.PI) * R * 0.42, R * 0.6, [255, 236, 214], 0.5 * w.think);
      }
      ctx.globalCompositeOperation = 'source-over';
      const ha = -2.25 + 0.22 * Math.sin(t * 0.45);
      const hx = cx + Math.cos(ha) * R * 0.5, hy = cy + Math.sin(ha) * R * 0.5;
      ctx.fillStyle = radial(ctx, hx, hy, 0, R * 0.34, [[0, `rgba(255,255,255,${0.82 + 0.18 * drive})`], [1, 'rgba(255,255,255,0)']]);
      ctx.fillRect(cx - R, cy - R, 2 * R, 2 * R);
      ctx.restore();
      ctx.strokeStyle = 'rgba(255,240,222,0.16)'; ctx.lineWidth = 1.2;
      ctx.beginPath(); ctx.arc(cx, cy, R - 0.6, 0, TAU); ctx.stroke();
    },
  };

  /* ---------- 2. Σέλας ---------- */
  const Aurora = {
    id: 'aurora', name: 'Σέλας', layout: 'orb',
    desc: 'Τρεις κορδέλες φωτός. Κυματίζουν ήρεμα στην αναμονή, ακολουθούν τη φωνή σου όταν ακούει, δένονται σε έναν βρόχο που γυρίζει όταν σκέφτεται και χορεύουν με τη φωνή της όταν μιλάει.',
    cost: 'Στη συσκευή: τρεις καμπύλες γραμμές με λάμψη σε λωρίδα 480×200. Μεσαίο κόστος.',
    init: () => ({ ph: [0, 0, 0] }),
    draw(ctx, s, st) {
      const { t, dt, w, lin, lout } = s;
      const cx = 240, cy = 240;
      const drive = w.listen * lin + w.speak * lout;
      const speed = 0.7 * w.idle + (1.6 + 2.6 * lin) * w.listen + 1.2 * w.think + (1.4 + 2.4 * lout) * w.speak;
      const haze = mix(w, { idle: [70, 100, 200], listen: [40, 170, 200], think: [120, 80, 230], speak: [220, 80, 160] });
      ctx.fillStyle = radial(ctx, cx, cy, 10, 270, [[0, rgba(haze, 0.1 + 0.18 * drive + 0.08 * w.think)], [1, rgba(haze, 0)]]);
      ctx.fillRect(0, 0, 480, 480);
      const cfg = [{ k: 1.55, sp: 1, amp: 1, off: 0 }, { k: 2.25, sp: 1.32, amp: 0.78, off: 2.1 }, { k: 1.15, sp: 0.78, amp: 0.62, off: 4.2 }];
      const pals = [
        { idle: [70, 200, 190], listen: [56, 225, 201], think: [157, 107, 255], speak: [255, 111, 177] },
        { idle: [80, 120, 230], listen: [80, 170, 255], think: [90, 140, 255], speak: [170, 110, 255] },
        { idle: [140, 110, 230], listen: [130, 240, 255], think: [255, 120, 200], speak: [255, 170, 120] },
      ];
      const A = 10 * w.idle + (16 + 92 * lin) * w.listen + 10 * w.think + (14 + 86 * lout) * w.speak;
      const N = 80;
      ctx.globalCompositeOperation = 'lighter';
      ctx.lineJoin = 'round'; ctx.lineCap = 'round';
      cfg.forEach((c, j) => {
        st.ph[j] += dt * speed * c.sp;
        const ph = st.ph[j] + c.off;
        const col = mix(w, pals[j]);
        const path = new Path2D();
        for (let i = 0; i <= N; i++) {
          const u = i / N;
          const env = Math.pow(Math.sin(Math.PI * u), 1.5);
          const xw = 36 + 408 * u;
          const yw = cy + env * c.amp * A * (0.75 * Math.sin(c.k * TAU * u + ph) + 0.35 * Math.sin(2.6 * c.k * TAU * u - ph * 1.3));
          const th = TAU * u + t * 1.5 + j * 0.9;
          const x = lerp(xw, cx + (118 + 10 * j) * Math.sin(th), w.think);
          const y = lerp(yw, cy + (64 - 6 * j) * Math.sin(2 * th), w.think);
          if (i) path.lineTo(x, y); else path.moveTo(x, y);
        }
        const fade = ctx.createLinearGradient(30, 0, 450, 0);
        fade.addColorStop(0, rgba(col, 0)); fade.addColorStop(0.22, rgba(col, 1));
        fade.addColorStop(0.78, rgba(col, 1)); fade.addColorStop(1, rgba(col, 0));
        ctx.strokeStyle = fade;
        const strength = 0.55 + 0.45 * Math.min(1, drive + w.think + 0.4 * w.idle);
        for (const [lw, la] of [[30, 0.07], [14, 0.16], [5, 0.42], [1.8, 0.95]]) {
          ctx.globalAlpha = la * strength;
          ctx.lineWidth = lw * (1 + 0.5 * drive);
          ctx.stroke(path);
        }
      });
      ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
    },
  };

  /* ---------- 3. Αστερισμός ---------- */
  const Stars = {
    id: 'stars', name: 'Αστερισμός', layout: 'orb',
    desc: 'Μια σφαίρα από 300 φωτεινά σημεία που γυρίζει αργά. Η επιφάνειά της ανασηκώνεται με τη φωνή σου, μια φωτεινή ζώνη τη σαρώνει όταν σκέφτεται και κύματα ξεκινούν από το κέντρο όταν μιλάει.',
    cost: 'Στη συσκευή: κουκκίδες σε μικρό καμβά 240×240, όπως το σημερινό Mercury. Μεσαίο κόστος.',
    init() {
      const N = 300, pts = [], ga = Math.PI * (3 - Math.sqrt(5)), r = rng(7);
      for (let i = 0; i < N; i++) {
        const y = 1 - (2 * (i + 0.5)) / N, rad = Math.sqrt(1 - y * y), p = i * ga;
        pts.push([Math.cos(p) * rad, y, Math.sin(p) * rad, r()]);
      }
      return { pts, rot: 0 };
    },
    draw(ctx, s, st) {
      const { t, dt, w, lin, lout } = s;
      const cx = 240, cy = 236;
      const drive = w.listen * lin + w.speak * lout;
      st.rot += dt * (0.16 * w.idle + (0.35 + 0.5 * lin) * w.listen + 0.55 * w.think + 0.28 * w.speak);
      const R = 118 * (1 + 0.02 * Math.sin(t * TAU / 6.5) * w.idle + 0.05 * lout * w.speak);
      const col = mix(w, { idle: [214, 226, 255], listen: [110, 232, 245], think: [182, 150, 255], speak: [255, 150, 200] });
      const hot = mix(w, { idle: [255, 255, 255], listen: [225, 255, 255], think: [244, 226, 255], speak: [255, 232, 242] });
      ctx.fillStyle = radial(ctx, cx, cy, R * 0.2, R * 1.9, [[0, rgba(col, 0.1 + 0.2 * drive + 0.08 * w.think)], [1, rgba(col, 0)]]);
      ctx.fillRect(0, 0, 480, 480);
      const ct = Math.cos(0.38), stl = Math.sin(0.38), cr = Math.cos(st.rot), sr = Math.sin(st.rot);
      const band = Math.sin(t * 1.6);
      ctx.globalCompositeOperation = 'lighter';
      for (const p of st.pts) {
        let x = p[0], y = p[1], z = p[2];
        if (w.think > 0.01) {
          const tw = w.think * 1.3 * y * Math.sin(t * 0.9), c2 = Math.cos(tw), s2 = Math.sin(tw);
          const nx = x * c2 - z * s2; z = x * s2 + z * c2; x = nx; y *= 1 - 0.18 * w.think;
        }
        const rx = x * cr - z * sr, rz = x * sr + z * cr;
        const ty = y * ct - rz * stl, tz = y * stl + rz * ct;
        let k = 1;
        if (w.listen > 0.01) k += w.listen * lin * 0.28 * (0.5 + 0.5 * Math.sin(6 * x + 4.5 * t + p[3] * 6) * Math.sin(5 * y - 3.2 * t));
        if (w.speak > 0.01) k += w.speak * (0.03 + 0.17 * lout) * Math.cos(8 * Math.acos(clamp(tz, -1, 1)) - 9 * t);
        const depth = (tz + 1) / 2;
        let bright = 0.16 + 0.84 * depth;
        if (w.think > 0.01) bright += w.think * 0.9 * Math.exp(-Math.pow((y - band) / 0.16, 2));
        ctx.fillStyle = rgba(bright > 1 ? hot : col, bright);
        ctx.beginPath(); ctx.arc(cx + rx * R * k, cy + ty * R * k, (0.8 + 1.9 * depth) * (1 + 0.5 * drive), 0, TAU); ctx.fill();
      }
      ctx.globalCompositeOperation = 'source-over';
    },
  };

  /* ---------- 4. Περίγραμμα ---------- */
  const Edge = {
    id: 'edge', name: 'Περίγραμμα', layout: 'edge',
    desc: 'Το φως ζει στο περίγραμμα της οθόνης και το κείμενο μένει καθαρό στη μέση. Στην αναμονή φέγγει απαλά από κάτω, όταν ακούει ή μιλάει φωτίζεται όλο το περίγραμμα με τη φωνή, και όταν σκέφτεται το διατρέχουν δύο φωτεινοί κομήτες.',
    cost: 'Στη συσκευή: έτοιμες λωρίδες λάμψης στις άκρες με αλλαγή χρώματος και διαφάνειας, και ένα μικρό φωτεινό στοιχείο για τον κομήτη. Χαμηλό έως μεσαίο κόστος.',
    init: () => ({ spin: 0, comet: 0 }),
    draw(ctx, s, st) {
      const { t, dt, w, lin, lout } = s;
      st.spin += dt * (0.35 + 0.9 * (w.listen * lin + w.speak * lout));
      st.comet += dt * (0.5 + 3.6 * w.think);
      const path = new Path2D();
      path.roundRect(2, 2, 476, 476, 34);
      if (w.idle > 0.01) {
        const b = 0.5 + 0.5 * Math.sin(t * TAU / 6.5);
        ctx.fillStyle = radial(ctx, 240, 540, 10, 320, [[0, `rgba(170,186,255,${(0.34 + 0.14 * b) * w.idle})`], [0.5, `rgba(120,110,255,${0.13 * w.idle})`], [1, 'rgba(120,110,255,0)']]);
        ctx.fillRect(0, 200, 480, 280);
      }
      const rim = (alpha, level, cols) => {
        if (alpha < 0.01) return;
        const g = ctx.createConicGradient(st.spin, 240, 240);
        cols.forEach((c, i) => g.addColorStop(i / (cols.length - 1), c));
        ctx.strokeStyle = g;
        ctx.globalCompositeOperation = 'lighter';
        for (const [lw, la] of [[56, 0.07], [28, 0.16], [12, 0.38], [4, 0.9]]) {
          ctx.globalAlpha = la * alpha;
          ctx.lineWidth = lw * (0.65 + 0.9 * level);
          ctx.stroke(path);
        }
        ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
      };
      rim(w.listen * (0.4 + 0.6 * lin), lin, ['#38e1c9', '#4f8dff', '#7ce8ff', '#2fc6ff', '#38e1c9']);
      rim(w.speak * (0.4 + 0.6 * lout), lout, ['#ff6fb1', '#9d6bff', '#ffb27a', '#ff8fd0', '#ff6fb1']);
      if (w.think > 0.01) {
        rim(w.think * 0.22, 0.1, ['#6f58c9', '#4f6bd9', '#6f58c9']);
        const g = ctx.createConicGradient(st.comet, 240, 240);
        const c = 'rgba(196,166,255,';
        [[0, 0], [0.33, 0], [0.48, 1], [0.5, 0], [0.83, 0], [0.98, 1], [1, 0]].forEach(([o, a]) => g.addColorStop(o, c + a + ')'));
        ctx.strokeStyle = g;
        ctx.globalCompositeOperation = 'lighter';
        for (const [lw, la] of [[44, 0.12], [18, 0.32], [5, 1]]) {
          ctx.globalAlpha = la * w.think; ctx.lineWidth = lw; ctx.stroke(path);
        }
        ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
      }
    },
  };

  const CONCEPTS = [Pearl, Aurora, Stars, Edge];
  const STATES = [
    { key: 'idle', name: 'Αναμονή', hint: 'περιμένει' },
    { key: 'listen', name: 'Ακούει', hint: 'ακολουθεί τη φωνή σου' },
    { key: 'think', name: 'Σκέφτεται', hint: 'επεξεργάζεται' },
    { key: 'speak', name: 'Μιλάει', hint: 'ακολουθεί τη φωνή της' },
  ];
  const COPY = {
    idle: { top: '', bottom: 'Άγγιξε για να μιλήσεις' },
    listen: { top: 'Σε ακούω', bottom: 'Τι καιρό θα κάνει αύριο…' },
    think: { top: 'Σκέφτομαι', bottom: '«Τι καιρό θα κάνει αύριο;»' },
    speak: { top: 'Αύρα', bottom: 'Αύριο λιακάδα, έως 23°.' },
  };

  const overlay = (layout, key) => {
    const c = COPY[key];
    if (layout === 'edge') {
      if (key === 'idle') return `<div class="center clockc">18:24</div><div class="center-date">Πέμπτη 29 Οκτωβρίου</div>`;
      return `<div class="top">${c.top}</div><div class="center${key === 'think' ? ' dim' : ''}">${key === 'speak' ? 'Αύριο λιακάδα με λίγα σύννεφα, έως 23° το μεσημέρι.' : c.bottom}</div>`;
    }
    if (key === 'idle') return `<div class="clock">18:24</div><div class="date">Πέμπτη 29 Οκτωβρίου</div><div class="bottom">${c.bottom}</div>`;
    return `<div class="top">${c.top}</div><div class="bottom">${c.bottom}</div>`;
  };

  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const Engine = { playing: !reduced, players: new Set(), last: 0 };
  let seed = 11;

  class Player {
    constructor(stage, concept, state) {
      this.stage = stage; this.state = state; this.visible = false;
      this.canvas = document.createElement('canvas');
      this.canvas.setAttribute('aria-hidden', 'true');
      stage.appendChild(this.canvas);
      this.ctx = this.canvas.getContext('2d');
      this.simIn = new VoiceSim(seed++, 4.6, [1.4, 2.6]);
      this.simOut = new VoiceSim(seed++, 5.4, [1.8, 3.4]);
      this.ov = {};
      this.setConcept(concept);
      this.w = Object.fromEntries(KEYS.map((k) => [k, k === state ? 1 : 0]));
      new ResizeObserver(() => this.resize()).observe(stage.parentElement);
      new IntersectionObserver((es) => { this.visible = es[0].isIntersecting; if (this.visible) this.paint(performance.now() / 1000, 0); }).observe(stage.parentElement);
      Engine.players.add(this);
    }
    setConcept(concept) {
      this.concept = concept;
      this.st = concept.init();
      this.stage.querySelectorAll('.ov').forEach((n) => n.remove());
      this.ov = {};
      const keys = this.state ? [this.state] : KEYS;
      for (const k of keys) {
        const el = document.createElement('div');
        el.className = 'ov ov-' + concept.layout;
        el.innerHTML = overlay(concept.layout, k);
        this.stage.appendChild(el);
        this.ov[k] = el;
      }
    }
    resize() {
      const css = this.stage.parentElement.clientWidth;
      if (!css) return;
      const px = Math.round(css * Math.min(2, window.devicePixelRatio || 1));
      if (this.canvas.width !== px) { this.canvas.width = this.canvas.height = px; }
      this.stage.style.transform = `scale(${css / 480})`;
      this.paint(performance.now() / 1000, 0);
    }
    paint(t, dt) {
      const { ctx, canvas } = this;
      if (!canvas.width) return;
      if (this.flow) this.flow.update(dt, this);
      const lin = Mic.on && (this.state === 'listen' || !this.state) ? Mic.level : this.simIn.sample(t);
      const lout = this.simOut.sample(t);
      ctx.setTransform(canvas.width / 480, 0, 0, canvas.width / 480, 0, 0);
      ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
      ctx.fillStyle = '#000'; ctx.fillRect(0, 0, 480, 480);
      this.concept.draw(ctx, { t, dt, w: this.w, lin, lout }, this.st);
      if (!this.state) for (const k of KEYS) this.ov[k].style.opacity = Math.pow(this.w[k], 1.6).toFixed(3);
    }
  }

  // Auto-cycling idle -> listening -> thinking -> speaking with eased weights.
  class Flow {
    constructor() {
      this.seq = [['idle', 4.5], ['listen', 5.5], ['think', 3.4], ['speak', 6.5]];
      this.i = 0; this.timer = 0; this.auto = true; this.cur = 'idle'; this.onChange = null;
    }
    set(key) { this.auto = false; this.cur = key; this.onChange && this.onChange(); }
    resume() { this.auto = true; this.i = KEYS.indexOf(this.cur); this.timer = 0; this.onChange && this.onChange(); }
    update(dt, p) {
      if (this.auto && Engine.playing) {
        this.timer += dt;
        if (this.timer > this.seq[this.i][1]) {
          this.timer = 0; this.i = (this.i + 1) % this.seq.length; this.cur = this.seq[this.i][0];
          this.onChange && this.onChange();
        }
      }
      const k = dt ? 1 - Math.exp(-dt / 0.2) : 1;
      let sum = 0;
      for (const key of KEYS) { p.w[key] += ((key === this.cur ? 1 : 0) - p.w[key]) * k; sum += p.w[key]; }
      for (const key of KEYS) p.w[key] /= sum;
    }
  }

  function frame(now) {
    requestAnimationFrame(frame);
    if (!Engine.playing) { Engine.last = now; return; }
    if (now - Engine.last < 1000 / 31) return;
    const dt = Math.min(0.1, (now - Engine.last) / 1000);
    Engine.last = now;
    if (Mic.on) Mic.sample();
    for (const p of Engine.players) if (p.visible) p.paint(now / 1000, dt);
  }
  requestAnimationFrame(frame);

  window.AURA_MOTION = { CONCEPTS, STATES, KEYS, Player, Flow, Engine, Mic, reduced };
})();
