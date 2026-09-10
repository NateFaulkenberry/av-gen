// The living chromatic field (ADR-057).
//
// The spatial half of this brief already exists and is resolved on the CPU: `noise::regionField`
// gives each instance a hue offset from where it stands, so a patch agrees with itself and differs
// from the next valley. What it cannot do is *move*, because instance colour is baked once when
// the cloud is projected. This is the other half: a slow drift in world space and time, evaluated
// per vertex at the instance's root.
//
// It is deliberately not noise. `valueNoise` is around 480 scalar operations and `fbm3` around
// 1400, which is the wrong tool for a smooth low-frequency swell, and the wind field (ADR-055)
// already showed that a few travelling plane waves read better and cost a fraction: incommensurate
// wavelengths never repeat on any useful scale, and because each term travels, two patches a
// hundred metres apart are never in phase. Three terms, six transcendentals, weights summing to
// one so the result is bounded by `amount`.
fn livingChromaTurns(p: vec3<f32>, t: f32, amount: f32, invScale: f32, speed: f32) -> f32 {
    let a = sin(p.x * invScale + t * speed);
    let b = sin(p.z * invScale * 0.83 - t * speed * 0.71);
    let c = sin((p.x + p.z) * invScale * 0.37 + t * speed * 0.43);
    return amount * (0.5 * a + 0.35 * b + 0.15 * c);
}

// Rotate an instance's emissive by `turns` of hue.
//
// `inst.emissive` is a *multiplier* on the material's emissive colour, not a colour, so rotating
// it directly rotates the wrong thing -- the product is what anyone sees. So: form the product,
// rotate that in OKLCH (equal angles are equal perceived steps, and L and C survive), and express
// the result back as a multiplier. The ceiling is the same 96 the CPU path uses: a saturated
// emitter has a channel near zero, and turning its hue means raising that channel by tens.
fn livingChromaMultiplier(base: vec3<f32>, mult: vec3<f32>, turns: f32) -> vec3<f32> {
    if (abs(turns) < 1e-6) {
        return mult;
    }
    let safe = max(base, vec3<f32>(1e-4));
    let rotated = max(hueShift(safe * mult, turns), vec3<f32>(0.0));
    return clamp(rotated / safe, vec3<f32>(0.0), vec3<f32>(96.0));
}
