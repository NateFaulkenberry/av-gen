---
id: shaders/overview
title: Writing a Shader Layer
category: Shaders
summary: The one function your file must define, where it runs, and what the engine generates around it.
order: 60
tags: shader, wgsl, isf, layer, background, post, hot reload
keywords: how do i write a shader; wgsl; add a shader; shader layer; mainimage; can i use shadertoy
related: shaders/uniforms, shaders/troubleshooting, rendering/overview
features: subsystem.shaders, command.file.add-background-shader, command.file.add-post-shader
---

# Writing a Shader Layer

A user shader is a `.wgsl` file that defines exactly one function:

```wgsl
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    return vec4<f32>(uv, 0.0, 1.0);
}
```

`uv` runs 0 to 1 with **(0, 0) at the top left**. `fragCoord` is `uv` times the current pass's size
in pixels.

The engine generates everything else — the vertex stage, the bindings, the uniform structs and the
entry point. You write the body.

## Loading one

Four ways: **File ▸ Add Background Shader...** or **Add Post Shader...**, the Shaders tab's **Add
background...** and **Add post...** buttons, dropping a `.wgsl` file on the window (it becomes a
background layer), or `--shader file.wgsl` / `--post file.wgsl` on the command line. Both flags are
repeatable.

## Background versus post

A **background** layer draws into the HDR target before any geometry, so the scene covers it. Its
`inputImage` is a 1×1 black texture — there is nothing behind it to read.

A **post** layer draws after the scene and before the built-in post chain, ping-ponging the HDR
target. Its `inputImage` is the current scene image, which is what makes an effect possible.

## The header

An optional ISF-style JSON header at the top of the file, in a block comment, declares inputs and
passes:

```wgsl
/*{
    "INPUTS": [
        { "NAME": "amount", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 2.0 },
        { "NAME": "tint",   "TYPE": "color", "DEFAULT": [1.0, 0.4, 0.9, 1.0] }
    ],
    "PASSES": [
        { "TARGET": "blurred", "FLOAT": true, "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2" },
        { }
    ]
}*/
```

A file with no header is a shader with no inputs and one output pass.

**Input types**: `float`, `long` (an integer), `bool`, `color`, `point2D`, `event`. Each becomes a
field of the `inputs` uniform struct, in declared order.

**Every input also becomes a parameter**, at `shader/<layer name>/<input name>`. It is modulatable,
it is saved in projects and presets, and it survives a hot reload. This is the whole point: a shader
input is a first-class parameter and the music can drive it.

**Passes** run in order. A pass with a `TARGET` writes a texture of that name, which later passes
can read as a binding of the same name. `FLOAT` makes the target 16-bit float rather than 8-bit
unorm. `PERSISTENT` double-buffers it so a pass can read its own previous frame — which is how you
write feedback. `WIDTH` and `HEIGHT` accept `$WIDTH`, `$HEIGHT`, numbers, and one multiplication or
division.

## Hot reload

User shader files are watched, about four times a second, and recompiled when they change. Each
layer also has a manual **reload** button.

> [!NOTE]
> Engine shaders are watched too, but only five of them directly: `common.wgsl`, `pbr.wgsl`,
> `grid.wgsl`, `skybox.wgsl` and `tonemap.wgsl`. Editing `post.wgsl` or `lighting.wgsl` alone will
> not trigger a reload — though once any of those five changes, everything is rebuilt. Touching
> `common.wgsl` is the reliable way to force a full reload.

## Shadertoy and ISF

The header format borrows from ISF, and the `mainImage` shape borrows from Shadertoy, but **the
language is WGSL, not GLSL**. A Shadertoy shader has to be translated, not pasted.
