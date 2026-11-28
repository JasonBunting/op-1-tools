#!/usr/bin/env python3
"""Norn's figures: Astra's art (norn/design/norn-figures.svg) -> norn/plugin/norn.svg in the subset R246 draws.

R246 (Hundekraft docs/screen-art.md): paths with M/L/Z only, at most 4 decimals (we write 2), one contour (one M) per
filled path (its fill walker joins later contours), paint on each element (no inherited <g> fill), at most 256
vertices a path, no turn-backs. So every subpath becomes its own <path>, curves are flattened, and the reverse-wound
subpaths (the faces' openings) are painted near-black (#010101, as stock does) over their figure. The threads start
at the hands (norn_weave.h nw_hand_x / nw_hand_y = the art's anchors), drawn live over this static art.

    python norn/plugin/gen_figures.py            (from the repository root)
Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / 'design' / 'norn-figures.svg'
OUT = HERE / 'norn.svg'
TOL = 0.08            # flattening: screen units between a curve and its chord
MAX_V = 256
FILL, HOLE = '#709cff', '#010101'
ANCHORS = {'figure-top': (79.717, 73.307), 'figure-mid': (84.131, 104.568), 'figure-bot': (87.441, 136.198)}
NUM = r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)'


def cubic(p0, p1, p2, p3, out, depth=0):
    """flatten adaptively: split until the control points lie within TOL of the chord"""
    (x0, y0), (x3, y3) = p0, p3
    dx, dy = x3 - x0, y3 - y0
    n = math.hypot(dx, dy) or 1e-9
    d = max(abs((p[0] - x0) * dy - (p[1] - y0) * dx) / n for p in (p1, p2))
    if d <= TOL or depth > 12:
        out.append(p3)
        return
    mid = lambda a, b: ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    a, b, c = mid(p0, p1), mid(p1, p2), mid(p2, p3)
    ab, bc = mid(a, b), mid(b, c)
    m = mid(ab, bc)
    cubic(p0, a, ab, m, out, depth + 1)
    cubic(m, bc, c, p3, out, depth + 1)


def subpaths(d):
    toks = re.findall(r'[MLCZ]|' + NUM, d)
    if re.sub(r'[\s,]+', '', re.sub(r'[MLCZ]|' + NUM, '', d)):
        raise ValueError('expected absolute M/L/C/Z only')
    out, cur, i, cmd = [], None, 0, None
    while i < len(toks):
        t = toks[i]
        if t in 'MLCZ':
            cmd = t
            i += 1
            if t == 'Z':
                out.append(cur)
                cur = None
            continue
        if cmd == 'M':
            cur = [(float(toks[i]), float(toks[i + 1]))]
            i += 2
            cmd = 'L'
        elif cmd == 'L':
            cur.append((float(toks[i]), float(toks[i + 1])))
            i += 2
        elif cmd == 'C':
            p = [(float(toks[i + k]), float(toks[i + k + 1])) for k in (0, 2, 4)]
            cubic(cur[-1], p[0], p[1], p[2], cur)
            i += 6
    if cur:
        out.append(cur)
    return out


def area(pts):
    return sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1])) / 2


def clean(pts):
    """2 decimals; drop repeats, the closing duplicate and turn-backs (svg_check's rule, cos < -0.985)"""
    q = [(round(x, 2), round(y, 2)) for x, y in pts]
    if len(q) > 1 and q[0] == q[-1]:
        q.pop()
    changed = True
    while changed and len(q) > 3:
        changed = False
        for k in range(len(q)):
            a, b, c = q[k - 1], q[k], q[(k + 1) % len(q)]
            v1, v2 = (b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])
            l1, l2 = math.hypot(*v1), math.hypot(*v2)
            if l1 == 0 or l2 == 0 or (v1[0] * v2[0] + v1[1] * v2[1]) / (l1 * l2) < -0.985:
                q.pop(k)
                changed = True
                break
    return q


def fmt(v):
    s = f'{v:.2f}'.rstrip('0').rstrip('.')
    return '0' if s == '-0' else s


def main():
    root = ET.parse(SRC).getroot()
    paths = [e for e in root.iter() if e.tag.endswith('path')]
    lines, stats = [], []
    for p in paths:
        pid = p.get('id')
        subs = [clean(s) for s in subpaths(p.get('d'))]
        sign = max(subs, key=lambda s: abs(area(s)))   # the largest contour is a solid part: its winding is "fill"
        fill_sign = area(sign) > 0
        solids = [s for s in subs if (area(s) > 0) == fill_sign]
        holes = [s for s in subs if (area(s) > 0) != fill_sign]
        ax, ay = ANCHORS[pid]
        if not any(abs(x - ax) < 0.01 and abs(y - ay) < 0.01 for s in solids for x, y in s):
            sys.exit(f'{pid}: the hand anchor {ax},{ay} is no longer a vertex: update norn_weave.h and ANCHORS')
        for s, col in [(s, FILL) for s in solids] + [(s, HOLE) for s in holes]:
            if len(s) > MAX_V:
                sys.exit(f'{pid}: a contour of {len(s)} vertices (R246: at most {MAX_V}); raise TOL')
            if max(y for _, y in s) > 147:
                sys.exit(f'{pid}: ink below y 147 (hidden on some panels)')
            d = 'M' + 'L'.join(f'{fmt(x)} {fmt(y)}' for x, y in s) + 'Z'
            lines.append(f'<path fill="{col}" d="{d}"/>')
        stats.append(f'{pid}: {len(solids)} solid + {len(holes)} openings, {sum(map(len, subs))} vertices, '
                     f'largest {max(map(len, subs))}')
    svg = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<svg version="1.1" xmlns="http://www.w3.org/2000/svg" x="0px" y="0px" width="320px" height="160px" '
           'viewBox="0 0 320 160">\n'
           '<!-- norn: the three keepers (artwork by Astra, see NOTICE), generated by gen_figures.py from '
           'norn/design/norn-figures.svg: do not edit. Static (the cached part); norn_screen.c draws the strip, the '
           'weave and its highlights over it. -->\n' + '\n'.join(lines) + '\n</svg>\n')
    OUT.write_text(svg, encoding='utf-8', newline='\n')
    print('\n'.join(stats))
    print(f'wrote {OUT.name}: {len(lines)} elements, {len(svg)} bytes')


if __name__ == '__main__':
    main()
