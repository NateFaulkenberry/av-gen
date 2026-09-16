# Directing a music video in AV Gen

*A guide for artists. No programming required.*

---

## 1. Overview

AV Gen listens to your song, works out its shape, and gives you a first cut you can watch straight
away. Then you change whatever you like.

That second sentence is the important one. The analysis is a **starting point**, not a verdict. Every
section it finds can be renamed, retyped, moved or split; every shot it makes can be trimmed,
replaced, re-aimed or thrown away. Nothing the analyzer decides is binding.

This guide covers the whole path, from importing an audio file to directing an individual camera move
frame by frame.

---

## 2. The model: two timelines, not one

The single most useful thing to understand is that **sections and shots are different things**.

```
SONG
│
├── Musical structure ── what the song is doing
│     Intro │ Verse │ Chorus │ Verse │ Bridge │ Outro
│
└── Visual edit ───────── what the camera is doing
      Shot 01 │ Shot 02 │ Shot 03 │ Shot 04 │ …
```

**Sections describe the music.** They come from the analyzer and you can edit them. They are
landmarks.

**Shots describe your edit.** They are what the audience sees. You author them.

Sections do **not** contain shots. A shot has no idea which section it sits in — it is simply a span
of time with a camera on it. That is what makes the next section possible.

---

## 3. Shots are free of sections

Because a shot doesn't belong to a section, all of these are legal and none of them needs a special
mode.

**One shot for a whole section**

```
VERSE 1
0s ──────────────────────────────── 24s
Shot 01
0s ──────────────────────────────── 24s
```

**Several shots inside one section**

```
VERSE 1
0s ──────────────────────────────── 24s
Shot 01      Shot 02      Shot 03   Shot 04
0s ── 7s     7s ── 14s    14s ─19s  19s ── 24s
```

**A shot that crosses a boundary — deliberately**

```
VERSE 1                    │ CHORUS
───────────────────────────┼──────────────────
        Shot 04 ───────────┼──────────►
```

That last one is not a workaround. It is how you do these:

* a camera move that starts before the chorus lands and keeps going through it
* a reveal that would be ruined by cutting on the beat
* a long performance take that runs through a transition
* any moment where the music changes and you want the picture *not* to

Moving or resizing a shot never changes a section. Renaming or retyping a section never changes your
shots. The two timelines are genuinely independent.

---

## 4. Importing and analyzing a song

1. In the **Sequence** panel, click **Import Audio…**
2. Leave **Analyze song structure** ticked.
3. Choose your file.

Analysis runs in the background — you'll see it in the Jobs list. When it finishes, the **Sections**
lane fills with named blocks: Intro, Verse, Chorus, and so on.

You will also see a second checkbox, **Generate performer actions**, usually greyed out. **You do not
need it.** It makes characters in the world move and react; it has nothing to do with cameras. Skip
it. Section 11 explains it.

### What the analyzer gives you

Each section arrives with:

* a **time span** — where it starts and ends
* a **type** — Verse, Chorus, Build, Drop, and about sixty others
* a **treatment** — the default visual style for that type, which you can change

Repeated material is recognised, so the second chorus knows it's the second chorus. Your film can use
that without you doing anything.

---

## 5. Editing sections

Click a section block to open its inspector.

| Control | What it does |
|---|---|
| **name** | What you call it. Leave it empty to use the type's name and a number — "Verse", "Verse 2" |
| **type** | What this passage *is*. Choose from the built-in vocabulary or your own |
| **shot** | The visual treatment. `default` follows the type |
| **+ New type…** | Invent your own type — see below |

Drag the line between two blocks to move a boundary. Right-click for split and delete.

The inspector also tells you where a section came from — `detected`, or `edited: start, label`. That
matters when you re-analyze: **your edits are kept**. Re-running analysis updates what the detector
inferred and leaves what you decided alone.

### Making your own section type

