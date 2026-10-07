# Sonic Live: quickstart

How to play AV Gen with your MIDI keyboard and synth (01-brief-live.md PART 22). It takes about five minutes the
first time. After that, AV Gen remembers your devices.

## What you need

- The MIDI keyboard, connected to the Mac (USB, or a MIDI interface).
- The synth's audio reaching the Mac as an **input**. Pick the setup that matches your rig:

  | your synth | how its sound gets into AV Gen | what to pick in AV Gen |
  |---|---|---|
  | Hardware synth | Synth audio out into your audio interface's input (a line input, e.g. the Apogee's). Keep monitoring it as you do now. | That interface |
  | Software synth or DAW on this Mac | Set the DAW's or synth's output to **BlackHole 2ch** (installed on this Mac). To hear it as well, create a Multi-Output Device in Audio MIDI Setup (your speakers or headphones plus BlackHole 2ch) and output to that. | BlackHole 2ch |
  | Nothing else available | The built-in microphone, near a speaker. It works, but room noise and reverb blur the timbre. | MacBook Pro Microphone |

- The keyboard's MIDI must reach **both** the synth and AV Gen.
  - On macOS, several programs can listen to one USB keyboard at once, so a DAW and AV Gen can both use it.
  - With a hardware synth on 5-pin MIDI, use the keyboard's USB to the Mac for AV Gen, and MIDI out (or thru)
    to the synth.

## Steps

1. **Build and launch** (from the repository root):

   ```
   cmake --build --preset release
   build/release/src/avgen --example "Sonic Live"
   ```

   Or launch AV Gen normally and choose **File > Live Projects > Sonic Live** (under "Sonic Garden"). Opening the Sonic Live project
   turns live input on, starts the transport, and opens the **Live** panel. If you close the Live panel, reopen it
   from **View > Live**.

2. **Audio input:** in the Live panel, choose the input from the table above.
   - The first time, macOS may ask to allow microphone access. That permission covers every audio input,
     including interfaces and BlackHole, so allow it.
   - AV Gen remembers the choice.

3. **MIDI input:** leave it on **All MIDI inputs**. A keyboard plugged in at any time connects on arrival. Choose
   your keyboard by name only if another MIDI device would otherwise send notes too.

4. **Play a note.** Both lights in the Live panel should turn green:
   - **Audio receiving** means a signal above -60 dBFS. The meter shows its level.
   - **MIDI receiving** shows the last note, its velocity and channel, and how many notes are held.

5. **Set Sensitivity:** play hard and adjust until the meter peaks around -12 dBFS. The Sonic Character reads
   levels on fixed scales, so an input that is too quiet looks like a quiet sound.

6. **Play.** Try the brief's list: bass, pad, lead, pluck, FM, distorted, filtered, resonant, noisy, sustained,
   percussive. Then hold one note and sweep the synth's filter: the notes do not change, but the world should.
   The **Analysis** panel's **Sonic** section shows what AV Gen hears: the character as bars, the musical
   context, and the visual families. The next section says what each gesture should do.

## What to try (and what you should see)

The world has four faces, and the **sound** picks among them over about two seconds; the notes, the knobs and the
rhythm act inside whichever face is showing. So change one thing at a time, and give a new patch a couple of
seconds. In this order:

1. **Before you play:** a dark night, the ground's veins faintly lit. That is the waiting world. It grows into a
   world within a second of sound, and returns six to eight seconds after you stop (a pause keeps the world).
2. **A soft, low pad with the filter fairly closed, a few long notes:** the **warm garden** (a lotus round a seed
   of light, tendrils, mushrooms, a low orange sun). Each note swells the light and opens the lotus a little, low
   notes in the mushrooms' gills and the lotus, high ones in the tendrils' tips; the light lasts as long as your
   note does (a long release keeps it lit). Held, legato playing grows the tendrils and lengthens the trails.
3. **Hold one note and sweep the filter slowly, all the way up and back.** The most important one. With the
   filter closed the garden is dusk: dim, soft focus, thick air. As you open it the light rises, the air clears,
   the focus sharpens and the ground's veins light up, within about a third of a second of the knob. Leave it open
   and the world itself turns: the garden gives way to glass, and at the very top (a fully open saw is a raspy
   sound) the glass buzzes and throws off fragments. Close it and the garden grows back.
