// One particle pool record (ADR-015), 64 bytes: rendering/particle_renderer.cpp's kParticleStride.
// Its own file because two modules read the pool -- particles.wgsl, which owns it, and
// particle_density.wgsl (ADR-1141), which splats it -- and two copies of a layout drift.
struct Particle {
    position: vec3<f32>,
    age: f32,
    velocity: vec3<f32>,
    life: f32,       // 0 = dead
    seed: f32,
    size: f32,
    trailWrites: f32, // history samples written since birth (ADR-040); 0 at emit
    // ADR-520: which life the particle is living. 0 = the primary one, 1 = the splash ring it
    // became when it hit the ground. This was `pad`, so the struct's size and layout are
    // unchanged and every existing pool is bit-identical: a system with no collision response
    // writes 0 here exactly where it used to write 0 there.
    stage: f32,
    // Scatter-anchored clusters (scene::ScatterAnchor): the crown centre this particle was born
    // round, w = 1. It is kept per particle rather than looked up in the table each step because
    // the table follows the camera: a tree that leaves it must not drag its swarm across the valley
    // to whichever tree took its slot. All zero for every other system, and then the attractor
    // below is `params.attractor`, exactly as it always was.
    home: vec4<f32>,
};
