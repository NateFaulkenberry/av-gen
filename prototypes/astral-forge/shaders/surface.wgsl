// THE ASTRAL FORGE -- the implicit surface and its material.
//
// The visible surface is the iso-surface of the PARTICLE DENSITY (metaballs from matter). As coherence rises,
// the sharpness S pulls it onto the latent anatomy's zero set, but only where matter already is:
//     f = mix(dRho, max(latent, dRhoDilated), S)
// With no matter there is no surface, whatever the latent says. Approach D (for the benchmark) marches the
// latent directly instead.
//
// Material: dark conductor + guilloche grooves (V-groove normals, anisotropic reflection across the
// grooves, a first-order-and-up grating term) + thin-film temper colour + molten heat in the grooves.
// The only lights are the reflection-only bands: the background stays black.

@group(1) @binding(0) var densTex: texture_3d<f32>;
@group(1) @binding(1) var densSamp: sampler;
@group(1) @binding(2) var coarseTex: texture_3d<f32>;

struct VOut { @builtin(position) clip: vec4f, @location(0) uv: vec2f, };
@vertex fn vs_full(@builtin(vertex_index) vi: u32) -> VOut {
    let xy = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
    var o: VOut;
    o.clip = vec4f(xy * 2.0 - 1.0, 0.0, 1.0);
    o.uv = vec2f(xy.x, 1.0 - xy.y);
    return o;
}

fn boxExtent() -> f32 { return F.grid0.w * F.grid1.x; }
fn density(p: vec3f) -> vec2f {
    let uvw = (p - F.grid0.xyz) / boxExtent();
    return textureSampleLevel(densTex, densSamp, uvw, 0.0).rg;
}

// The reconstructed field (positive outside).
fn fieldAt(p: vec3f) -> f32 {
    let approach = i32(F.sim.w);
    if (approach == 3) { return latent(p); }
    let s = density(p).r;
    let T = F.grid1.z;
    let cell = F.grid0.w;
    let dRho = (T - s) * cell * 1.6;
    // sharpness is local: the anatomy's centre (eyes, mouth) becomes precise, its periphery stays matter
    var S = select(0.0, F.ent0.z, approach == 4);
    if (S > 0.001) {
        let qc = (p - F.entity.xyz) / F.entity.w;
        S *= mix(1.0, clamp(1.3 - 0.55 * length(qc.xy / vec2f(2.0, 2.8)) - 0.2 * abs(qc.z), 0.15, 1.0), F.ext.x);
    }
    if (S > 0.001 && s > T * 0.12) {
        let L = latent(p);
        let dil = (T * 0.38 - s) * cell * 3.0;
        return mix(dRho, max(L, dil), S);
    }
    return dRho;
}

fn normalAt(p: vec3f, e: f32) -> vec3f {
    let k0 = vec3f(1.0, -1.0, -1.0); let k1 = vec3f(-1.0, -1.0, 1.0);
    let k2 = vec3f(-1.0, 1.0, -1.0); let k3 = vec3f(1.0, 1.0, 1.0);
    let g = k0 * fieldAt(p + k0 * e) + k1 * fieldAt(p + k1 * e) + k2 * fieldAt(p + k2 * e) + k3 * fieldAt(p + k3 * e);
    let l = length(g);
    return select(vec3f(0.0, 0.0, 1.0), g / l, l > 1e-9);
}

// ---- the light: reflection-only bands ------------------------------------------------------------
fn bandBasis(a: vec3f) -> mat2x3f {
    let up = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(a.y) > 0.9);
    let e1 = normalize(cross(up, a));
    return mat2x3f(e1, cross(a, e1));
}
fn bandColor(warm: f32) -> vec3f { return mix(vec3f(0.80, 0.90, 1.08), vec3f(1.08, 0.93, 0.78), warm); }

