AV Gen · Live performance

# Live Scene Guide

How to get sound and MIDI into AV Gen's live scenes, what you can steer from the Live panel and a controller, and how each scene answers what you play.

[Setup](#setup)[Playing & the transport](#transport)[Live panel](#panel)[MIDI everywhere](#midi)[Signals](#signals)[Scenes](#scenes)

01 · Before you play

## Route your music into AV Gen

AV Gen listens to an audio *input*. To feed it the music you are playing out of this Mac and still hear it, send the music to a **Multi-Output Device** that plays through your speakers and copies into **BlackHole 2ch**, then pick BlackHole as AV Gen's input.

1. **Create the Multi-Output Device** (once). Open *Audio MIDI Setup* (Applications ▸ Utilities), click **+** ▸ *Create Multi-Output Device*, and tick your real output (speakers, headphones or interface) and **BlackHole 2ch**.
   - *Primary Device* (the clock): your real output.
   - *Drift Correction*: on for BlackHole only.
   - Both devices at the same sample rate; 48 kHz is safest.
2. **Send your music to it** (each session): System Settings ▸ Sound ▸ Output ▸ *Multi-Output Device*, or choose it as the output inside your DAW or player.
3. **In AV Gen**: View ▸ Live ▸ tick **Enabled** under Live Sonic Input, set **Audio input** to *BlackHole 2ch*. The *Audio receiving* light comes on and the dBFS meter moves; raise **Input gain** if it sits low.
4. **MIDI**: plug in the controller or keyboard; *MIDI input* in the Live panel picks which device to listen to (all, by default). The *MIDI receiving* light shows the last note.

**Keep the Multi-Output steady.** Only put devices in it that stay plugged in: an interface that is unplugged while it is a member (the Volt, say) leaves the device in a broken, crackly state until Core Audio restarts (`sudo killall coreaudiod` in Terminal fixes it without a reboot). Make the clock your real output, never BlackHole. AV Gen never changes the buffer size of a device another app is using, so opening or closing it doesn't disturb what you are playing.

**Gotchas.** The keyboard volume keys do nothing on a Multi-Output Device: set the level in your music app or interface. Crackle or drift usually means mismatched sample rates, or drift correction on the wrong device. Switch the system output back to your speakers when you finish.

02 · What moves when

## Playing a live scene, and the transport

A live scene has two clocks. Everything that answers your sound and MIDI runs on the wall clock, all the time. The scene's **timeline**, which in most scenes is the camera's flight, moves only while the **transport** plays.

#### Runs whether or not the transport plays

- every audio- and MIDI-reactive change (kick, snare, bass, notes, the character of the sound)
- particles and simulations
- the beat clock (from the audio, or MIDI clock)
- stage machines (Digital Mosh, THE RIFT) and the Astral Forge conductor

#### Moves only while the transport plays

- the camera's path in Sonic Live, every Sonic Abstract scene and the Sonic VFX scenes
- in some scenes, the travel through the world, or the paths that draw the main figure
- stopped, these scenes still react, but the view stands still

**Start projection** starts the transport for a live scene, so the camera moves from the first frame, and **Stop projection** stops it. A live scene's transport is **endless**: it plays past the end of its timeline and its camera paths loop seamlessly, so it keeps moving until you stop. Switching scenes while the picture moves (playing or projecting) starts the next scene playing as soon as it loads. Without a projection, Space or the transport bar's Play starts it.

Each scene below carries a badge: camera follows the transport or moves on its own.

03 · View ▸ Live

## The Live panel

Everything on the panel, top to bottom. The *Response* group shapes how every live scene hears the music: Sensitivity, Transients and Sustain set how readily it reacts to quiet material, hits and sustained sound; Attack and Release set how fast reactions rise and fall. At the middle settings each scene looks as it was designed; raise Sensitivity if your input runs quieter than a mastered mix.

| Control | What it does | Range | Default |
| --- | --- | --- | --- |

04 · Any controller

## MIDI that works in every live scene

Each scene adds its own mappings (on its card below). These hold everywhere:

| Control | What it does |
| --- | --- |

### Program change → scene

05 · Vocabulary

## What the scenes listen to

The scene cards name the signals each reaction reads. In plain terms:

| Signal | What makes it rise |
| --- | --- |

06 · The set

## Every live scene

In the order of the Live panel's Scene list (and the program-change numbers). Screenshots are offline renders at 1:36 into *Trench*, with no MIDI, so note-driven elements are at rest.

No scene matches that search.

Generated from the AV Gen repository (main, 2026-10-26): each scene's project file (its MIDI bindings, routes and authored vocabulary) and the engine source. Mappings are what the files say; a few the source could not confirm are marked "unverified".