# ADR-068: Mouse control and picking in the viewport

Status: accepted
Date: 2026-09-10

## Context

Every mouse event went to ImGui and nowhere else. The camera could only be moved by typing numbers
into the inspector, and there was no way to click on anything in the scene at all. That is a fine
way to *set* a shot and an impossible way to *find* one, and it is the reason the editor built in
ADR-066 was reachable but not operable.

## Decision

### The camera lives in parameters, not in a controller

`applyDrag`, `applyDolly` and `frameSphere` take an eye and a target and return an eye and a target.
They touch no parameters, no engine, no SDL and no ImGui. The application reads
`camera/position` and `camera/target`, applies the gesture, and writes them back.

A viewport camera holding its own copy of the pose would be a second source of truth for two vectors
that already save, load, automate and route -- and the two would disagree the first time anything
else moved the camera, which in this engine is routine: a cinematic shot, an automated parameter and
a generated world all move it.

The consequence worth stating loudly: **`camera/position` and `camera/target` are read only in free
mode.** A drag in orbit mode moves both parameters and changes nothing on screen. So the application
switches the camera to free mode on the first gesture and says so in the log, because silently
moving nothing is indistinguishable from a dead input -- a mistake already made once in this project
when `installWorld` framed a camera without setting the mode and the frame came out byte-identical.

### Picking reuses the targets the scene pass already writes

The identifier target (ADR-035) records which entity covered each pixel; the linear-depth target
records how far its surface was along the camera's forward axis. Between them they answer both
questions a click asks -- *what* is under the cursor and *where in the world* it is -- so picking
adds nothing to the per-frame cost and needs no new pass. The reads happen on click.

They are single-texel reads. Reading the whole identifier target to get four bytes is five megabytes
and a GPU stall.

Two details are easy to get wrong and both are load-bearing:

- The stored depth is distance along the **forward axis**, not along the ray. Travelling it along
  the ray lands short by a cosine that is zero at the centre of frame and grows toward the corners,
  which reads as "picking is a bit off near the edges" rather than as a missing division.
- SDL reports mouse positions in points; the targets are in pixels. On a retina display those differ
  by two, and skipping the conversion puts every click in the top-left quarter of the frame.

### A click is answered after the next frame

The event handler records the pixel; the pick runs after the frame is submitted. Answering inside
the handler would read the *previous* frame's targets with the *current* frame's camera matrices --
the two disagree by exactly one frame of camera motion, so picks would be right when the camera is
still and wrong while it is moving.

### A pick selects a node, not an entity

The identifier target records a scene entity index, and a person selects the node they placed.
`Composition::nodeForEntity` holds that mapping, because the composition already flattens nodes into
contiguous entity ranges. A separate table built beside it would drift the first time a node stopped
emitting geometry.

Geometry no node owns -- terrain chunks, procedural instances -- returns a position and no node.
That is expected rather than broken, and the position is still what placement needs.

## Consequences

- `gpu::readTextureRaw` takes an origin; two single-texel readers are exposed beside it.
- `WorldSelection` gains a `Node` kind, whose parameters live under `nodes/<name>/`.
- A drag that starts on the scene keeps the mouse until the button is released, even if the cursor
  passes over a panel. Letting a panel steal a gesture halfway through makes an orbit stop mid-swing.
- Bindings: left orbits, shift-left or middle pans, right looks, the wheel dollies, and a left click
  that did not travel picks. A click on the sky deselects, which is what a click on nothing means
  everywhere else.

## What this does not do

Placement. Picking answers "where in the world did I click", which is the hard half, but dropping an
asset there is a separate change and is not in this one.
