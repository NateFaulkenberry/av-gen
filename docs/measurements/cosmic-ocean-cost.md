# Cosmic Ocean cost attribution

1920x1080, Realtime tier, `--ab cosmic`, 4 blocks x 200 frames, through `tools/gpu-lock.sh`.
Delta read off per-block GPU **minima** (ADR-170) by `tools/ab_minima.py`. The spread column is
the spread of those minima within an arm: a large one means the machine moved under the
measurement and the delta is worth that much less.

## Arms

| arm | delta | spread |
| --- | --- | --- |
| full ocean | +2.556 ms | 0.393 ms |
| nebulae off | +1.114 ms | 0.524 ms |
| dust off | +1.769 ms | 0.393 ms |
| stars off | +2.425 ms | 0.393 ms |
| everything off (deep space only) | +0.328 ms | 0.918 ms |

## Components, by difference

| component | ms | share |
| --- | --- | --- |
| nebulae | 1.44 | 56% |
| cosmic dust | 0.79 | 31% |
| star strata | 0.13 | 5% |
| base: the draw, deep space, palette, recede | 0.33 | 13% |
| **sum** | **2.69** | against a measured total of 2.556 |

The components sum to within 5% of the independently measured total, which is the check that the
arms are not overlapping or double-counting.

## The two readings of the same quantity, and why one of them is worthless

This is ADR-170's rule with a worked counterexample, which is the reason it is written down here.

| reading | statistic | machine | nebulae |
| --- | --- | --- | --- |
| first pass | `--ab` headline (**median**) | load ~20, five agents compiling and running suites | ~0.4 ms, **17%** -- "not dominant" |
| second pass | per-block **minima** | quiet window, load ~5 | **1.44 ms, 56%** -- the largest component |

These are not two estimates of the same thing. On a machine with other agents saturating the
unified memory bus an M2 shares between CPU and GPU, the median frame is measuring the contention;
the minimum frame is the one that got the machine to itself. The first reading was used to argue
that the half-resolution nebula buffer would not pay, and it argued the opposite of the truth.

ADR-170 says minima over repeats and never means. This is what it costs to ignore that.
