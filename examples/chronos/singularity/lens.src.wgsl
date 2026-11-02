/*{
  "NAME": "chronos-lens",
  "DESCRIPTION": "CHRONOS, Temporal Singularity: the gravitational distortion of everything drawn. A late layer (ADR-1935): it runs after the temporal stage, so the figures, their slit-scan trails, the luminous structures and the background are bent together by the same field (field.wgsl) -- point-lens magnification and compression round each mass, frame dragging that twists the image round them (the eddies the other way), and, in a tear, a smooth shear that drags whole regions sideways. Stateless.",
  "CREDIT": "avgen -- CHRONOS",
  "CATEGORIES": ["Filter", "Distortion"],
  "INPUTS": [
    {"NAME": "lens", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 3.0, "LABEL": "Lensing"},
    {"NAME": "drag", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 3.0, "LABEL": "Frame dragging"},
    {"NAME": "shear", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 3.0, "LABEL": "Tear shear"},
    {"NAME": "coreRadius", "TYPE": "float", "DEFAULT": 0.085, "MIN": 0.02, "MAX": 0.3, "LABEL": "Core radius (match the background)"},
    {"NAME": "scale", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.3, "MAX": 3.0, "LABEL": "Singularity scale (match the background)"},
    {"NAME": "expansion", "TYPE": "float", "DEFAULT": 0.0, "MIN": -1.0, "MAX": 2.0, "LABEL": "Expansion (match the background)"},
    {"NAME": "rotation", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Rotation rate (match the background)"},
    {"NAME": "timeOffset", "TYPE": "float", "DEFAULT": 0.0, "MIN": -1000.0, "MAX": 1000.0, "LABEL": "Time offset (s)"},
    {"NAME": "paletteGuard", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 1.0, "LABEL": "Palette guard (no green or teal)"}
  ]
}*/

//@FIELD

fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let t = sys.time + inputs.timeOffset;
    let aspect = sys.renderSize.x / max(sys.renderSize.y, 1.0);
    let screen = vec2<f32>((uv.x - 0.5) * aspect, 0.5 - uv.y);
    let turn = frameTurn(t);
    let zoom = frameZoom(t, inputs.scale);
    let p = rot(screen, turn) / zoom;

    let D = tear(t);
    let c0 = mainCenter(t);
    let R0 = coreRadiusAt(t, inputs.coreRadius, inputs.expansion);
    let spinT = spinTime(t, inputs.rotation);

    // point lenses: each mass pulls the image toward itself (magnifying near it, compressing beyond). Softened at the
    // centre, so the map stays continuous through the core -- the core is black whatever it samples.
    var q = p;
    {
        let d = p - c0;
        // tuned so the lens magnifies the misshapen core a little and bends what passes near it, without rounding the
        // core into a large black disk (which a strong point lens does: everything inside its Einstein ring maps inside)
        let k = inputs.lens * (0.75 * R0) * (0.75 * R0);
        q = q - k * d / (dot(d, d) + R0 * R0);
    }
    for (var i = 0; i < 3; i = i + 1) {
        let e = eddy(i, t, c0, R0);
        let de = p - e.c;
        let k = inputs.lens * e.w * (1.6 * e.r) * (1.6 * e.r);
        q = q - k * de / (dot(de, de) + e.r * e.r);
        // frame dragging round the eddy, its own way
        let dq = q - e.c;
        q = e.c + rot(dq, inputs.drag * e.spin * e.w * 2.2 * e.r * e.r / (dot(dq, dq) + e.r * e.r));
    }
    // frame dragging round the main mass: the image is wound round it, harder in a tear and when the rotation runs fast
    {
        let d = q - c0;
        let r = length(d);
        let wind = inputs.drag * (0.55 + 0.5 * D) * exp(-r / (4.0 * R0)) + 0.06 * sin(0.21 * spinT) * exp(-r / 0.6);
        q = c0 + rot(d, -wind);
    }
    // the tear: a smooth shear that drags whole regions sideways, round the mass
    {
        let d = q - c0;
        let r = length(d);
        let a = atan2(d.y, d.x);
        let s1 = fbm(vec3<f32>(cos(a) * 1.2, sin(a) * 1.2, log(max(r / R0, 1e-3)) * 1.1 + 0.3 * t), 3) - 0.5;
        let amt = inputs.shear * D * 0.9 * s1 * exp(-r / (7.0 * R0));
        q = c0 + rot(d, amt);
    }

    // back to the screen, and sample what was drawn there
    let back = rot(q * zoom, -turn);
    let suv = vec2<f32>(back.x / aspect + 0.5, 0.5 - back.y);
    var c = textureSampleLevel(inputImage, linearSampler, clamp(suv, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).rgb;
    // the palette guard: the RGB temporal split (ADR-1936) throws complementary fringes, and green and teal are outside
    // the brief's palette (sec. 7). Green may never exceed the warmer of red and blue, so whites stay white and a split
    // fringe runs magenta to amber instead.
    let gmax = max(c.r, c.b) * 0.92;
    c.g = mix(c.g, min(c.g, gmax), inputs.paletteGuard);
    // and a strong blue leans to violet (red under it), never to teal or ice
    c.r = mix(c.r, max(c.r, 0.55 * c.b), inputs.paletteGuard);
    return vec4<f32>(c, 1.0);
}