fn env(dir: vec3f, alpha: f32) -> vec3f {
    var sum = vec3f(0.0);
    for (var k = 0; k < 4; k++) {
        let A0 = F.bands[2 * k];
        let A1 = F.bands[2 * k + 1];
        if (A1.y <= 0.0) { continue; }
        let x = dot(dir, A0.xyz) - A0.w;
        let ww = A1.x * A1.x + alpha * alpha;
        var g = exp(-x * x / (2.0 * ww)) * A1.x / sqrt(ww);
        if (A1.z > 0.5) {
            let bb = bandBasis(A0.xyz);
            let ph = safeAtan2(dot(dir, bb[1]), dot(dir, bb[0]));
            let s = fract(ph * A1.z / TAU + F.rig.x * (0.07 + 0.05 * f32(k)));
            let soft = 0.04 + alpha;
            g *= smoothstep(0.0, soft, s) * smoothstep(0.0, soft, 0.72 - s);
        }
        sum += A1.y * g * bandColor(A1.w);
    }
    // the faintest omni term so cavities are not absolute zero, plus the collapse strobe
    // a broad, dim soft-box sweep (orbiting with the rig): it describes the body's curvature between the
    // crisp strips, the way a gradient sweep does in product photography of black chrome
    let key = normalize(vec3f(cos(F.rig.x * 0.23 + 0.8), 0.55, sin(F.rig.x * 0.23 + 0.8)));
    let soft = exp((dot(dir, key) - 1.0) * 2.6) * 0.55 + 0.08 * smoothstep(-0.3, 1.0, dir.y);
    sum += soft * vec3f(0.92, 0.95, 1.0);
    return sum * (1.0 + F.rig.w) + vec3f(0.006) + vec3f(F.misc.z * 1.5);
}
// The point on band k nearest a direction: the "light direction" for the grating term.
fn bandDir(k: i32, r: vec3f) -> vec3f {
    let A0 = F.bands[2 * k];
    return normalize(r - A0.xyz * (dot(r, A0.xyz) - A0.w));
}

// ---- engraving ----------------------------------------------------------------------------------
struct Groove { n: vec3f, tang: vec3f, across: vec3f, mask: f32, };

fn wrapPi(x: f32) -> f32 { return x - TAU * round(x / TAU); }

fn grooveFamily(p: vec3f, n: vec3f, fam: i32, freq: f32, depth: f32, footprint: f32, groove: ptr<function, Groove>, wfam: f32) {
    if (wfam < 0.02) { return; }
    let e = 0.004 * F.entity.w;
    let E0 = engraveUV(warp(p), fam);
    let Ex = engraveUV(warp(p + vec3f(e, 0.0, 0.0)), fam);
    let Ey = engraveUV(warp(p + vec3f(0.0, e, 0.0)), fam);
    let Ez = engraveUV(warp(p + vec3f(0.0, 0.0, e)), fam);
    // gradients of u and v; an angular u is unwrapped across the atan2 branch cut
    var du = vec3f(Ex.uv.x - E0.uv.x, Ey.uv.x - E0.uv.x, Ez.uv.x - E0.uv.x);
    if (E0.angular > 0.5) { du = vec3f(wrapPi(du.x), wrapPi(du.y), wrapPi(du.z)); }
    du /= e;
    let dv = vec3f(Ex.uv.y - E0.uv.y, Ey.uv.y - E0.uv.y, Ez.uv.y - E0.uv.y) / e;
    let u = E0.uv.x;
    let v = E0.uv.y;
    let A = 1.5 * E0.amp; // wave height, in line spacings
    let drift = 0.35; // phase advance per line: the braid
    for (var o = 0; o < 4; o++) {
        let sc = pow(4.0, f32(o));
        let ff = freq * sc;
        let nn = E0.n * pow(2.0, f32(o)); // petals double per octave while lines quadruple: no spokes
        let arg = nn * u + drift * ff * v;
        let L = ff * v + A * sin(arg);
        var grad = ff * dv + A * cos(arg) * (nn * du + drift * ff * dv);
        grad -= n * dot(n, grad);
        let gl = length(grad);
        if (gl < 1e-5) { continue; }
        let aa = gl * footprint;          // line cycles per pixel
        let vis = (1.0 - smoothstep(0.12, 0.35, aa)) * wfam;
        if (vis < 0.01) { continue; }
        let across = grad / gl;
        let x = fract(L) - 0.5;
        let w = 0.32;
        let h = max(0.0, 1.0 - abs(x) / w);
        let slope = select(0.0, -sign(x) / w, abs(x) < w);
        (*groove).n = normalize((*groove).n - across * slope * depth * vis / pow(1.6, f32(o)));
        if (h * vis >= (*groove).mask) {
            (*groove).mask = h * vis;
            (*groove).tang = normalize(cross(n, across));
            (*groove).across = across;
        }
    }
}

struct Shade { col: vec3f, };

