#!/usr/bin/env python3
"""CNK expansion progress: draws status/cnk-progress.svg from status/cnk.json.

    python tools/status/render_cnk_status.py           write the SVG
    python tools/status/render_cnk_status.py --check   write nothing; exit 1 if the
                                                       SVG on disk is not what the
                                                       JSON gives
    --input FILE    the status file (default: status/cnk.json)
    --output FILE   the picture (default: status/cnk-progress.svg)

The README embeds the picture in its section "CNK Expansion - Progress". The
numbers are kept by hand in the JSON (how: tools/status/README.md). It holds
counts and names only, never game data.

The picture is 600 px wide: a progress bar for the tracks that are complete,
one for the functions that are verified, and the list of building blocks. Each
block has a marker per state - done: a filled circle with a check, in
progress: a half-filled circle, planned: a dashed ring - and the state written
out next to it, so no state rests on colour alone. The picture draws its own
card and switches between a light and a dark card with the viewer's colour
scheme (prefers-color-scheme); neither card is pure white or black, so it reads
on GitHub's light and dark theme either way. System fonts only: no external
font, image or script.

The JSON is checked before anything is drawn: every key must be there and no
other, counts are whole numbers 0 or larger and never above their total, a
state is one of done, in_progress, planned. Anything else stops the script
with a message that names the entry.

Standard library only. Nothing is random and no clock is read: the same JSON
gives byte-identical output, with LF line ends on every system.

Exit codes: 0 good, 1 --check found the SVG out of date, 2 wrong call, or the
JSON is missing or invalid.
"""
import argparse
import datetime
import json
import os
import re
import sys
from xml.sax.saxutils import escape

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_INPUT = os.path.join(REPO, 'status', 'cnk.json')
DEFAULT_OUTPUT = os.path.join(REPO, 'status', 'cnk-progress.svg')

# The allowed states and how the picture writes them, in the order of the
# summary line above the list.
STATES = {'done': 'Done', 'in_progress': 'In progress', 'planned': 'Planned'}
KEYS = ('last_updated', 'tracks', 'functions_verified', 'building_blocks')
DATE_PLACEHOLDER = 'YYYY-MM-DD'     # the date before the first real update
MAX_NAME = 48                       # a longer name runs into the state label

# Colour roles of the two cards. The state colours were checked with a palette
# validator against their card: contrast >= 3:1, distinct for colour-blind
# viewers. Planned is grey on purpose (nothing started yet); its dashed ring
# and its label tell it apart.
LIGHT = {
    'surface': '#f6f8fa', 'border': '#d1d9e0', 'rule': '#d8dee4',
    'ink': '#1f2328', 'ink2': '#59636e', 'muted': '#818b98',
    'track': '#b7d3f6', 'accent': '#2a78d6', 'good': '#0ca30c', 'on_good': '#ffffff',
}
DARK = {
    'surface': '#161b22', 'border': '#3d444d', 'rule': '#262c36',
    'ink': '#e6edf3', 'ink2': '#9198a1', 'muted': '#6e7681',
    'track': '#0d366b', 'accent': '#3987e5', 'good': '#0ca30c', 'on_good': '#ffffff',
}
# class -> its colours; written once per card, so both cards have every role.
COLOUR_RULES = (
    ('.card', 'fill:{surface};stroke:{border}'),
    ('.rule', 'stroke:{rule}'),
    ('.t1', 'fill:{ink}'),
    ('.t2', 'fill:{ink2}'),
    ('.track', 'fill:{track}'),
    ('.bar,.half', 'fill:{accent}'),
    ('.ring', 'stroke:{accent}'),
    ('.done', 'fill:{good}'),
    ('.check', 'stroke:{on_good}'),
    ('.plan', 'stroke:{muted}'),
)
SHAPE_RULES = (
    ('text', 'font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans",'
             'Helvetica,Arial,sans-serif'),
    ('.rule', 'stroke-width:1'),
    ('.check', 'fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round'),
    ('.ring', 'fill:none;stroke-width:2'),
    ('.plan', 'fill:none;stroke-width:1.5;stroke-dasharray:2.55 2.55'),
)

