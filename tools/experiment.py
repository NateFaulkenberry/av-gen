#!/usr/bin/env python3
"""Record one controlled look-development experiment.

The loop this supports is the one a look-dev artist uses: change ONE variable,
render, compare against the frame before it, and decide. It writes a record so a
later reader can see what was tried and why it was kept or rejected -- including
the ones that were rejected, which are the more useful half.

    tools/experiment.py record <name> --before A.png --after B.png \
        --hypothesis "..." --change "..." --expected "..." --actual "..." \
        --decision KEEP|REJECT|REVISIT [--notes "..."]

Metrics come from tools/image_stats.py. They are evidence, not a score: a
decision line that only cites numbers is a decision that has not looked at the
image.
"""
import argparse, json, os, sys, datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_stats import stats  # noqa: E402


def relative(a, b, key):
    if b[key] == 0:
        return None
    return round((a[key] - b[key]) / abs(b[key]), 4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('command', choices=['record'])
    ap.add_argument('name')
    ap.add_argument('--before', required=True)
    ap.add_argument('--after', required=True)
    ap.add_argument('--hypothesis', required=True)
    ap.add_argument('--change', required=True)
    ap.add_argument('--expected', required=True)
    ap.add_argument('--actual', required=True)
    ap.add_argument('--decision', required=True, choices=['KEEP', 'REJECT', 'REVISIT'])
    ap.add_argument('--notes', default='')
    ap.add_argument('--out', default='docs/experiments')
    args = ap.parse_args()

    before = stats(args.before)
    after = stats(args.after)
    before.pop('file', None)
    after.pop('file', None)
    delta = {k: relative(after, before, k) for k in before if isinstance(before[k], float)}

    record = {
        'name': args.name,
        'recorded': datetime.date.today().isoformat(),
        'hypothesis': args.hypothesis,
        'change': args.change,
        'expected': args.expected,
        'actual': args.actual,
        'decision': args.decision,
        'notes': args.notes,
        'frames': {'before': args.before, 'after': args.after},
        'metrics': {'before': before, 'after': after, 'relative_change': delta},
    }
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, f'{args.name}.json')
    with open(path, 'w') as f:
        json.dump(record, f, indent=1)
    print(path)
    for k in ('mean', 'rms_contrast', 'shadow_frac', 'highlight_frac', 'clipped_frac', 'mean_saturation'):
        print(f'  {k:18s} {before[k]:.4f} -> {after[k]:.4f}  ({delta[k]:+.1%})' if delta[k] is not None
              else f'  {k:18s} {before[k]:.4f} -> {after[k]:.4f}')


if __name__ == '__main__':
    main()
