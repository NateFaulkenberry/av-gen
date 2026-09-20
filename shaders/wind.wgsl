// The frame's wind (ADR-055). The arithmetic is in `wind_field.wgsl` and is not repeated here;
// this file is the one thing that file deliberately cannot do, which is read the frame uniforms.
//
// Reads frame.windDir / windRegion / windGust / windTurb (common.wgsl), so it can only be included
// by a module that already includes common.wgsl. A module without a frame group -- `particles.wgsl`
// is the one -- includes `wind_field.wgsl` instead and passes the copy of the field its own
// uniforms carry. That is the whole of ADR-370's "one description of the air": two callers, two
// sources of the same sixteen floats, one function.

#include "wind_field.wgsl"

// The frame's packed field, in the form `wind_field.wgsl` takes. `FrameUniforms` stores the four
// vectors flat rather than as a struct, so this is where they are gathered.
fn frameWindField() -> WindField {
    var w: WindField;
    w.dir = frame.windDir;
    w.region = frame.windRegion;
    w.gust = frame.windGust;
    w.turbulence = frame.windTurb;
    return w;
}

// What the air is doing at a world position at a time, for a shader that has the frame group.
fn windSampleAt(p: vec3<f32>, t: f32) -> WindSample {
    return windSampleFrom(frameWindField(), p, t);
}
