"""One module per Sonic VFX scene. Each defines `build() -> kit.Scene`.

SCENES is the SET LIST: the Examples menu lists the scenes in this order, and the live scene switcher (ADR-1063: the
Live panel's Scene row, PageUp/PageDown, MIDI program change n -> scene n mod count) steps through Sonic Live and then
these, in this order. It runs from the quiet and atmospheric, through the organic and the precise, into energy and
glitch, to the cosmic finale -- and no two neighbours share a palette family.
"""

SCENES = [
    "salt_flat",            # 1  dusk, the sky as the instrument: a calm opening
    "lantern_lake",         # 2  dusk on water, warm lanterns
    "aurora_tundra",        # 3  night, cold light overhead
    "breathing_deep",       # 4  organic, slow: the living cavern
    "abyssal_bloom",        # 5  the deep sea
    "cymatic_plate",        # 6  precise, scientific
    "silk_theatre",         # 7  a dancer's line in the air
    "ferrofluid_crown",     # 8  sculptural, magnetic
    "feedback_mirror",      # 9  the loop: digital and hypnotic
    "tesla_choir",          # 10 electric: harmony as lightning
    "datascape",            # 11 data, monochrome and red
    "ember_forest",         # 12 after the fire
    "corrupted_cathedral",  # 13 glitch, intense
    "storm_cell",           # 14 violent weather
    "stellar_nursery",      # 15 cosmic: stars igniting
    "event_horizon",        # 16 the cosmic finale
]

# SONIC ABSTRACT (04-brief-abstract-direction.md, docs/prototypes/sonic-garden/ABSTRACT-PLAN.md): the abstract
# direction's nine prototypes (as amended by briefs 05-08), each a distinct visual language. Their own project (the owner's decision, 2026-10-02):
# examples/sonic-abstract/, the index category "Sonic Abstract", built by tools/sonic_vfx/abstract.py `build`. Numbered
# as the owner's brief numbers its directions.
ABSTRACT = [
    "sacred_flight",            # 1 Sacred Geometry Flight: The Golden Passage (05-brief-direction-correction.md)
    "neon_vector",              # 2 Neon Vector World: Pulsar Plain
    "cel_dream",                # 3 Cel-Shaded Dream World: Candy Archipelago
    "color_geometry",           # 4 Infinite Color Geometry: Chromatic Corridor
    "organic_garden",           # 5 Organic Digital Garden: Lantern Reef
    "digital_alpine",           # 6 Digital Alpine: Glass Caldera (07-brief-digital-alpine.md)
    "chromatic_topography",     # 7 Chromatic Topography: Contour Valley (05-brief-direction-correction.md)
    "glitch_signal",            # 8 Glitch Signal: Pixel Canyon (06-brief-glitch-signal.md)
    # 9 bit_ocean                8-Bit Ocean (08-brief-8bit-ocean.md): being built
]
