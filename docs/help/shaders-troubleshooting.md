---
id: shaders/troubleshooting
title: When a Shader Will Not Compile
category: Shaders
summary: What the magenta stripes mean, where the error text is, and how line numbers map back to your file.
order: 62
tags: shader, error, compile, debug, magenta
keywords: magenta stripes; shader error; my shader is pink; shader wont compile; where is the shader error
related: shaders/overview, shaders/uniforms, troubleshooting/rendering
features: subsystem.shaders
---

# When a Shader Will Not Compile

## The magenta stripes

Diagonal animated magenta stripes mean **this layer has never compiled successfully.** The engine
falls back to an error shader built with the same generated bindings, so the layer still draws
something rather than breaking the frame.

If a layer *had* compiled and a later edit fails, the previous working pipeline is kept and the
picture does not change. That is deliberate — a typo mid-session should not blank your scene — but
it does mean **the picture not changing is not proof the edit took**. Check the Shaders tab.

## Where the error is

**Control ▸ Shaders**. Each layer shows its name, stage, input and pass counts, its file path, and
— in red, word-wrapped — the parse error if the header was malformed, or the compile error
otherwise. The tab header counts reloads for the session.

A compile error looks like:

```
shader 'user:myLayer#3' failed to compile:
  user:myLayer#3:41:9: error: unresolved identifier 'sys_time'
```

## Line numbers

The number in the message is a line in the **generated** module, which has a prologue above your
body. The prologue's first line states how many lines it is:

```
// avgen user shader 'myLayer': 38 prologue lines; body starts at line 39
```

Subtract that count to get the line in your file. If your file has a JSON header, the body starts
after it, and the offset is taken care of.

## Common causes

| Message | Usually means |
|---|---|
| `unresolved identifier` | a Shadertoy or GLSL name — `iTime`, `fragColor`, `texture()` |
| `no matching overload` | a WGSL type mismatch; WGSL will not implicitly convert `f32` and `i32` |
| the header will not parse | trailing comma, or the block comment is not the very first thing in the file |
| the layer draws nothing at all | a background layer with `discard`, or a returned alpha of 0 |

## Things that are not shader errors

**A parameter you declared is missing from the Parameters panel** — shader inputs appear under the
`shader` group, and the panel's authoring layer shows that group at every level, so check the
layer's name rather than the layer filter.

**The shader runs but nothing reacts** — check that a route exists to
`shader/<layer name>/<input>`, and remember the audio lanes in `sys` read 0 when no audio is
playing.

**The engine's own shaders will not reload** — only five engine shaders are watched directly. See
[Writing a shader layer](help://shaders/overview).

> [!WARNING]
> Shaders are read from disk at run time, and the uniform structs they share with C++ are compiled
> into the binary. If you pair a rebuilt shader tree with an older binary, pipeline creation fails
> with a `minBindingSize` mismatch. AV Gen warns at startup when the shaders on disk are newer than
> the executable.
