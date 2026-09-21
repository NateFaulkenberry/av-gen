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
| The 100STYLE Dataset (Ian Mason) | Any creative or commercial work whose output used 100STYLE motion | `The 100STYLE Dataset - Ian Mason` |

Ready to paste:

```
The 100STYLE Dataset - Ian Mason
```

**100STYLE is CC0, and it asks to be credited anyway.** Those are two separate facts and the
second does not follow from the first. CC0 waives the *legal* requirement to attribute; the
request to credit is the authors' own, and we honour it. The distinction only ever matters in one
direction: **nobody may re-read the waiver and conclude the obligation lapsed.**

"An appropriate way" is the dataset's own phrasing, and it means the credit should be somewhere a
person would actually look for one -- end credits, a description, a liner note, an about page --
not buried where it satisfies a checklist and reaches nobody.

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
