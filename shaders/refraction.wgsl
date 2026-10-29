// ADR-1603 (from procedural.wgsl, shared by ADR-1819 with the entity path): screen-space refraction. A refractive surface
// (object.refraction0.x > 0) drawn after the refraction copy shows what is behind it: the copy read at the pixel offset
// along the surface's view-space normal, absorbed per channel over the path to what is behind (the linear depth there
// minus the surface's own, at most refraction0.z metres), under `opacity` of its own shaded colour and its Fresnel
// reflectance. ADR-1640: with refraction0.w > 0, the copy is read across a spectrum of offsets (chromatic dispersion).
// Needs `frame`, `object` and `sceneLinearDepth` from its includer.
@group(0) @binding(18) var refractionCopy: texture_2d<f32>;
@group(0) @binding(19) var refractionSampler: sampler;

// ADR-1640: the dispersion's wavelength taps, and how much of each lands in red, green and blue (three Gaussians, a
// stand-in for the eye's response: red 610 nm, green 545, blue 465).
const DISPERSION_TAPS: i32 = 7;
fn dispersionWeights(lambda: f32) -> vec3<f32> {
    let g = (vec3<f32>(lambda) - vec3<f32>(610.0, 545.0, 465.0)) / vec3<f32>(40.0, 38.0, 32.0);
    return exp(-0.5 * g * g);
}

// The surface's shaded colour `shaded` with what it transmits mixed under it.
fn refractSurface(shaded: vec3<f32>, normal: vec3<f32>, frontFacing: bool, worldPos: vec3<f32>,
                  clipXY: vec2<f32>) -> vec3<f32> {
    let strength = object.refraction0.x;
    let opacity = object.refraction0.y;
    let maxThickness = object.refraction0.z;
    let n = normalize(select(-normal, normal, frontFacing));
    let toEye = frame.cameraPos.xyz - worldPos;
    let v = normalize(toEye);
    let screenUv = clipXY * frame.targetSize.zw;
    // the view-space normal's screen components: right and up (a screen's v runs down)
    let offset = vec2<f32>(dot(n, frame.cameraRight.xyz), -dot(n, frame.cameraUp.xyz)) * strength;
    let uv = clamp(screenUv - offset, vec2<f32>(0.0), vec2<f32>(1.0));
    let texel = vec2<i32>(clamp(uv * frame.targetSize.xy, vec2<f32>(0.0), frame.targetSize.xy - vec2<f32>(1.0)));
    let behind = textureLoad(sceneLinearDepth, texel, 0).r;
    let own = max(dot(-toEye, frame.cameraForward.xyz), 1e-4);
    let path = clamp(behind - own, 0.0, maxThickness);
    let absorb = exp(-object.refraction1.rgb * path);
    var seen = textureSampleLevel(refractionCopy, refractionSampler, uv, 0.0).rgb;
    // ADR-1640: chromatic dispersion. The copy is read again at DISPERSION_TAPS wavelengths from 650 to 450 nm, each along
    // the same offset scaled by Cauchy's 1/lambda^2 about 550 nm (red least far, blue most; the two ends are
    // `dispersion` of the offset apart), and each read lands in red, green and blue by its wavelength's weight
    // (dispersionWeights, normalised per channel so a grey stays grey). Seven taps rather than three: three flat bands
    // across a wide fringe become thirteen colours (the [measure] banding case), for +0.11 ms at 4K over three reads.
    // The depth and the absorption path stay the single read's. 0 = the single read alone, the frame before ADR-1640.
    let dispersion = object.refraction0.w;
    if (dispersion > 0.0) {
        var acc = vec3<f32>(0.0);
        var wsum = vec3<f32>(0.0);
        for (var k = 0; k < DISPERSION_TAPS; k = k + 1) {
            let lambda = mix(650.0, 450.0, f32(k) / f32(DISPERSION_TAPS - 1));
            let kc = (1.0 / (lambda * lambda) - 1.0 / (550.0 * 550.0)) / (1.0 / (450.0 * 450.0) - 1.0 / (650.0 * 650.0));
            let uvk = clamp(screenUv - offset * (1.0 + kc * dispersion), vec2<f32>(0.0), vec2<f32>(1.0));
            let w = dispersionWeights(lambda);
            acc = acc + textureSampleLevel(refractionCopy, refractionSampler, uvk, 0.0).rgb * w;
            wsum = wsum + w;
        }
        seen = acc / wsum;
    }
    let transmitted = seen * absorb;
    let fresnel = 0.04 + 0.96 * pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 5.0);
    // ADR-1819: an imported glass (refraction1.w = its KHR_materials_transmission factor, > 0). Its shading already
    // left the diffuse out (LIT_TRANSMISSION), so what it shows is its specular reflection plus the light it
    // transmits, tinted by its base colour (glTF's transmission is), the diffuse share kept as `opacity` of the shaded.
    let transmission = object.refraction1.w;
    if (transmission > 0.0) {
        return shaded + transmitted * (1.0 - fresnel) * object.baseColor.rgb * transmission;
    }
    return mix(transmitted * (1.0 - fresnel), shaded, opacity);
}