4. **Same note, now turn up the distortion or drive** (a drive AFTER the filter, like a pedal, shows best: a drive
   into a closed filter is filtered away before anyone hears it). First the light opens (more harmonics), then the
   forms start to buzz and crack and fragments fly (roughness); held, the world turns to bright, buzzing glass and
   the heavy world begins to rise behind it. On a single note distortion and an open filter sound alike, so they
   look alike at the top; play a chord or a riff through the distortion and it goes all the way to the heavy world
   (violet air, basalt, a fissured mass) within about two seconds.
5. **A bright pluck, an FM bell or a bright lead, played high:** the **glass observatory** (dark crystals, a gem in
   rings of light). Notes ring the rings: high notes swing the ecliptic ring, low notes the meridians. The gem and
   the rings rise with your register. A ringing sound keeps the rings lit; a dry pluck lets them go at once.
6. **A fast arpeggio (16ths):** the rings keep turning and swinging in the pattern of your notes, stars stream; in
   the garden, motes circle the seed and the tendrils ripple.
7. **Chords, then denser chords:** the world swells: the gem and the rings open wide and a cluster of small gems
   rises (in the garden, the lotus opens and glowing buds rise). Denser voicings, more of it.
8. **A distorted bass riff:** the heavy world: the fissured mass heaves on each note, basalt rises. The first
   seconds of the distortion shatter the world it was in.
9. **Noise, drums, a noisy percussive patch:** the **strike field**: black, one hard light, and each hit a white
   flash, a shock ring racing out along the ground and a burst of blades. A distorted high lead also lands here.
10. **Stop.** After six to eight seconds of silence the world returns to the waiting night.

If a patch lands in a world you did not expect, the Sonic section of the Analysis panel says why (for example a
bright saw pad reads as glass, a warm electric piano between the garden and the glass). The Smoothing control
makes all of this faster (below 1x) or calmer (above 1x); 1x is the demo's own live tuning.

## The rest of the Live panel

- **Smoothing:** how quickly the visuals follow the sound. Below 1x they react faster and more nervously; above
  1x they are calmer. 1x is the project's own tuning: in the Sonic Live demo that is already a live tuning (the
  world follows a new sound in about 2 s, the filter's light in about a third of a second), so start at 1x. AV Gen
  remembers the setting.
- **Anti-aliasing:** the live viewport's edge smoothing (the same setting as Settings > Rendering). For cleaner
  thin rings at some frame-rate cost, raise **Settings > Rendering > Lowest scale**.
- The grey lines at the bottom are measurements:
  - "MIDI -> frame" is how old a note is when the frame that shows it starts.
  - "audio analysis -> frame" is the same for the sound.
  - The timbre cost, and whether any analysis was dropped (it should stay 0).

## Projecting to a second screen

The **Start projection** button at the top of the Live panel puts the picture, with no panels, in a window of
its own. Send that window to a projector or any second screen (HDMI, USB-C, AirPlay).

1. **Connect the projector** before you press the button. In System Settings > Displays, use it as an extended
   display, not a mirror: when the screens are mirrored, the projector shows the editor too.
2. **Check the choices under the button.** AV Gen remembers them on this Mac, as it does your devices.
   - **Display:** Automatic picks the first screen that is not this Mac's own. You can also choose a screen by
     name. If the remembered screen is not connected, AV Gen says so and uses the automatic choice.
   - **Fullscreen:** on by default for another screen and off for this one. A fullscreen window on this Mac's own
     screen would cover the editor.
   - **Size** (when not fullscreen): Automatic is the other screen's full size, or half of this one.
   - **Fit / Fill / Stretch:** what happens when the screen's shape is not the picture's. Fit shows the whole
     picture with black bars. Fill fills the screen and crops the picture's edges. Stretch distorts the picture.
3. **Press Start projection.**
   - If the open project is not a live one, AV Gen opens the Sonic Live demo first. If the current project has
     unsaved changes, it asks about them first, and Cancel there leaves the projection off.
   - It turns live input on, then opens the projection window.
   - The button turns red and reads **Stop projection**. The line under the choices says where the projection
     is and at what size.
4. **To end it,** press **Stop projection**, press **Esc** with the projection window in front, or close the
   window. If the projector is unplugged, the projection stops and the line under the button says so. The editor
   is never affected.

Things to know:

- The projection is the picture the canvas renders, after post-processing and the live anti-aliasing. While it
  runs, the canvas shows the film's camera, not a free editor view, so the two always match.
- The picture is the canvas's size. For a picture that exactly fills a 16:9 projector, switch the canvas to
  **Output Frame** mode in the canvas toolbar: it then renders at the project's 16:9 output shape.
