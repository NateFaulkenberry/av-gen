---
id: start/welcome
title: Welcome to AV Gen
category: Getting Started
summary: What AV Gen is, what it is for, and the three things worth knowing before you touch anything.
order: 0
audience: beginner
tags: introduction, overview, getting started
keywords: what is av gen; where do i start; i am new here; how does this work
related: start/how-it-works, start/interface, start/projects
---

# Welcome to AV Gen

AV Gen is a real-time audiovisual engine. You load a piece of music, build or generate a world,
connect properties of that world to what the music is doing, and either watch it live or render it
to a file.

It is a native application. Everything here runs on your machine, offline, on the GPU.

## Three things worth knowing first

**Sound drives the picture through *signals*.** AV Gen analyses the audio continuously and
publishes the results as named values — `audio.bass`, `audio.onset`, `beat.phase`. You connect
those to things you can see. That connection is called a **route**, and it is the centre of the
whole application. See [How AV Gen works](help://start/how-it-works).

**Almost everything you can see is a *parameter*.** A parameter has a path (`post/bloom/intensity`),
a type, a range and a default. If something is a parameter you can set it by hand, save it in a
project, animate it on the timeline, drive it from the music, or bind it to a MIDI knob. See
[Parameters](help://modulation/parameters).

**The world is the canvas.** The editor is a dockspace with panels around the edges and the world
rendered in the middle. Panels can be closed and reopened from the **View** menu; the world cannot.
See [The interface](help://start/interface).

## Where to go next

| If you want to | Read |
|---|---|
| Understand the whole pipeline in one page | [How AV Gen works](help://start/how-it-works) |
| Find your way around the editor | [The interface](help://start/interface) |
| Make something move with the music | [Modulation recipes](help://modulation/recipes) |
| Know what the analyser measures | [Audio analysis](help://audio/analysis) |
| Render a file | [Offline rendering](help://rendering/offline-render) |
| Work out why a scene is slow | [Diagnosing performance](help://performance/diagnosis) |

> [!NOTE]
> This Help system documents what AV Gen actually does. Where an area is still being built, you
> will find a topic that says so plainly rather than a description of something that does not
> exist. Those topics are listed under **Not Yet Documented**.