Click **+ New type…**, give it a name — "Ocean Ambience", "Dream Sequence" — a one-line description,
and a default treatment. It appears in the type picker immediately and behaves exactly like a
built-in one. Nothing in the director knows the difference.

This is the proof that the system isn't secretly built around Verse and Chorus. If your piece has a
passage that isn't a musical structure at all, give it a name and direct it.

---

## 6. Getting a first cut

In the **Auto-director** panel, choose a **Shot mode**:

| Mode | What it does |
|---|---|
| **Continuous shot** | One unbroken take that follows the world |
| **Edited sequence** | A cut sequence, timed from the music |
| **Song** | Uses your sections as the script |

Pick **Song**, then click **Enable Auto-director**.

Song mode reads each section's treatment and decides the rest itself: which camera, how close, how
much it moves, when to cut. It is not a playback of a fixed list — the same section directed twice
gives you two different takes, and a second chorus is deliberately not a copy of the first.

**Song mode needs nothing but an analyzed song.** No tables, no manual setup.

### What it leaves you

**Real shots, in the Shots lane.** Not a plan, not a suggestion, not a list in a panel — actual shot
objects, the same kind you get from **Add Shot**. Look at the lane after directing and you will see
them, named for what they are: "Chorus 2 · UFO Watch".

Everything in section 7 works on them. Drag one. Trim it. Split it in four. Change its camera. Delete
it and put your own there instead. Nothing about a generated shot is special or locked, and there is
no step where you "convert" the director's output into something editable — it already is.

Think of it as opening a project where somebody else has made a rough cut. The cut is yours now.

### If you direct again

Re-running the director replaces the shots **it** made and leaves the shots **you** made. A generated
shot you have edited stays generated, so a re-direct will replace it; a shot you created yourself is
never touched.

Where the two would collide, **the generated film gives way**: a directed shot overlapping one of
yours is trimmed to the gap around it, or dropped if too little of it is left. Your shot is the fixed
point. The machine works around you, not the other way about.

---

## 7. Shots: creating and editing

Shots live in the **Shots** lane.

| To do this | Do that |
|---|---|
| Add a shot | **Add Shot**, or right-click the lane → **Add shot at end** |
| Move a shot | Drag its middle |
| Change when it starts | Drag its left edge |
| Change when it ends | Drag its right edge |
| Split it in two | Right-click where you want the cut → **Split at pointer** |
| Copy it | Right-click → **Duplicate** |
| Remove it | Right-click → **Delete**, or select and press Delete |

Trimming the start moves only the start — the end stays where it is.

**Split** is offered only where it would produce two shots that are both long enough to be real. If
the option is greyed out, you're too close to an edge.

### Shots cannot overlap

Two shots covering the same moment is not allowed, and AV Gen says so rather than guessing. There is
one camera, and two shots pointing it in different directions at the same instant has no answer —
picking one silently would be worse than refusing.

You can still *drag* a shot into an overlap; what happens is that the change is refused when it goes
in, with a message naming the shot that starts inside the one before it. Move one of them apart and
it takes.

**Shots may sit edge to edge.** One ending exactly where the next begins is a cut, and that is the
normal way to build a sequence.

If you want one image to become another gradually, that is a **transition** on the shot, not an
overlap — see the shot's `in` and `out` settings.

---

## 8. Camera direction

Select a shot to open its camera controls. A shot's camera works one of three ways:

**Inherit** — keep whatever the previous shot was doing. Good for a cut that changes the subject but
not the camera.

**Move** — describe the shot and let AV Gen work out the geometry. You choose the kind of move, what
it aims at, and the path shape; the system sizes it against the subject, so the same move works on a
mushroom or a mountain.

Presets: **Isometric, Follow, Wide, Close, TopDown, Tracking, Reveal**. Each fills in a complete move
you can then edit.

**Keys** — you place the camera yourself, at specific times. This is full manual control.

### Aiming at a performer

Any shot can track a character:

* **look-at actor** — who to watch
* **look-at height** — how far above their feet to aim, in metres (1.6 is eye height)
* **look-at weight** — 0 ignores them, 1 watches them completely

