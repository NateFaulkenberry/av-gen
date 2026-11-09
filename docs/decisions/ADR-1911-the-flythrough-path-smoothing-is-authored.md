# ADR-1911: The flythrough's path smoothing is authored

**Status:** Accepted (Strange Attractor pass 3, the A-to-B camera paths)
**Date:** 2026-11-05
**Relates to:** ADR-1854 (the flight's height and aim), ADR-1909 (the flight's gaze), 61- §3

## Problem

The attractor world's flight follows the gorge's centreline, smoothed over 0.6 s, so it takes every bend. On the
canyon leg that meant five turn reversals and a look that swayed at about 20 deg/s. The owner's rule (61- §3) is one
straight line or one gentle curve from A to B.

## Decision

`flightSmooth` (seconds, default 0.6, range 0.05-20) sets the window that smooths the flight's horizontal path before
the clearance lift. It is hashed and written only when it differs from the default, so worlds that do not set it are
unchanged.

The canyon uses 4 s: no reversals, look yaw 5.6 deg/s p95, and the crossing stays on its frame with the approach
retimed (13.2 m/s from 20.5 s over 4.5 s). The clearance lift still runs after the smoothing, so a cut corner rises
rather than clips.

Test: `the flythrough's path smoothing is authored: a longer window turns the gorge's bends into one curve`.
