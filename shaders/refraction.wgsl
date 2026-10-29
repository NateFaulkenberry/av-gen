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
    return mix(transmitted * (1.0 - fresnel), shaded, opacity);
}

// ADR-1819: an imported glass (refraction1.w = its KHR_materials_transmission factor, > 0), as glTF defines it. Its
// shading already left the diffuse out (LIT_TRANSMISSION), so what it shows is its specular reflection plus the light it
// transmits, tinted by its base colour. Where it looks: thin-walled (refraction0.z = 0, no KHR_materials_volume) straight
// through; with a volume, Filament's solid-sphere model -- the view refracted in at the surface (refraction0.y = ior),
// carried `thickness` along the inward normal, and the exit point projected -- absorbed over that path.
fn refractGlass(shaded: vec3<f32>, normal: vec3<f32>, frontFacing: bool, worldPos: vec3<f32>,
                clipXY: vec2<f32>) -> vec3<f32> {
    let ior = max(object.refraction0.y, 1.0);
    let thickness = object.refraction0.z;
    let n = normalize(select(-normal, normal, frontFacing));
    let v = normalize(frame.cameraPos.xyz - worldPos);
    var uv = clipXY * frame.targetSize.zw;
    var path = 0.0;
    if (thickness > 0.0) {
        let r = refract(-v, n, 1.0 / ior);
        path = thickness * max(-dot(n, r), 0.0);
        let exitClip = frame.viewProj * vec4<f32>(worldPos + r * path, 1.0);
        if (exitClip.w > 1e-4) {
            uv = clamp(exitClip.xy / exitClip.w * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5), vec2<f32>(0.0), vec2<f32>(1.0));
        }
    }
    let seen = textureSampleLevel(refractionCopy, refractionSampler, uv, 0.0).rgb;
    let absorb = exp(-object.refraction1.rgb * path);
    // The Fresnel of the ior's own F0 (0.04 at 1.5), for the share the surface does not reflect.
    let f0s = (ior - 1.0) / (ior + 1.0);
    let f0 = f0s * f0s;
    let nDotV = dot(n, v);
    let fresnel = f0 + (1.0 - f0) * pow(1.0 - clamp(nDotV, 0.0, 1.0), 5.0);
    let glass = shaded + seen * absorb * (1.0 - fresnel) * object.baseColor.rgb * object.refraction1.w;
    // The silhouette. A tessellated curve ends on its facets, but its shading normal (interpolated) turns away from the
    // eye a facet earlier: on the S3 sphere a band ~2 px wide shaded with N.V < 0, where the Fresnel is 1 and nothing
    // was transmitted -- a dark rim stepped along the facet edges. The smooth surface those normals describe ends
    // where N.V = 0, so the pixel's share of it is its distance to that line in pixels (N.V over its screen gradient);
    // the rest of the pixel is what lies behind, which this pass has (the copy, unbent). Coverage 1 wherever N.V is
    // not near 0 (a flat face, the middle of a curve), so only the last pixel or two of a curved silhouette change.
    let coverage = clamp(nDotV / max(fwidth(nDotV), 1e-4) + 0.5, 0.0, 1.0);
    if (coverage >= 1.0) {
        return glass;
    }
    let behind = textureSampleLevel(refractionCopy, refractionSampler, clipXY * frame.targetSize.zw, 0.0).rgb;
    return mix(behind, glass, coverage);
}
