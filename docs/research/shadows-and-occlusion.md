# Shadows, contact and ambient occlusion

Status: research (2026-09-09). Decision: ADR-034.

## 1. Why this is first

Nothing else in this phase matters as much. Without shadows there is no contact, no separation
between objects, no sense of form, and no way for light to describe geometry. Every "mid-grade CG"
frame in the current examples shares that one cause.

## 2. Shadow maps for the key light

- **Cascaded shadow maps** (Engel 2006; Zhang et al. 2006) split the view frustum by depth and fit
  a map per slice. Three or four cascades over a practical range are standard. Fitting must be
  stabilised (snap the light-space origin to texel increments) or the shadow edges crawl when the
  camera moves, which is exactly the temporal instability this phase must avoid.
- **Filtering**: percentage-closer filtering with a Poisson or rotated disc gives a soft edge for
  little cost. **Percentage-closer soft shadows** (Fernando 2005) estimate a blocker distance and
  widen the filter with it, which is what makes an area light read as an area light. PCSS is the
  right default for a single hero light; plain PCF for the rest.
- **Bias**: normal-offset bias (offset the sample position along the surface normal by a
  texel-scaled amount) avoids most acne and peter-panning; slope-scaled depth bias handles the
  rest. (Sources: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps ; Persson, "Shadow Filtering for Soft Shadows".)
- **Point and spot lights**: cube maps are expensive; for this engine, spot lights get a single
  perspective map and point lights get either a cube map at low resolution or no map at all,
  relying on contact shadows and occlusion instead.

## 3. Screen-space contact shadows

A short ray march in screen space against the depth buffer, from the shaded point toward the
light, catches the small-scale contact that a cascade at world scale always misses: an object
resting on a floor, a pipe against a wall, small parts of a machine against each other. Cheap
(8 to 16 steps), no extra buffers beyond depth, and it composes with the shadow map by taking the
minimum. Used widely since Unreal 4.19's "contact shadows" and Frostbite's equivalent.

## 4. Ambient occlusion

- **GTAO** (Jimenez et al., "Practical Realtime Strategies for Accurate Indirect Occlusion",
  SIGGRAPH 2016) is the current quality/cost sweet spot: horizon-based, ground-truth calibrated,
  with an optional bent normal that also improves specular occlusion.
  (https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf — accessed 2026-09-09.)
- Needs depth and a normal buffer, which the engine does not currently produce. That is the
  argument for auxiliary passes (ADR-035) before AO.
- Must be temporally filtered or it boils; deterministic per-frame noise plus a reprojected
  history is the standard answer, and determinism is a hard requirement here.

## 5. Order of work

Depth and normal targets, then cascaded shadows for the key light, then contact shadows, then
GTAO. Each step is independently visible in a frame, which suits the "render and look" loop this
phase demands.
