"""The Living Prism: the musical behaviour (Stage 4), as conditioned signals (ADR-1602) and the routes that read them.

Every musical idea is named once, as `signal.<name>`, with the separated stem first and the main mix as its fallback
(`firstActive`); routes read only signals, never a stem or the mix directly. Nothing reads the mix's kick, snare or
hat classes (the owner's option A): impacts come from the drum stem's bands, else the mix's broadband onset.

The drivers (05 section 5, made real), measured on Feline Footwear's drum stem (07 section 4):
  hit      the kick: the drum stem's bass band jumps 0 -> ~0.67 on every kick. A sharp flash (0 attack, 160 ms decay)
  front    the same kick as a latching linear fall (one beat long): it drives a travelling front's radius
  accent   the clap: the drum stem's high-mid band, ~0.8 on the backbeat, <= 0.18 elsewhere
  glint    the hats: the drum stem's treble, fast in and out
  bass     the bass notes: each note a weighty push (the bass stem's onsets, a linear fall, its level as depth)
  synth    the synth's light: its level over about a second (it is pumped by the kick, so smoothed through it)
  bright   the synth's brightness: its centroid over about two seconds
  energy   the three stems' overall level over several seconds (the mix's level as fallback)
  section  energy accumulated over a phrase: the slow evolution
MIDI: CC 1 boosts the impacts (hit, front, accent, glint) up to 2x, CC 2 the world's broad response (section);
at rest (no controller, or 0) the authored strength. CC 9 selects a look, CC 10 locks it.
"""

# a stem input, gated on the stem being present (so a project without it falls back) -- ADR-1602
def _stem(src, chain, **kw):
    d = {"source": src, "chain": chain}
    d.update(kw)
    return d


# A control channel reads 0 until its controller first moves (ControlSource has no rest value), so a CC is a BOOST:
# at rest the authored strength, at full twice it (07 section 4: the engine gap, with soft takeover P1-12).
IMPACT_DEPTH = {"depthSource": "control.impact", "depthMin": 1.0, "depthMax": 2.0, "depthDefault": 0.0}
WORLD_DEPTH = {"depthSource": "control.world", "depthMin": 1.0, "depthMax": 2.0, "depthDefault": 0.0}


