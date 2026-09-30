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

   Or launch AV Gen normally and choose **File > Examples > Sonic Live** (under "Lab"). Opening the Sonic Live project
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
   context, and the visual families.

## The rest of the Live panel

- **Smoothing:** how quickly the visuals follow the sound. Below 1x they react faster and more nervously; above
  1x they are calmer. 1x is the project's own tuning. AV Gen remembers the setting.
- **Anti-aliasing:** the live viewport's edge smoothing (the same setting as Settings > Rendering). For cleaner
  thin rings at some frame-rate cost, raise **Settings > Rendering > Lowest scale**.
- The grey lines at the bottom are measurements:
  - "MIDI -> frame" is how old a note is when the frame that shows it starts.
  - "audio analysis -> frame" is the same for the sound.
  - The timbre cost, and whether any analysis was dropped (it should stay 0).

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
| It feels late | Check the frame rate in the status bar: below about 40 fps, every response is a frame later. Settings > Rendering > Lowest scale trades sharpness for frame rate. With a DAW in the chain, use a small audio buffer (128 samples). |
| Feedback or howling (microphone) | Use an interface input or BlackHole instead. |
| Everything is dark before you play | That is the Sonic Garden's "silence" world. It grows in within a second of sound. |

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