WIDTH = 600
LEFT = 24
RIGHT = WIDTH - 24
BAR_HEIGHT = 8
ROW_STEP = 30


class StatusError(Exception):
    """The status file is missing or invalid; the text says what to fix."""


# ---------------------------------------------------------------- reading


def show(value):
    """A JSON value as the file writes it, for a message."""
    return json.dumps(value, ensure_ascii=False)


def no_duplicate_keys(pairs):
    # json keeps the last of two equal keys without a word; a doubled
    # "state" would hide a typo.
    obj = {}
    for key, value in pairs:
        if key in obj:
            raise StatusError('the key %s appears twice in one object' % show(key))
        obj[key] = value
    return obj


def check_keys(obj, where, keys):
    if not isinstance(obj, dict):
        raise StatusError('%s must be an object { ... }, not %s' % (where, show(obj)))
    missing = [k for k in keys if k not in obj]
    if missing:
        raise StatusError('%s: missing key %s' % (where, ', '.join(show(k) for k in missing)))
    unknown = sorted(k for k in obj if k not in keys)
    if unknown:
        raise StatusError('%s: unknown key %s (allowed: %s)'
                          % (where, ', '.join(show(k) for k in unknown), ', '.join(keys)))


def count(obj, where, key):
    value = obj[key]
    # bool is an int in Python: true and false are refused as well
    if type(value) is not int or value < 0:
        raise StatusError('%s.%s must be a whole number 0 or larger, not %s'
                          % (where, key, show(value)))
    return value


def ratio(data, where, part, whole):
    check_keys(data[where], where, (part, whole))
    done = count(data[where], where, part)
    total = count(data[where], where, whole)
    if done > total:
        raise StatusError('%s.%s (%d) is larger than %s.%s (%d)'
                          % (where, part, done, where, whole, total))
    return done, total


