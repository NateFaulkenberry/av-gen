"""The portal rim particles (Art Pass 2, M3b; ADR-1898), shared by every world's builder.

A particle node with "portalRim": true is placed by the engine on the scene's live portal: a ring (the disc's inner
radius keeps the centre clear) the size of the opening, its spawn following the field and the opening by the camera's
distance, stopping in the last stretch to the plane (its larger side) so the rim has cleared when the camera passes through. Sparks and
shards streaming into the opening, turbulent: the rim reads chaotic as the camera nears, the destination still seen
through the middle.
"""


def rim(color=(1.0, 0.5, 0.16), hot=(1.0, 0.8, 0.45), size=0.05, speed=(1.0, 4.0)):
    return {
        "enabled": True, "capacity": 7000, "seed": 91, "shape": "disc", "position": [0, 0, 0],
        "extent": [1.0, 0.1, 1.0], "innerRadius": 0.72, "direction": [0, 0, -1], "spawnRate": 3200.0,
        "lifetimeMin": 0.3, "lifetimeMax": 0.75, "spread": 0.55, "speedMin": speed[0], "speedMax": speed[1],
        "gravity": [0, 0, 0], "drag": 1.2, "turbulence": 3.5, "turbulenceScale": 0.35,
        "sizeStart": size, "sizeEnd": size * 0.15, "sizeVariance": 0.6,
        "colorStart": [*hot, 0.85], "colorEnd": [*color, 0.0], "emissive": 3.0, "blend": "additive",
        "velocityStretch": 0.8, "stretchMax": 1.5}


RIM_PARTICLES = rim()
RIM_PARTICLES_MOON = rim(color=(0.85, 0.7, 0.4), hot=(0.95, 0.9, 0.75))
RIM_PARTICLES_SEA = rim(color=(0.3, 0.75, 1.0), hot=(0.7, 0.95, 1.0), size=0.12)