- The projection costs almost nothing: it copies the frame that is already rendered. On this Mac it measured
  0.07 ms of CPU a frame, and no measurable change in frame rate (ADR-1026).
- Changing a choice while the projection runs reopens the window with the new choice.
- From the command line, `--start-projection` presses the button once the editor is up. The older
  `--output <display>[:fullscreen|:WxH]` flag still opens a plain output window, which is saved in the project.

## What to expect from the timing

Measured on this Mac with a virtual keyboard and a synth routed through BlackHole:

- A note reaches the picture in about 13 ms at the median (23 ms at worst typical).
- What the sound is like reaches it in about 24 ms (33 ms at worst typical).
- The frame then takes one GPU frame and one display refresh to appear.

So a note's gesture appears first and the character of its sound follows about a frame later. Your synth and
interface add their own few milliseconds before any of this.

## Troubleshooting

| symptom | what to check |
|---|---|
| No devices in the audio list | macOS microphone permission: System Settings > Privacy & Security > Microphone. Allow Terminal (or whatever launched AV Gen), then relaunch. |
| Audio light stays grey | Is the synth's audio actually on that input? Its level meter in Audio MIDI Setup should move. For BlackHole, is the DAW's output set to BlackHole 2ch (or to a Multi-Output Device that includes it)? Is Sensitivity turned down? |
| Audio light green but the world barely changes | Look at the Sonic section of the Analysis panel. If the bars move, the sound is being heard and the mapping is the art's to tune. If the level is very low, raise Sensitivity. |
| MIDI light stays grey | Is the keyboard listed in the MIDI picker? If not, reconnect it (it connects on arrival). If a specific device is chosen, go back to All MIDI inputs. The Modulation > Control tab shows the MIDI status and the last message received. |
| Notes stick | A missed note-off. Press the sustain pedal and release it, or send "all notes off" (CC 123) from the synth or DAW. Toggling Enabled also clears held notes. |
| It feels late | Check the frame rate in the status bar: below about 40 fps, every response is a frame later. The Preview quality tier is the steadiest for live play (on this Mac, at a canvas three times yours: 50 fps against 34 at the default tier). Settings > Rendering > Lowest scale trades sharpness for frame rate. With a DAW in the chain, use a small audio buffer (128 samples). |
| Feedback or howling (microphone) | Use an interface input or BlackHole instead. |
| Everything is dark before you play | That is the Sonic Garden's "silence" world. It grows in within a second of sound. |
| Turning up distortion barely changes the world | Put the drive after the filter (or open the filter): a drive into a closed low-pass is removed by it, and the synth sounds nearly clean. On a single held note, distortion reads as brighter and rougher; chords and riffs show it most. |
| A keyboard chosen by name does not connect | A named choice only connects a keyboard that is already plugged in (All MIDI inputs connects one on arrival). Plug it in, then choose it, or use All MIDI inputs. |

## Command line (optional)

```
build/release/src/avgen --example "Sonic Live" --input "BlackHole" --midi "*"
```

- `--input <name>` opens the audio input whose name contains `<name>`.
- `--midi <filter>` limits MIDI to sources whose names contain `<filter>`.
- `--live` turns live input on for any project.
- `--sonic-live-log f.csv` writes one row per frame of every sonic, notes, timbre and visual signal.

## Without the hardware

`build/release/tools/avgen_sonic_probe <latency|sweep|drive|demo>` stands in for a keyboard and synth. It creates
a virtual MIDI source ("AV Gen Probe") and plays a small synth into BlackHole 2ch. Start AV Gen as above with
BlackHole as the input, then run the probe.


## Response and scenes (VFX expansion, 2026-10-02)

- **Response** (the Live panel, under the audio input):
  - **Sensitivity** sets how readily the world answers: it lowers the floor and lifts small values, without pushing
    loud ones past full.
  - **Transients** sets kicks, snares, hats and notes. At 0, no hit fires.
  - **Sustain** sets the bass, the level and held sound.
  - **Attack** and **Release** are multipliers. Release is the feel.
  - The four meters show the kick, snare, hat and note as the detector hears them.
  - **Reset** returns the five sliders to the scene's own values.
  - The old "Sensitivity" is now **Input gain** (the interface level).
- **Scenes:** step through Sonic Live and the Sonic VFX scenes with the Scene row's arrows or list, PageUp/PageDown,
  or a program change from the keyboard (program n = scene n). Live input, the projection and your Response
  adjustments carry across.