fn shadeSurface(p: vec3f, n0: vec3f, rd: vec3f, tHit: f32) -> vec3f {
    let V = -rd;
    let q = warp(p);
    let fw = featureWeight(q);
    let arch = i32(F.ent1.x);
    let footprint = tHit * F.camFwd.w;
    let dh = density(p);
    let heat = clamp(dh.g, 0.0, 2.0);

    var gr: Groove;
    gr.n = n0; gr.mask = 0.0;
    gr.tang = normalize(cross(n0, select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(n0.y) > 0.9)));
    gr.across = cross(gr.tang, n0);
    let eyeW = clamp(fw * 1.6, 0.0, 1.0);
    let isFace = arch <= 4 || F.fold1.w > 0.5;
    // guilloche is cut in PANELS: engraved fields separated by polished bands, as on a real engine-turned
    // piece. The polished bands are where the macro form reads, through clean reflections.
    let panel = smoothstep(0.42, 0.58, vnoise3(q * 0.55 + vec3f(3.1, 0.0, 0.0), 91u));
    // eyeballs are mirror-polished: the rose lines are cut only on the socket rim around them
    let eyeBall = select(0.0, smoothstep(0.42, 0.6, fw), isFace);
    grooveFamily(p, n0, 0, 12.0, 0.35, footprint, &gr, select(0.0, eyeW * (1.0 - eyeBall), isFace));
    grooveFamily(p, n0, 1, 7.0, 0.28, footprint, &gr, (1.0 - 0.8 * eyeW) * panel);
    grooveFamily(p, n0, 2, 9.0, 0.16, footprint, &gr, 0.25 * (1.0 - eyeW) * (1.0 - panel));
    let n = gr.n;
    let cosV = clamp(dot(n, V), 0.0, 1.0);

    // conductor: gunmetal base, polished near the eyes, edge tint toward silver
    let polish = clamp(fw * 1.3, 0.0, 1.0);
    var F0 = vec3f(0.42, 0.43, 0.46) * (0.82 + 0.25 * polish);
    if (arch == 4) { F0 = vec3f(0.55, 0.56, 0.58); }
    // temper: oxide thickness from the energy state, thicker where the matter is hot and around features
    let tn = vnoise3(q * 0.9 + vec3f(0.0, F.cam.w * 0.05, 0.0), 71u);
    let thick = F.ent0.w * (330.0 + 90.0 * tn + 60.0 * fw) + 140.0 * min(heat, 1.0);
    var film = thinFilm(cosV, thick);
    // temper is a tint on metal, not paint: keep 55% of its chroma, and let it gather at the features
    film = mix(vec3f(dot(film, vec3f(0.2126, 0.7152, 0.0722))), film, 0.35 + 0.4 * fw);
    let F0f = clamp(F0 * film, vec3f(0.0), vec3f(1.0));
    let edge = vec3f(0.80, 0.81, 0.83);
    let Fr = F0f + (edge - F0f) * pow(1.0 - cosV, 5.0);

    // anisotropic reflection: grooves smear the reflection ACROSS the grooves
    let r = reflect(-V, n);
    let aAlong = 0.018 + 0.04 * (1.0 - polish);
    let aAcross = aAlong + 0.22 * gr.mask;
    var spec = vec3f(0.0);
    for (var j = -2; j <= 2; j++) {
        let rj = normalize(r + gr.across * f32(j) * aAcross * 0.5);
        spec += env(rj, aAlong);
    }
    spec *= 0.2;

    // specular occlusion: matter shadows matter along the reflected ray; and density AO
    var occ = 0.0;
    let cell = F.grid0.w;
    for (var k = 1; k <= 6; k++) {
        let s = density(p + r * cell * (1.5 + 2.2 * f32(k))).r;
        occ += max(s - 0.1, 0.0);
    }
    let so = exp(-occ * 0.55 / max(F.grid1.z, 1e-3));
    let ao1 = density(p + n0 * cell * 2.5).r + 0.6 * density(p + n0 * cell * 6.0).r + 0.3 * density(p + n0 * cell * 12.0).r;
    let ao = exp(-ao1 * 0.45 / max(F.grid1.z, 1e-3));

    var col = spec * Fr * so * (0.35 + 0.65 * ao);

    // diffraction from the grooves: d (sin i + sin o) = m lambda, across the grooves only
    let dNm = F.flags.w * 1000.0;
    // no grating on the eye rosettes: their groove direction turns too fast per pixel and aliases to confetti
    let gratW = gr.mask * (1.0 - eyeW);
    if (gratW > 0.02) {
        for (var k = 0; k < 4; k++) {
            let A1 = F.bands[2 * k + 1];
            if (A1.y <= 0.0) { continue; }
            let L = bandDir(k, r);
            let s = L + V;
            let along = dot(s, gr.tang);
            let wAlong = exp(-along * along / (2.0 * 0.06 * 0.06));
            if (wAlong < 0.01 || dot(L, n) < 0.0) { continue; }
            let u = abs(dot(s, gr.across));
            let bandI = env(L, 0.0);
            for (var m = 1; m <= 3; m++) {
                let lam = dNm * u / f32(m);
                let inVis = smoothstep(390.0, 430.0, lam) * (1.0 - smoothstep(660.0, 700.0, lam));
                col += spectralRgb(lam) * inVis * bandI * wAlong * gratW * 0.11 / f32(m) * so;
            }
        }
    }

    // edge energy: a thin spectral rim on the features the viewer should read
    let rim = pow(1.0 - cosV, 4.0) * fw;
    col += rim * thinFilm(0.2, 260.0 + 220.0 * F.ent0.w) * 0.35 * (0.3 + F.audio0.w);

    // forge heat: molten light in the grooves of hot matter
    col += heatColor(heat * 0.8) * (0.1 + 0.9 * gr.mask) * 0.3;
    return col;
}

