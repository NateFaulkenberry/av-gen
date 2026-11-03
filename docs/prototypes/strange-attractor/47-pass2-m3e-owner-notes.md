# Art Pass 2, M3e: the owner's notes on the merged M3d

M3d is merged to main (64c1fd94). The owner is reviewing the full render
(`pass2-m3d/07-full/strange-attractor-m3d-full.mp4`) and sends notes as they go ("notes so far"); more may
be appended here. Build M3e on scene/strange-attractor at 64c1fd94.

Owner screenshots (read-only; never commit them) are in
`~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/00-owner-refs/`.

## 1. Sea exit portal (launch bay to dawn canyon): not confined to the bay

`sea-exit-portal-confined-to-bay.png`: the glyphs and the tear exist only inside the bay window's
rectangle, inside a glowing white frame, while the base round it stays untouched.

- Remove that constraint. Like the other portals, the corruption must spread out over all the
  surrounding geometry: the base's walls, the rock and the seafloor. The tear and the shard burst should
  break out past the bay frame.
- Match the Moon reference portal's look (owner, M3d).

## 2. Pyramid exit portal: long shard projectiles

`pyramid-portal-long-shard-projectiles.png`: during the pyramid portal's burst, several very long, thin,
straight dark rods or slabs shoot out of the opening across the frame, far longer than any other shard.
They are probably the "sand triangles that read as long strips" from the M3d not-done list: degenerate,
stretched shard triangles.

- Fix it at the cause: no shard may stretch into a long projectile. Clamp or rebuild degenerate source
  triangles and cap the shard size and aspect ratio, so the burst matches the Moon reference.
- Check every portal for the same kind of shard.

## Rules

- Same branch and worktree (`scene/strange-attractor`, `../av-gen-attractor`), on 64c1fd94.
- ADRs continue from 1904 within 1885-1929.
- Media goes in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/`, in numbered folders, with exact
  file names. Show each fixed portal beside the Moon reference, with frame-by-frame stills through its
  burst.
- Art checkpoint first; suites and the full render after the owner approves (as for M3d).
- All standing rules in `31-art-pass-2-coordinator-notes.md` apply.

## 3. Mars: the land mass blocking the pyramid (owner addition)

`mars-landmass-blocking-pyramid.png`: during the fly-over past the rover, a big smooth sand mound or
drift (boxed in red) sits between the rover and the pyramids. It hides most of the pyramid, and it has
creased, faceted geometry on its face.

- Remove it, so the pyramid is clearly visible over the rover along the whole fly-over.
- Check the whole Mars camera path for any other terrain hiding the pyramids.
