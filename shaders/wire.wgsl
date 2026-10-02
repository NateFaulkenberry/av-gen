// ADR-1073: a surface's edges as screen-space line quads. Included by procedural.wgsl after the surface's
// own vertex chain, so `vs_proc_wire` runs exactly the deformer stack, instancing, wind and effect
// displacement `vs_proc` runs, on both endpoints of every edge (scene/wire_edges.hpp builds the list).
//
// A vertex carries its endpoint and the other one; the quad is expanded across the projected segment by
// half the line's width plus one pixel of antialiasing margin, and lengthened by the same at each end so
// the joins of a polyline overlap. Both points are pulled towards the camera by a small share of their
// distance, so a line on a surface's own edge passes the depth test against that surface while one on the
// far side of a solid stays hidden.

struct WireVertexIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) corner: vec2<f32>,       // x = side (-1 / +1), y = +1 at endpoint A, -1 at B
    @location(3) otherPosition: vec3<f32>,
    @location(4) otherNormal: vec3<f32>,
};

struct WireVertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) across: f32,                         // -1 .. 1 across the quad
    @location(2) @interpolate(flat) widths: vec3<f32>, // x = the quad's half width (px), y = the line's, z = coverage
};

const WIRE_PULL_RELATIVE: f32 = 0.003;
const WIRE_PULL_ABSOLUTE: f32 = 0.002;

fn wirePulled(p: vec3<f32>) -> vec3<f32> {
    let toCamera = frame.cameraPos.xyz - p;
    let d = length(toCamera);
    if (d < 1e-5) {
        return p;
    }
    return p + toCamera * ((WIRE_PULL_RELATIVE * d + WIRE_PULL_ABSOLUTE) / d);
}

// `width1080` is the line's width in pixels at 1080 lines; it scales with the frame like every other
// authored pixel size.
fn wireExpand(pThisIn: vec3<f32>, pOtherIn: vec3<f32>, corner: vec2<f32>, width1080: f32) -> WireVertexOut {
    var out: WireVertexOut;
    let pThis = wirePulled(pThisIn);
    let pOther = wirePulled(pOtherIn);
    var cThis = frame.viewProj * vec4<f32>(pThis, 1.0);
    var cOther = frame.viewProj * vec4<f32>(pOther, 1.0);
    // An endpoint behind the eye is moved along the edge to just in front of it; an edge entirely
    // behind is collapsed (both its triangles become degenerate and draw nothing).
    let nearW = 1e-3;
    if (cThis.w < nearW && cOther.w < nearW) {
        out.clip = vec4<f32>(0.0, 0.0, -2.0, 1.0);
        out.worldPos = pThis;
        out.across = 0.0;
        out.widths = vec3<f32>(1.0, 0.0, 0.0);
        return out;
    }
    if (cThis.w < nearW) {
        let k = (nearW - cThis.w) / (cOther.w - cThis.w);
        cThis = mix(cThis, cOther, k);
    } else if (cOther.w < nearW) {
        let k = (nearW - cOther.w) / (cThis.w - cOther.w);
        cOther = mix(cOther, cThis, k);
    }
    let half = frame.targetSize.xy * 0.5;
    let sThis = cThis.xy / cThis.w * half;
    let sOther = cOther.xy / cOther.w * half;
    var dir = (sOther - sThis) * corner.y; // A -> B at both ends
    let len = length(dir);
    dir = select(vec2<f32>(1.0, 0.0), dir / max(len, 1e-6), len > 1e-4);
    let perp = vec2<f32>(-dir.y, dir.x);
    let widthPx = max(width1080, 0.0) * frame.targetSize.y / 1080.0;
    let lineHalf = max(widthPx, 1.0) * 0.5;
    let quadHalf = lineHalf + 1.0;
    let offset = perp * (corner.x * quadHalf) - dir * (corner.y * quadHalf);
    out.clip = vec4<f32>(cThis.xy + offset / half * cThis.w, cThis.zw);
    out.worldPos = pThis;
    out.across = corner.x;
    out.widths = vec3<f32>(quadHalf, lineHalf, clamp(widthPx, 0.0, 1.0));
    return out;
}

// The line's coverage of this fragment: 1 inside it, a one-pixel ramp at its edge, and its share of a
// pixel when it is thinner than one.
fn wireCoverage(in: WireVertexOut) -> f32 {
    let px = abs(in.across) * in.widths.x;
    return clamp(in.widths.y + 0.5 - px, 0.0, 1.0) * in.widths.z;
}
