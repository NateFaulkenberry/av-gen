# ADR-157: A number derived from the depth buffer needs the axis correction and the filter

**Status:** Accepted
**Date:** 2026-09-14
**Corrects:** `bf52530`, which made the waterline read the scene's depth and got two things wrong on
the way

## What happened

`bf52530` fixed a real bug: the shoreline fade, the foam band and the depth colour are all made of
the water's vertical depth under a pixel, and that depth was `in.uv.x` — a per-vertex baked value —
so every shoreline effect was quantised to the water mesh's tessellation and a river on a coarse
channel got a stair-stepped bank. Deriving it from the scene's depth is right, and the comment above
those three effects had claimed it for a long time.

It broke `test_water_depth_forensics_gpu.cpp`'s "water thickness does not depend on where in the
frame the water landed": the same patch of river, seen at the centre of the frame and off-axis from
the same eye, showed the bed through the water 5.7x differently. The suite said so on the first run
after the change and the failure was mine.

Two distinct mistakes, both of the same kind — treating a depth-buffer reading as if it were already
the quantity wanted:

1. **The axis is not the ray.** `viewDepth` is `dot(worldPos - cameraPos, cameraForward)` and the
   linear depth target is written the same way, so the difference between them is an interval along
   the camera's *axis*, not a length along the view ray. They agree only down the middle of the
   frame. The fix is one cosine: divide by `dot(cameraForward, rayDir)`. Nothing depended on the
   distinction before, because the grazing-angle cap was masking it.
2. **Nearest sampling is not a depth.** Adequate while the number only tinted the water; not
   adequate once the shoreline, the foam and the colour are all derived from it. The bed under a
   bank drops fast, so one texel is tens of centimetres of water depth, and at the QA scene's
   clarity of 1.25 m that is a visible step — and a number that changes when the camera turns,
   because the point lands on a different texel. `bedDepthAt` now does a 2x2 bilinear by hand
   (`r32float` is not filterable). This is also the smoother bank the original report asked for.

A third thing, found while fixing the first two and worth recording separately: the 0.15 floor
`bf52530` put on `abs(v.y)` was not conservative, it was wrong twice over. The product
`thickness * abs(v.y)` is self-correcting — a shallower ray travels proportionally further before it
reaches the bed — so flooring the multiplier overstates the depth at exactly the grazing angles it
was meant to protect. And it silently disabled the guard three lines below, which caps thickness at
`6 * vertical`: with a floor of 0.15, `6 * vertical` stays above `thickness` at every angle. One
floor in the wrong place turned off a cap nobody had touched.

## The rule

A quantity derived from the linear depth target is not finished when it is read. It is in axis
space, and it is quantised. If it is going to drive anything the eye can see a step in, it needs the
cosine and it needs a filter — and the test that catches the omission is a comparison of the same
world point at two places in the frame, from a fixed eye.

## Evidence

`worstRatio` on the forensics test: 5.70 before, 4.31 with the axis correction alone, under the 2.0
threshold with the filter as well. Full GPU suite 277 passed / 1 skipped / 0 failed; CPU suite 1581
cases, 493,608 assertions, 0 failed.
