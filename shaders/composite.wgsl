// The 2D composition pass (ADR-081): text, shapes and overlays drawn over the finished frame.
//
// Display-referred, after tone mapping. #FFFFFF is white here; nothing in this pass goes through
// AgX, and nothing in it is tone mapped twice. See docs/composition.md for the argument.
//
// One pipeline, one pass, one branch on an integer. A glyph reads a signed distance field out of
// the shared atlas; a rectangle and an ellipse evaluate theirs analytically. Fill, outline/stroke
// and glow all come out of the same distance, which is why three effects cost no extra passes.

struct CompositeUniforms {
    targetSize: vec2<f32>,
    atlasSize: vec2<f32>,
};

// Mirrors avgen::comp::LayerItem. Everything that animates is in here.
struct ItemStyle {
    xform0: vec4<f32>, // local -> pixels 2x2, row major
    xform1: vec4<f32>, // xy = translation in pixels, zw = box half extent in local units
    color: vec4<f32>,  // fill
    accent: vec4<f32>, // glyph outline / shape stroke
    glow: vec4<f32>,
    params: vec4<f32>, // x = kind, y = outline/stroke half width, z = corner radius, w = glow extent
};

@group(0) @binding(0) var<uniform> uniforms: CompositeUniforms;
@group(0) @binding(1) var<storage, read> items: array<ItemStyle>;
@group(0) @binding(2) var atlas: texture_2d<f32>;
@group(0) @binding(3) var atlasSampler: sampler;

struct VertexIn {
    @location(0) local: vec2<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) item: u32,
};

struct VertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) @interpolate(flat) item: u32,
};

@vertex
fn vs_main(in: VertexIn) -> VertexOut {
    let style = items[in.item];
    // A layer whose vertices are already in final local units (text, in em) carries a half-extent
    // of 0.5, so this is the identity for it and the box scale for a shape.
    let local = in.local * (style.xform1.zw * 2.0);
    let px = vec2<f32>(style.xform0.x * local.x + style.xform0.y * local.y + style.xform1.x,
                       style.xform0.z * local.x + style.xform0.w * local.y + style.xform1.y);
    var out: VertexOut;
    out.clip = vec4<f32>(px.x / uniforms.targetSize.x * 2.0 - 1.0,
                         1.0 - px.y / uniforms.targetSize.y * 2.0, 0.0, 1.0);
    out.uv = in.uv;
    out.item = in.item;
    return out;
}

fn premul(c: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(c.rgb * c.a, c.a);
}

// `src` over `under`, both premultiplied.
fn over(under: vec4<f32>, src: vec4<f32>) -> vec4<f32> {
    return src + under * (1.0 - src.a);
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    let style = items[in.item];
    let kind = u32(style.params.x + 0.5);

    // Both distance fields are evaluated unconditionally. Texture sampling and derivatives are
    // only defined in uniform control flow, so the branch has to come after them, not around them.
    let sdfValue = textureSample(atlas, atlasSampler, in.uv).r;
    let sdfWidth = max(fwidth(sdfValue), 1e-5);

    let half = max(style.xform1.zw, vec2<f32>(1e-6));
    let p = in.uv * (half * 2.0);
    let radius = clamp(style.params.z, 0.0, min(half.x, half.y));
    let b = max(half - vec2<f32>(radius), vec2<f32>(0.0));
    let q = abs(p) - b;
    let boxDistance = length(max(q, vec2<f32>(0.0))) + min(max(q.x, q.y), 0.0) - radius;
    let ellipseDistance = (length(p / half) - 1.0) * min(half.x, half.y);
    let shapeDistance = select(boxDistance, ellipseDistance, kind == 2u);
    let shapeWidth = max(fwidth(shapeDistance), 1e-6);

    var fillAlpha = 0.0;
    var accentAlpha = 0.0;
    var glowAlpha = 0.0;
    if (kind == 0u) {
        // The field is positive inside; 0.5 is the outline.
        fillAlpha = smoothstep(0.5 - sdfWidth, 0.5 + sdfWidth, sdfValue);
        let outline = style.params.y;
        if (outline > 0.0) {
            accentAlpha = smoothstep(0.5 - outline - sdfWidth, 0.5 - outline + sdfWidth, sdfValue);
        }
        let glowWidth = style.params.w;
        if (glowWidth > 0.0) {
            let t = clamp((sdfValue - (0.5 - glowWidth)) / glowWidth, 0.0, 1.0);
            glowAlpha = t * t;
        }
    } else {
        fillAlpha = 1.0 - smoothstep(-shapeWidth, shapeWidth, shapeDistance);
        let stroke = style.params.y;
        if (stroke > 0.0) {
            accentAlpha = 1.0 - smoothstep(stroke - shapeWidth, stroke + shapeWidth, abs(shapeDistance));
        }
        let glowWidth = style.params.w;
        if (glowWidth > 0.0) {
            // Measured from what is actually drawn, not from the shape's outline. A stroke-only
            // rectangle -- a border, the commonest shape in a composition -- has nothing in its
            // middle, and a glow measured from the outline would fill the whole picture with a
            // wash. So a hollow shape's glow hugs its stroke and a filled one's spreads outward.
            let filled = style.color.a > 0.0;
            let fromDrawn = select(max(abs(shapeDistance) - stroke, 0.0), max(shapeDistance, 0.0), filled);
            let t = clamp(1.0 - fromDrawn / glowWidth, 0.0, 1.0);
            glowAlpha = t * t;
        }
    }

    var colour = vec4<f32>(0.0);
    colour = over(colour, premul(vec4<f32>(style.glow.rgb, style.glow.a * glowAlpha)));
    colour = over(colour, premul(vec4<f32>(style.accent.rgb, style.accent.a * accentAlpha)));
    colour = over(colour, premul(vec4<f32>(style.color.rgb, style.color.a * fillAlpha)));
    return colour;
}