def signals():
    clampc = {"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}
    S = []

    def sig(name, inputs, chain=None, combine="firstActive"):
        s = {"kind": "signal", "name": name, "settings": {"combine": combine, "inputs": inputs}}
        if chain:
            s["settings"]["chain"] = chain
        S.append(s)

    sig("hit", [
        _stem("stem.drums.bass", dict(clampc, gain=2.2, offset=-0.45, attackMs=0.0, decayMs=160.0), **IMPACT_DEPTH),
        dict({"source": "response.onset", "chain": {"attackMs": 0.0, "decayMs": 160.0}, "amount": 0.8}, **IMPACT_DEPTH)])
    sig("front", [
        _stem("stem.drums.bass", dict(clampc, gain=2.2, offset=-0.45, envelope="linearfall",
                                       envelopeFallPerSecond=1.8, envelopeRefractoryMs=300.0, envelopeRearm=0.25),
              **IMPACT_DEPTH),
        dict({"source": "response.onset", "chain": {"envelope": "linearfall", "envelopeFallPerSecond": 1.8,
                                                    "envelopeRefractoryMs": 300.0}}, **IMPACT_DEPTH)],
        chain=dict(clampc))
    sig("accent", [
        _stem("stem.drums.highMid", dict(clampc, gain=2.6, offset=-1.25, attackMs=4.0, decayMs=300.0), **IMPACT_DEPTH),
        # on a mix there is no clap to claim: the strongest broadband onsets only, a little
        dict({"source": "response.onsetEnv", "chain": dict(clampc, gain=2.0, offset=-1.1, attackMs=4.0, decayMs=300.0)},
             **IMPACT_DEPTH)])
    sig("glint", [
        _stem("stem.drums.treble", dict(clampc, gain=2.6, offset=-0.4, attackMs=3.0, decayMs=110.0), **IMPACT_DEPTH),
        dict({"source": "audio.treble", "chain": dict(clampc, gain=2.0, offset=-0.8, attackMs=3.0, decayMs=110.0)},
             **IMPACT_DEPTH)])
    sig("bass", [
        _stem("stem.bass.onset", {"envelope": "linearfall", "envelopeFallPerSecond": 2.2, "attackMs": 0.0,
                                  "envelopeRefractoryMs": 130.0, "envelopeRearm": 0.05},
              depthSource="stem.bass.level", depthMin=-0.3, depthMax=1.6, depthDefault=0.5),
        {"source": "response.bass", "chain": dict(clampc, gain=2.4, offset=-1.3, attackMs=10.0, decayMs=450.0)}],
        chain=dict(clampc))
    sig("synth", [
        _stem("stem.synth.level", dict(clampc, gain=2.4, offset=-1.0, attackMs=250.0, decayMs=1000.0)),
        {"source": "response.sustain", "chain": dict(clampc, gain=2.4, offset=-0.95, attackMs=250.0, decayMs=1200.0)}])
    sig("bright", [
        _stem("stem.synth.centroid", dict(clampc, gain=5.0, offset=-2.6, attackMs=1500.0, decayMs=2500.0)),
        {"source": "audio.centroid", "chain": dict(clampc, gain=3.0, offset=-1.2, attackMs=1500.0, decayMs=2500.0)}])
    sig("energyStem", [
        {"source": "stem.drums.level", "amount": 0.35}, {"source": "stem.bass.level", "amount": 0.3},
        {"source": "stem.synth.level", "amount": 0.35}], combine="sum")
    sig("energy", [
        {"source": "signal.energyStem", "gate": "stem.synth.active",
         "chain": dict(clampc, gain=2.0, offset=-0.7, attackMs=2000.0, decayMs=5000.0)},
        {"source": "response.level", "chain": dict(clampc, gain=2.5, offset=-1.15, attackMs=2000.0, decayMs=5000.0)}])
    sig("section", [dict({"source": "signal.energy", "chain": {"attackMs": 8000.0, "decayMs": 16000.0}}, **WORLD_DEPTH)])
    return S


def routes(rock, floor, hero, xtal, crystal_nodes):
    """Every route reads a signal. `crystal_nodes` maps a role to its crystal nodes."""
    R = []

    def r(src, target, amount, chain=None, op="add", component=-1, **kw):
        d = {"source": "signal." + src, "target": target, "amount": amount, "op": op, "polarity": "unipolar",
             "component": component}
        if chain:
            d["chain"] = chain
        d.update(kw)
        R.append(d)

    def em(node):
        return "procedural/%s/material/emissive" % node

    # 1. the immediate impact (kick): the primary formation's core flares; the shore and the water flash once
    r("hit", em("spire_xtal"), 3.2)
    r("hit", "lights/spireGlow/intensity", 160.0)
    r("hit", "nodes/floor/water/sparkle", 0.6)
    # 2. the front: a ring of light leaves the spire on each kick and runs out across the shore, the rock and the
    #    formations (all four programs read the `front` field), fading as it goes
    r("front", "field/front/waveOrigin", 1.0, op="replace", chain={"gain": -13.0, "offset": 13.0})
    r("front", "field/front/amplitude", 1.0, op="replace")
    # the clap: the geode and the arch's crust answer, and the rock's seams flicker
    r("accent", em("geode_xtal"), 2.6)
    r("accent", em("arch_xtal"), 1.4)
    r("accent", "lights/geodeGlow/intensity", 40.0)
    r("accent", rock.path("seamGain"), 0.9)
    # the hats: the dense crystal's mirror facets glint
    r("glint", xtal.path("sparkleGain"), 2.6)
    r("glint", hero.path("sparkleGain"), 1.2)
    # 3. the bass: broad, slower -- the fan's great blades and the chandelier pulse, the passage breathes, the water
    #    lifts a few centimetres, the seams brighten; each note decays in a few hundred ms
    r("bass", em("fan_xtal"), 0.9)
    r("bass", em("chandelier_xtal"), 0.7)
    r("bass", "lights/passage/intensity", 220.0)
    r("bass", "nodes/floor/water/swell", 0.03)
    r("bass", rock.path("seamGain"), 0.35)
    # 4. the synth: the internal light of the many small crystals and the roof's wash; its brightness pales the
    #    terminations and lifts the saturation
    for node in crystal_nodes.get("secondary_small", []):
        r("synth", em(node), 0.5)
    r("synth", em("crust_xtal"), 0.55)
    r("synth", "lights/roofwash/intensity", 70.0)
    r("synth", "particles/motes/spawnRate", 120.0)
    r("bright", xtal.path("tipMix", "value"), 0.25)
    r("bright", hero.path("tipMix", "value"), 0.2)
    r("bright", "post/grade/saturation", 0.12)
    # 5. memory: the hits leave a lingering glow in the spire and the seams, released over seconds (bounded: a
    #    one-pole decay, no accumulation past its input)
    r("hit", em("spire_xtal"), 0.5, chain={"attackMs": 60.0, "decayMs": 2500.0})
    r("accent", rock.path("seamGain"), 0.4, chain={"attackMs": 80.0, "decayMs": 3000.0})
    # 6. the phrase: as the music builds, the whole grotto grows richer -- bloom, the primary's light, the haze
    r("section", em("spire_xtal"), 0.7)
    r("section", em("arch_xtal"), 0.5)
    r("section", "post/bloom/intensity", 0.18)
    r("section", "lights/backwash/intensity", 40.0)
    return R
