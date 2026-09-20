# Cosmic Ocean cost attribution (ADR-393, ADR-450 pending)

1920x1080, Realtime tier, `--ab cosmic`, 4 blocks x 200 frames, through tools/gpu-lock.sh.
Delta read off per-block GPU **minima** (ADR-170), via tools/ab_minima.py. The spread column is
the spread of the minima within an arm: a large one means the machine moved under the
measurement and the delta is worth that much less.

```
arm                      delta      spread
full ocean              +2.556 ms  0.393 ms
nebulae off             +1.114 ms  0.524 ms
```

**Nebulae = 2.556 - 1.114 = ~1.44 ms, about 56% of the effect.**

Worst case on the two spreads the difference is uncertain by about +/- 0.6 ms, so treat 1.44 as
"the clear majority of the cost" rather than as three significant figures. The sign is not in
doubt and that is what the decision turns on.
