"""One module per Sonic VFX scene. Each defines `build() -> kit.Scene`.

SCENES is the SET LIST: the Examples menu lists the scenes in this order, and the live scene switcher (ADR-1063: the
Live panel's Scene row, PageUp/PageDown, MIDI program change n -> scene n mod count) steps through Sonic Live and then
these, in this order. It runs from the quiet and atmospheric to the cosmic finale.
"""

SCENES = [
    "salt_flat",            # 1  dusk, the sky as the instrument: a calm opening
    "breathing_deep",       # 2  organic, slow
    "cymatic_plate",        # 3  precise, scientific
    "ferrofluid_crown",     # 4  sculptural
    "tesla_choir",          # 5  electric: energy rising
    "corrupted_cathedral",  # 6  glitch, intense
    "event_horizon",        # 7  the cosmic finale
]
