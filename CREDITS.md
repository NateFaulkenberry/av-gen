# Credits

**This file is for publishing.** If you are about to release a render, a video, a still, a
performance or a written piece made with AV Gen, this is the list of credits that work must
carry. It is short on purpose.

`docs/dependencies.md` is the engineering record -- every third-party library and dataset, its
licence, its version and why it is here. **This file is the subset of that record which follows
the work out of the door.** Where the two could disagree about a licence, `docs/dependencies.md`
is canonical; where they could disagree about the wording of a credit, this file is.

## Credit required

| What | When | The line, verbatim |
|---|---|---|
| The 100STYLE Dataset (Ian Mason, Sebastian Starke, Taku Komura) | Any public sharing of work whose output used 100STYLE motion | `The 100STYLE Dataset - Ian Mason` |

Ready to paste:

```
The 100STYLE Dataset - Ian Mason
```

**100STYLE is licensed CC BY 4.0. Attribution is a licence condition, not a courtesy.**

This was recorded as CC0 on 2026-09-21 and that was wrong; the correction is in the git history
rather than erased, because the two licences place opposite defaults on a reader who is in a
hurry. Under CC0 a missing credit is a discourtesy. **Under CC BY a missing credit is a licence
breach**, and the licence terminates automatically for anyone who breaches it.

CC BY 4.0 asks for four things when you share the work publicly, and the credit line is only the
first:

- **credit** the creators -- the line above is the authors' own preferred wording,
- a **notice of the licence** and a **link to it** (`https://creativecommons.org/licenses/by/4.0/`),
- an **indication that changes were made**, which applies to us: motion is subsetted and
  retargeted onto non-human rigs before it reaches a frame,
- and **no additional restrictions** placed on what a recipient may do with the licensed material.

A workable form for end credits or a description:

```
The 100STYLE Dataset - Ian Mason. Licensed CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/). Motion was subsetted and retargeted.
```

"An appropriate way" is the dataset's own phrasing for the credit, and it means somewhere a person
would actually look -- end credits, a description, a liner note, an about page -- not buried where
it satisfies a checklist and reaches nobody.

One limit worth stating rather than assuming: CC BY binds the sharing of the licensed material and
of **adaptations** of it. Whether a rendered frame of an alien driven by retargeted motion is an
adaptation of the dataset is a question with no settled answer, and this file does not pretend to
resolve it. **The cheap course is to credit, and that is the course taken** -- the cost is one
line, and the alternative is relying on an argument nobody has tested.

## Nothing currently surfaces this at export time

The application does not add these credits to a render, a manifest or an about box. **Whoever
publishes the work carries the obligation**, and a person exporting a video will not read this
file unless they already know it exists. That is a process control, not a technical one, and it
is written down here rather than left as an assumption. Fixing it means an about box or an export
manifest that lists the credits for the assets a render actually used.

## What is not here

Libraries are not in this list. Every one AV Gen links is permissively licensed -- MIT, BSD, zlib,
Apache-2.0, BSL-1.0 or public domain -- and none of them require a credit in creative output. They
require their licence text to travel with *redistributed binaries*, which is a different
obligation with a different home; `docs/dependencies.md` has the detail. NDI and ffmpeg are
neither linked nor shipped: the user supplies their own runtime, and its licence stays theirs.

Assets you supply yourself -- models, textures, audio, fonts, footage -- carry whatever terms you
obtained them under. AV Gen does not track those, and nothing in this file speaks for them.