def check_date(value):
    if value == DATE_PLACEHOLDER:
        return None
    if not isinstance(value, str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', value):
        raise StatusError('last_updated must be a date written as YYYY-MM-DD (for example '
                          '2026-10-30), not %s' % show(value))
    try:
        datetime.date.fromisoformat(value)
    except ValueError:
        raise StatusError('last_updated: %s is not a real date' % show(value)) from None
    return value


def check_blocks(blocks):
    if not isinstance(blocks, list) or not blocks:
        raise StatusError('building_blocks must be a list [ ... ] with at least one block')
    seen = set()
    result = []
    for i, block in enumerate(blocks):
        where = 'building_blocks[%d]' % i
        check_keys(block, where, ('name', 'state'))
        name = block['name']
        if not isinstance(name, str) or not name.strip():
            raise StatusError('%s.name must be a text that is not empty, not %s'
                              % (where, show(name)))
        if any(ord(c) < 32 or ord(c) == 127 for c in name):
            raise StatusError('%s.name %s contains a control character' % (where, show(name)))
        if len(name) > MAX_NAME:
            raise StatusError('%s.name %s has %d characters; at most %d fit'
                              % (where, show(name), len(name), MAX_NAME))
        if name in seen:
            raise StatusError('%s: the name %s is listed twice' % (where, show(name)))
        seen.add(name)
        state = block['state']
        if not isinstance(state, str) or state not in STATES:
            raise StatusError('%s %s: unknown state %s (allowed: %s)'
                              % (where, show(name), show(state), ', '.join(STATES)))
        result.append((name, state))
    return result


def load(path):
    """The checked status: a dict with updated (str or None), tracks and
    functions ((done, total) each) and blocks ([(name, state), ...])."""
    try:
        # utf-8-sig: Windows editors may put a byte order mark in front
        with open(path, encoding='utf-8-sig') as f:
            text = f.read()
    except FileNotFoundError:
        raise StatusError('file not found') from None
    except UnicodeDecodeError:
        raise StatusError('the file is not UTF-8 text') from None
    except OSError as e:
        raise StatusError('cannot read the file: %s' % e.strerror) from None
    try:
        data = json.loads(text, object_pairs_hook=no_duplicate_keys)
    except json.JSONDecodeError as e:
        raise StatusError('not valid JSON: %s (line %d, column %d)'
                          % (e.msg, e.lineno, e.colno)) from None
    check_keys(data, 'the file', KEYS)
    return {
        'updated': check_date(data['last_updated']),
        'tracks': ratio(data, 'tracks', 'complete', 'total'),
        'functions': ratio(data, 'functions_verified', 'verified', 'in_scope'),
        'blocks': check_blocks(data['building_blocks']),
    }


# ---------------------------------------------------------------- drawing


def num(x):
    """A coordinate without float noise: 42.3, 8, 0.5."""
    return ('%.2f' % x).rstrip('0').rstrip('.')


def percent(done, total):
    # rounded down: 99.6 % is not shown as 100 % while one is missing
    return done * 100 // total


def style():
    def block(colours):
        return ''.join('%s{%s}' % (sel, body.format(**colours)) for sel, body in COLOUR_RULES)
    shapes = ''.join('%s{%s}' % rule for rule in SHAPE_RULES)
    return ('<style>%s%s@media (prefers-color-scheme:dark){%s}</style>'
            % (shapes, block(LIGHT), block(DARK)))


def meter(out, y, label, done, total, unit):
    """A label line with the value at the right, the bar 10 px below it."""
    out.append('<text x="%d" y="%d" class="t1" font-size="13" font-weight="600">%s</text>'
               % (LEFT, y, escape(label)))
    if total:
        value = ('<tspan class="t1" font-weight="600">%s of %s</tspan>'
                 '<tspan class="t2">%s · %d%%</tspan>'
                 % ('{:,}'.format(done), '{:,}'.format(total), escape(unit), percent(done, total)))
    else:
        value = '<tspan class="t2">not counted yet</tspan>'
    out.append('<text x="%d" y="%d" font-size="13" text-anchor="end">%s</text>' % (RIGHT, y, value))
    top = y + 10
    span = RIGHT - LEFT
    out.append('<rect class="track" x="%d" y="%d" width="%d" height="%d" rx="4"/>'
               % (LEFT, top, span, BAR_HEIGHT))
    if done:
        # at least one bar height wide, so a small share still shows as a dot
        width = max(BAR_HEIGHT, span * done / total)
        out.append('<rect class="bar" x="%d" y="%d" width="%s" height="%d" rx="4"/>'
                   % (LEFT, top, num(width), BAR_HEIGHT))


def marker(out, state, cx, cy):
    if state == 'done':
        out.append('<circle class="done" cx="%s" cy="%s" r="7.5"/>' % (num(cx), num(cy)))
        out.append('<path class="check" d="M%s %sL%s %sL%s %s"/>'
                   % (num(cx - 3.5), num(cy + 0.2), num(cx - 1), num(cy + 2.7),
                      num(cx + 3.6), num(cy - 2.6)))
    elif state == 'in_progress':
        r = 6.5
        out.append('<circle class="ring" cx="%s" cy="%s" r="%s"/>' % (num(cx), num(cy), num(r)))
        out.append('<path class="half" d="M%s %sA%s %s 0 0 1 %s %sZ"/>'
                   % (num(cx), num(cy - r), num(r), num(r), num(cx), num(cy + r)))
    else:
        out.append('<circle class="plan" cx="%s" cy="%s" r="6.5"/>' % (num(cx), num(cy)))


def describe(status):
    """The text of the picture for screen readers (<desc>)."""
    parts = []
    for label, (done, total) in (('Tracks complete', status['tracks']),
                                 ('Functions verified', status['functions'])):
        parts.append('%s: %s.' % (label, '%d of %d' % (done, total) if total else 'not counted yet'))
    parts.append('Building blocks: %s.' % '; '.join(
        '%s: %s' % (name, STATES[state].lower()) for name, state in status['blocks']))
    parts.append('Last updated: %s.' % (status['updated'] or 'not set'))
    return ' '.join(parts)


def render(status):
    blocks = status['blocks']
    first_row = 224
    height = first_row + ROW_STEP * (len(blocks) - 1) + 28
    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
               'role="img" aria-labelledby="title desc">' % (WIDTH, height, WIDTH, height))
    out.append('<title id="title">CNK expansion progress</title>')
    out.append('<desc id="desc">%s</desc>' % escape(describe(status)))
    out.append('<!-- Generated by tools/status/render_cnk_status.py from status/cnk.json. '
               'Do not edit by hand. -->')
    out.append(style())
    out.append('<rect class="card" x="0.5" y="0.5" width="%d" height="%d" rx="12"/>'
               % (WIDTH - 1, height - 1))

    out.append('<text x="%d" y="40" class="t1" font-size="17" font-weight="600">CNK Expansion</text>'
               % LEFT)
    out.append('<text x="%d" y="40" class="t2" font-size="12" text-anchor="end">Last updated: %s</text>'
               % (RIGHT, escape(status['updated'] or 'not set')))

    meter(out, 76, 'Tracks complete', *status['tracks'], unit='')
    meter(out, 128, 'Functions verified', *status['functions'], unit=' in scope')

    out.append('<line class="rule" x1="%d" y1="170.5" x2="%d" y2="170.5"/>' % (LEFT, RIGHT))
    out.append('<text x="%d" y="198" class="t1" font-size="13" font-weight="600">Building blocks</text>'
               % LEFT)
    tally = [(sum(1 for _, s in blocks if s == state), label.lower())
             for state, label in STATES.items()]
    summary = ' · '.join('%d %s' % (n, label) for n, label in tally if n)
    out.append('<text x="%d" y="198" class="t2" font-size="12" text-anchor="end">%s</text>'
               % (RIGHT, summary))

    for i, (name, state) in enumerate(blocks):
        cy = first_row + ROW_STEP * i
        if i:
            out.append('<line class="rule" x1="%d" y1="%s" x2="%d" y2="%s"/>'
                       % (LEFT, num(cy - ROW_STEP / 2 + 0.5), RIGHT, num(cy - ROW_STEP / 2 + 0.5)))
        marker(out, state, LEFT + 8, cy)
        out.append('<text x="%d" y="%s" class="t1" font-size="13">%s</text>'
                   % (LEFT + 26, num(cy + 4.5), escape(name)))
        out.append('<text x="%d" y="%s" class="t2" font-size="12" text-anchor="end">%s</text>'
                   % (RIGHT, num(cy + 4.5), STATES[state]))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- main