Partial weights are useful: 0.5 lets a character influence the framing without the camera being
welded to them.

---

## 9. Camera animation

With the camera set to **Keys**, each key records:

* **position** — where the camera is
* **target** — what it's looking at
* **focal length** — the lens, in millimetres (leave at 0 to not change it at this key)
* **interpolation** — how it travels to the next key, smooth by default

**Key times are relative to the shot's start.** Move the shot and the whole move goes with it,
undistorted. Trim the shot and your keys stay where you put them.

You can also animate other scene parameters inside a shot — the same relative timing applies.

---

## 10. Automatic and manual together

These aren't two modes you choose between. They're a continuum:

```
Auto-directed  →  adjust a shot  →  replace a shot  →  hand-place the camera  →  keyframe it
```

**Your work is safe.** Every shot is marked as either **authored** (yours) or **directed**
(generated). Re-running the Auto-director replaces only the generated ones. Your hand-made shots
survive untouched, and a generated shot that would have overlapped one of yours gives way to it.

So the normal way to work is:

1. Let Song mode make a first pass.
2. Watch it.
3. Replace the three shots that bother you.
4. Re-direct if you change the song's sections — your three survive.

---

## 11. Performer actions (a separate thing)

**Generate performer actions** is not part of camera direction. It makes *characters* do things on
section boundaries — walk, turn, pose, react — using rules stored in the project.

```
Song Director        →  cameras and shots
Performer actions    →  what the cast does
```

They respond to the same section boundaries and are otherwise unrelated. **Song mode does not need
performer rules and is not affected by them.**

The rules are currently written in the project file by hand, as a list of *section kind → who → what*:

```json
{ "section": "drop", "subject": "rook", "verb": "pose", "argument": "react" }
```

Verbs are `wait`, `move`, `face`, `pose`, `interact`, `equip`, `unequip`, `set`. There is no editor
for these yet — see *Current limits*.

---

## 12. Tutorial: your first music video

**1 — Import.** Sequence panel → **Import Audio…**, leave **Analyze song structure** ticked, choose
your file. Ignore **Generate performer actions**.

**2 — Wait.** Watch the Jobs list. The Sections lane fills when it's done.

**3 — Look.** You should see blocks named Intro, Verse, Chorus and so on. If a boundary is in the
wrong place, drag it. If a section is mislabelled, click it and change its type.

**4 — Direct.** Auto-director panel → Shot mode **Song** → **Enable Auto-director**.

**5 — Play.** Press play. You have a music video.

**6 — Change one thing.** Find a shot that's too long. Drag its right edge in. Play again.

**7 — Add a shot.** Click **Add Shot**, drag it where you want it, drag its edges to length.

**8 — Change its camera.** With it selected, set the camera to **Move** and try the **Close** preset.
Play again.

That's the whole loop. Everything else is more of the same.

---

## 13. Tutorial: turning one verse into four shots

The verse is 24 seconds and the director gave it one shot. You want variety.

**Start**

```
VERSE 1
Shot 01 ──────────────────────────── 24s
```

**Split it.** Right-click Shot 01 about 7 seconds in → **Split at pointer**. Two shots, same total
length.

**Split again** at 14s, and again at 19s. Four shots.

**Give them different cameras.** Select each and choose a different treatment — Wide for the first,
Close for the second, Tracking for the third.

**Let the last one run on.** Drag the fourth shot's right edge *past* the end of the verse and into
the chorus. The section boundary doesn't move; the section is untouched. Your camera simply keeps
going while the music changes underneath it — which is often the most cinematic moment available.

**What you just learned:** the music gave you the structure. You decided the edit.

---

## 14. Tutorial: directing a shot exactly

For when a moment has to be precise.

**1 — Make the shot.** Add one and set its length.

**2 — Switch to Keys.** Manual control.

**3 — First key at 0s.** Place the camera where the shot begins. Set the focal length — 85mm for a
tight portrait, 24mm for something wide.