struct FOut { @location(0) color: vec4f, @location(1) depth: vec4f, };

@fragment fn fs_surface(i: VOut) -> FOut {
    let ndc = vec2f(i.uv.x * 2.0 - 1.0, 1.0 - i.uv.y * 2.0);
    let a = F.invViewProj * vec4f(ndc, 0.0, 1.0);
    let b = F.invViewProj * vec4f(ndc, 1.0, 1.0);
    let ro = F.cam.xyz;
    let rd = normalize(b.xyz / b.w - a.xyz / a.w);
    var o: FOut;
    o.color = vec4f(0.0, 0.0, 0.0, 1.0);
    o.depth = vec4f(1e9, 0.0, 0.0, 0.0);
    let approach = i32(F.sim.w);
    if (approach == 0) { return o; } // A: particles only

    let bmin = F.grid0.xyz;
    let bmax = F.grid0.xyz + vec3f(boxExtent());
    let inv = 1.0 / rd;
    let t0v = (bmin - ro) * inv; let t1v = (bmax - ro) * inv;
    let tn = max(max(min(t0v.x, t1v.x), min(t0v.y, t1v.y)), min(t0v.z, t1v.z));
    let tf = min(min(max(t0v.x, t1v.x), max(t0v.y, t1v.y)), max(t0v.z, t1v.z));
    if (tf <= max(tn, 0.0)) { return o; }

    let cell = F.grid0.w;
    let T = F.grid1.z;
    let coarseSize = cell * 8.0;
    let px = vec2u(i.clip.xy);
    let jit = u01(hashu(px.x * 1973u + px.y * 9277u + u32(F.sim.y) * 26699u));
    var t = max(tn, 0.02) + jit * cell * 0.5;
    var tPrev = t;
    var hit = false;
    var haze = 0.0;
    var hazeHeat = 0.0;
    for (var it = 0; it < 360; it++) {
        if (t > tf) { break; }
        let p = ro + rd * t;
        if (approach != 3) {
            let cc = vec3i(floor((p - bmin) / coarseSize));
            let m = textureLoad(coarseTex, cc, 0).r;
            if (m < T * 0.3) {
                // skip to the exit of this coarse cell
                let cmin = bmin + vec3f(cc) * coarseSize;
                let e0 = (cmin - ro) * inv; let e1 = (cmin + vec3f(coarseSize) - ro) * inv;
                let tx = min(min(max(e0.x, e1.x), max(e0.y, e1.y)), max(e0.z, e1.z));
                tPrev = t;
                t = max(tx, t) + cell * 0.3;
                continue;
            }
        }
        let f = fieldAt(p);
        if (approach >= 2) {
            let s = density(p).rg;
            haze += min(s.r, T) * cell;
            hazeHeat += s.r * s.g * cell;
        }
        if (f < 0.0015 * t + 0.002) { hit = true; break; }
        tPrev = t;
        if (approach == 3) { t += max(f * 0.75, 0.002 * t); }
        else { t += clamp(f * 0.7, cell * 0.3, cell * 3.0); }
    }
    var col = vec3f(0.0);
    if (hit) {
        // refine by bisection
        var lo = tPrev; var hi = t;
        for (var k = 0; k < 6; k++) {
            let mid = 0.5 * (lo + hi);
            if (fieldAt(ro + rd * mid) < 0.0) { hi = mid; } else { lo = mid; }
        }
        t = hi;
        let p = ro + rd * t;
        let S = select(0.0, F.ent0.z, approach == 4);
        let e = select(mix(cell * 0.7, 0.01 * F.entity.w, S), 0.004 * F.entity.w, approach == 3);
        let n = normalAt(p, e);
        if (i32(F.flags.x) == 3) {
            col = vec3f(0.0);
        } else if (i32(F.flags.x) == 1) {
            col = vec3f(0.5 + 0.5 * dot(n, normalize(vec3f(0.4, 0.7, 0.6))));
        } else {
            col = shadeSurface(p, n, rd, t);
        }
        o.depth = vec4f(t, 0.0, 0.0, 0.0);
    }
    // haze: the field's low density, lit faintly by the bands and by heat
    let hz = 1.0 - exp(-haze * F.look.z);
    let hazeLight = vec3f(0.035, 0.038, 0.045) * (1.0 + 2.0 * F.audio0.w) + heatColor(0.6) * min(hazeHeat * F.look.z, 1.0) * 0.02;
    col = col * (1.0 - hz * 0.6) + hazeLight * hz;
    o.color = vec4f(col, 1.0);
    return o;
}