def shown_path(path):
    """The path relative to the repository when it is inside it."""
    rel = os.path.relpath(os.path.abspath(path), REPO)
    return path if rel.startswith('..') else rel.replace(os.sep, '/')


def main(argv):
    parser = argparse.ArgumentParser(
        prog='render_cnk_status.py',
        description='Draws status/cnk-progress.svg from status/cnk.json.')
    parser.add_argument('--input', default=DEFAULT_INPUT, help='the status file (default: %(default)s)')
    parser.add_argument('--output', default=DEFAULT_OUTPUT, help='the picture (default: %(default)s)')
    parser.add_argument('--check', action='store_true',
                        help='write nothing; exit 1 if the picture is not what the status file gives')
    args = parser.parse_args(argv)

    try:
        status = load(args.input)
    except StatusError as e:
        print('render_cnk_status: %s: %s' % (shown_path(args.input), e), file=sys.stderr)
        return 2
    svg = render(status)

    if args.check:
        try:
            with open(args.output, encoding='utf-8', newline='') as f:
                # a Windows checkout may have turned the LF line ends into CRLF
                current = f.read().replace('\r\n', '\n')
        except (OSError, UnicodeDecodeError):
            current = None
        if current != svg:
            print('render_cnk_status: %s is %s - run python tools/status/render_cnk_status.py'
                  % (shown_path(args.output), 'missing' if current is None else 'out of date'),
                  file=sys.stderr)
            return 1
        print('render_cnk_status: %s is up to date' % shown_path(args.output))
        return 0

    with open(args.output, 'w', encoding='utf-8', newline='\n') as f:
        f.write(svg)
    print('render_cnk_status: wrote %s' % shown_path(args.output))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