**4 — Last key at the end.** Move the camera where it should finish. Smooth interpolation gives you
an eased move.

**5 — A key in the middle, if you want a curve.** Three keys make an arc rather than a straight line.

**6 — Aim at a performer instead of a point.** Set a look-at actor and let the position keys do the
travelling while the aim follows the character.

**7 — Play just that shot**, adjust, repeat.

Because keys are relative to the shot, you can slide the whole shot along the timeline afterwards and
the move is unchanged.

---

## 15. Recipes

**One continuous verse.** One shot across the section, camera set to Move with a slow travel. Nothing
to cut.

**Fast chorus.** Split the chorus into five or six short shots, alternating cameras. Short holds read
as energy.

**Slow cinematic build.** A single long shot with Keys: start wide and far, end close, with one
middle key so the move curves.

**Cross-section camera move.** Start a shot a few seconds before the section ends and let it run well
past the boundary. The cut lands where *you* want it, not where the music changes.

**Generate then take over.** Let Song mode direct everything, then replace only the shots you don't
like. Your replacements survive later re-directs.

**Performance coverage.** Point several shots at the same performer from different cameras, cut
between them by placing shots end to end.

---

## 16. Troubleshooting

**No sections appeared.** Analysis may still be running — check Jobs. If it finished with nothing,
the track may be too short or too uniform for boundaries to be found. You can add sections by hand.

**The sections are in the wrong places.** Drag the boundaries. Split what's merged. Nothing downstream
objects.

**A shot I made disappeared after re-directing.** It shouldn't — authored shots are kept and only
generated ones replaced. If you lost one, it was probably a generated shot you had *edited* rather
than one you created.

**My shot extends past a section and that looks wrong.** It's allowed and often deliberate. If you
didn't mean it, drag the edge back.

**The camera isn't moving.** Check the camera kind. **Inherit** keeps the previous shot's camera, so a
shot set to Inherit after a static shot stays static.

**My keys aren't doing anything.** Keys only apply when the camera kind is **Keys**. Switching to Move
or Inherit ignores them — it doesn't delete them.

**I dragged a shot and my change was refused.** You probably dragged it into another shot. Shots
cannot overlap — one camera cannot be in two places — and the message names the shot that starts
inside the one before it. Move one apart and the edit takes.

**I want the director to take it back.** Re-enable the Auto-director. It replaces its own shots and
leaves yours.

**Generate performer actions is greyed out.** It needs performer rules in the project, and it is not
required for anything to do with cameras. Ignore it unless you want characters to react.

---

## 17. Design principles

**Musical structure is information, not instruction.** The analyzer says what it heard. You decide
what to do about it.

**Shots are yours.** Sections don't own them, don't constrain them, and don't change them.

**Boundaries are suggestions.** Cutting on every section change is one valid style, not a rule.

**Automatic direction is a first draft.** Always editable, never authoritative.

**Manual direction wins.** What you author is kept, including when the director runs again.

**Complexity arrives when you ask for it.** You can make a whole video without opening the camera
controls. They're there when you want them.

---

## 18. Current limits

Honest notes on things you might look for and not find.

* **No editor for performer rules.** They're written in the project file by hand. Camera direction is
  unaffected.
* **Overlapping shots resolve by list order**, which the interface doesn't show. Avoid overlaps unless
  deliberate.
* **Transitions are cut, dip to/from black, and match cut.** There is no crossfade between two 3D
  shots.
* **Re-analysis keeps your edits**, but a section you moved a long way may not be recognised as the
  same section by a fresh detection.

---

## 19. Where to go next

* `docs/sequencer.md` — the Sequence panel in full
* `docs/auto-director.md` — how automatic direction decides
* `docs/section-shot-language.md` — the section types and treatments
* `docs/song-analyzer.md` — what the analyzer measures

The engineering verification behind this guide is
`docs/investigations/song-manual-verification.md`, which records what was checked and how.
