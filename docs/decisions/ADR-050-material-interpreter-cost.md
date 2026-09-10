# ADR-050: What the material interpreter was actually spending

## Status
Accepted, 2026-09-09.

## Context

`docs/performance.md` priced the ADR-030/036 material program interpreter at **1.6 ms per op per
frame over a full-screen surface at 2880x1800**, linear in op count, and blamed the per-pixel read
of the op records out of a storage buffer: 20 ops x 112 bytes is 2 KB per pixel, gigabytes of
traffic per frame. That is a reasonable guess and it is wrong.

A compute probe of 5.18 M invocations, one `evaluateMaterialProgram` each
(`tests/rendering/test_material_perf.cpp`), reproduced the cost at 0.78 ms per op and then took it
apart. Three measurements, each interleaved A/B against the other variant in the same process,
because this machine drifts by more between runs than any of these changes is worth:

| probe | ms per op |
|---|---|
| 20 x `Constant` (no arithmetic at all) | 0.781 |
| 20 x `Noise` (a full fbm3 each) | 0.943 |
| 20 iterations of *only* the 112-byte storage fetch | 0.145 |
| 20 iterations of *only* a dynamically indexed `array<vec4<f32>, 8>` read-modify-write | 0.271 |
| 20 iterations of the fetch **plus the whole 31-branch `materialEvalOp` body** | 0.110 |

The first pair kills the arithmetic hypothesis: an fbm3 is worth a hundred-odd ALU ops and a
`Constant` is worth none, and they are 21% apart. The next pair kills the fetch hypothesis: the
fetch is a fifth of the cost, and it is *cheaper* to fetch a record and run the entire op switch
over it than to fetch the record and add up its fields, because the compiler sinks the loads into
the branch that wants them.

The last line is the one that matters. Fetch plus the whole interpreter body costs 0.110 ms per op,
and the interpreter costs 0.781. The difference is not in any op. It is in what the loop does
*around* them.

## What it actually was

**Two things, and neither is bandwidth.**

### 1. The register file was in memory, not in registers

`alias MatRegs = array<vec4<f32>, 8>`, indexed by `op.registers.y` — dynamically, from a value that
arrives from another load. A dynamically indexed local array cannot be held in registers, so Metal
puts it in thread-private indexable memory. `regs[dst] = f(regs[srcA], ...)` is then a loop-carried
dependency **through memory**, whose addresses are themselves loaded from memory, and the GPU can
only hide that with occupancy.

### 2. A Field op inlined the whole of `fields.wgsl` into the loop body

`MAT_OP_FIELD` called `materialFieldValue`, which calls `fieldScalar`/`fieldVector`/`fieldColor`,
which go through `combineScalar`'s four-child loop over `basicScalar`, each of those a grid fetch, a
falloff curve and a noise. All of it inlined into the body of the op loop.

The loop body's size sets the shader's register allocation, and the allocation sets occupancy for
every thread in the pipeline. So **every material program paid for Field ops, including programs
that have none, including programs of zero ops**: deleting just that one branch took a 12-op
terrain-shaped program from 0.837 to 0.386 ms per op and a program of *no ops at all* from 1.39 ms
to 0.47.

That this is about the loop body rather than the code's existence was checked directly: a variant
with the evaluator removed from the op switch but still present in the shader, reachable, behind a
condition the compiler cannot prove false, runs at the same speed as one with no field code at all.
Cutting the three evaluators (scalar, vector, colour) down to one changed nothing — one copy already
clears the cliff. Moving the call one level out, into `materialRunOps` as a sibling of the op
switch, made it *worse*.

## Decision

### The register file is eight named vec4s and a select tree
`MatRegs` is a struct with fields `r0`..`r7`; `matGet` is a seven-select balanced tree and `matSet`
is eight selects. Branch-free, no memory, and the interpreter's dependency chain becomes
register-to-register.

### Every distinct field is sampled once, before the interpreter runs
A Field op's value is `materialFieldValue(slot, ctx.worldPosition)`, and neither argument changes
while a program runs. So all of them are loop-invariant and can be hoisted wholesale. This is exact,
not an approximation — it is the reason this fix is available at all.

`materialFieldPrepass` samples the program's distinct fields into a four-entry `MatFields` file
before the base ops run, and `MAT_OP_FIELD` reads its own by ordinal. The op record carries a
`fieldOrdinal` and the program header a `fieldSlots` ivec4, both assigned at pack time, so two ops
naming the same field share an ordinal and the field is sampled once however often it is read.

### At most four distinct fields per program
`kMaxMaterialFields = 4`, enforced by `MaterialProgram::validate()` the way `kMaxMaterialOps` and
`kMaxMaterialLayers` already are. This is a real limit, not a hint: the pre-pass file is four
registers wide, and eight measured meaningfully worse than four (0.563 against 0.478 ms per op)
because the file stays live across the whole interpreter.

Four is generous against what exists. No example scene uses a material Field op at all; the widest
program in the test suite uses two.

### The op record is read where it is used, not copied
`materialEvalOp` takes `(pi, oi)` rather than a `MaterialOpGpu` by value, so the four constants load
only in the five branches that want them. Measured neutral on its own; kept because it is what makes
the branch-sinking above legible rather than accidental.

## Results

Interleaved A/B, both interpreters compiled into one process, 5.18 M invocations, minimum of nine:

| program | before | after |
|---|---|---|
| 20 x `Constant` | 0.781 ms/op | **0.263** |
| 20 x `Mix` | 0.782 | **0.282** |
| 20 x `Noise` | 0.943 | **0.684** |
| terrain-shaped, 12 ops | 1.150 | **0.424** |
| a program of zero ops (fixed cost) | 1.75 ms | **0.70 ms** |

Split between the two changes, on the terrain-shaped program: 1.150 -> 0.837 for the register file,
0.837 -> 0.424 for the field pre-pass. The second is the larger, and it is the one nobody would have
found by reasoning about buffer traffic.

Glowmere at 2880x1800, two binaries interleaved: **60.3 -> 45.7 ms/frame** (14.7 ms), and with
ecology off **57.2 -> 37.5 ms** (19.7 ms), where the terrain material is a larger share of the frame.

`examples/world/terrain.scene.json` renders **bit-identical** before and after, same MD5.

## Consequences

Op count is still the cost, but it is now about 2.7x less of one, and the fixed price of having a
program at all has more than halved. The advice in `world::terrainMaterialProgram`'s comment — that
a full-screen material can afford single-figure ops — is now conservative rather than binding; that
comment still quotes the old 1.6 ms figure.

A material program may name at most four distinct fields. A fifth is a load-time error with a
message that says so.

**The general lesson is worth more than the fix.** On this backend the unit of cost in an
interpreter loop is not the instruction, it is the loop body: whatever the largest branch needs, the
whole shader pays, on every pixel, whether or not that branch is ever taken. Anything expensive
whose value does not depend on the loop belongs outside it. The interpreter's 31 branches are not
the problem; the one that inlined a whole other subsystem was.

## What is left

The floor for this shape of interpreter is around 0.25 ms per op, and it is the select trees plus
the serial dependency chain through them. Getting below it means not interpreting per pixel:
specialising a WGSL variant per material program at pipeline creation would turn ops into
straight-line code with real SSA registers and no dispatch at all. That is a renderer change, not a
material one.
