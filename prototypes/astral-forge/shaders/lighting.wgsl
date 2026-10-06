// THE ASTRAL FORGE -- the reflection-only light rig as a function of direction, shared by flakes and shards.

fn bandBasisF(a: vec3f) -> mat2x3f {
    let up = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(a.y) > 0.9);
    let e1 = normalize(cross(up, a));
    return mat2x3f(e1, cross(a, e1));
}
fn envF(dir: vec3f, alpha: f32) -> vec3f {
    var sum = vec3f(0.0);
    for (var k = 0; k < 4; k++) {
        let A0 = F.bands[2 * k];
        let A1 = F.bands[2 * k + 1];
        if (A1.y <= 0.0) { continue; }
        let x = dot(dir, A0.xyz) - A0.w;
        let ww = A1.x * A1.x + alpha * alpha;
        var g = exp(-x * x / (2.0 * ww)) * A1.x / sqrt(ww);
        if (A1.z > 0.5) {
            let bb = bandBasisF(A0.xyz);
            let ph = safeAtan2(dot(dir, bb[1]), dot(dir, bb[0]));
            let s = fract(ph * A1.z / TAU + F.rig.x * (0.07 + 0.05 * f32(k)));
            g *= smoothstep(0.0, 0.04 + alpha, s) * smoothstep(0.0, 0.04 + alpha, 0.72 - s);
        }
        sum += A1.y * g * mix(vec3f(0.80, 0.90, 1.08), vec3f(1.08, 0.93, 0.78), A1.w);
    }
    // a broad, dim soft-box sweep (orbiting with the rig): it describes the body's curvature between the
    // crisp strips, the way a gradient sweep does in product photography of black chrome
    let key = normalize(vec3f(cos(F.rig.x * 0.23 + 0.8), 0.55, sin(F.rig.x * 0.23 + 0.8)));
    let soft = exp((dot(dir, key) - 1.0) * 2.6) * 0.55 + 0.08 * smoothstep(-0.3, 1.0, dir.y);
    sum += soft * vec3f(0.92, 0.95, 1.0);
    return sum * (1.0 + F.rig.w) + vec3f(F.misc.z * 1.5);
}


// the strips alone (no soft-box sweep): for small plates, which must be dark unless they catch a strip
fn envBands(dir: vec3f, alpha: f32) -> vec3f {
    var sum = vec3f(0.0);
    for (var k = 0; k < 4; k++) {
        let A0 = F.bands[2 * k];
        let A1 = F.bands[2 * k + 1];
        if (A1.y <= 0.0) { continue; }
        let x = dot(dir, A0.xyz) - A0.w;
        let ww = A1.x * A1.x + alpha * alpha;
        var g = exp(-x * x / (2.0 * ww)) * A1.x / sqrt(ww);
        if (A1.z > 0.5) {
            let bb = bandBasisF(A0.xyz);
            let ph = safeAtan2(dot(dir, bb[1]), dot(dir, bb[0]));
            let s = fract(ph * A1.z / TAU + F.rig.x * (0.07 + 0.05 * f32(k)));
            g *= smoothstep(0.0, 0.04 + alpha, s) * smoothstep(0.0, 0.04 + alpha, 0.72 - s);
        }
        sum += A1.y * g * mix(vec3f(0.80, 0.90, 1.08), vec3f(1.08, 0.93, 0.78), A1.w);
    }
    return sum * (1.0 + F.rig.w) + vec3f(F.misc.z * 1.5);
}

