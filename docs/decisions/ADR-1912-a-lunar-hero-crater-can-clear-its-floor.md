# ADR-1912: A lunar hero crater can clear its floor

**Status:** Accepted (Strange Attractor pass 3, owner note 2026-11-06)
**Date:** 2026-11-06
**Relates to:** ADR-1882 (the Moon), ADR-1883 (the crater-floor portal), 65- §2

## Problem

The hero craters are stamped first and the small population overprints them. On this seed, a small crater's rim landed
on the portal crater's floor and raised a lump about 15 m high beside the opening. The corruption's wire and glyphs
drew over it, so the owner saw a blob where the portal opens.

## Decision

A hero may set `clear` (metres). Once all the craters are stamped, each texel inside that radius becomes the mean of
its ring round the hero's centre, blended back to the ground over the outer 30%. The bowl's own shape stays and
anything printed over it goes. Absent, or 0, the world and its hash are unchanged.

The portal crater uses `clear` 125 m (its radius is 130 m). The portal's opening, wire and glyphs are unchanged.

Test: `a lunar hero's clear radius removes what overprinted its floor (ADR-1912)`.
