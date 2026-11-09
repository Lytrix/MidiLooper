# Sets browser walkthroughs as button sequences: each walkthrough is a strip of 256x64 frames (the firmware's 3x5 font,
# doubled for the large stage) with the button pressed between them.
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 256, 64
MICRO = {'-': 0x01C0, '+': 0x05D0, '/': 0x12A4, '0': 0x2B6A, '1': 0x2C97, '2': 0x62A7, '3': 0x628E, '4': 0x5BC9,
         '5': 0x798E, '6': 0x39AA, '7': 0x7292, '8': 0x2AAA, '9': 0x2ACE, 'A': 0x2BED, 'B': 0x6BAE, 'C': 0x3923,
         'D': 0x6B6E, 'E': 0x79A7, 'F': 0x79A4, 'G': 0x396B, 'H': 0x5BED, 'I': 0x7497, 'J': 0x126A, 'K': 0x5BAD,
         'L': 0x4927, 'M': 0x5F6D, 'N': 0x6B6D, 'O': 0x2B6A, 'P': 0x6BA4, 'Q': 0x2B6B, 'R': 0x6BAD, 'S': 0x388E,
         'T': 0x7492, 'U': 0x5B6F, 'V': 0x5B6A, 'W': 0x5BFD, 'X': 0x5AAD, 'Y': 0x5A92, 'Z': 0x72A7,
         ':': 0x0410, '.': 0x0002, '!': 0x2482, '<': 0x1511, '>': 0x4454, '^': 0x2A80}
OUT = sys.argv[1]
STAGES = ['SKETCH', 'BUILDING', 'EXPLORATION', 'TAKE', 'ARRANGEMENT', 'FINAL']


class Fb:
    def __init__(self):
        self.p = [[0] * W for _ in range(H)]

    def px(self, x, y, v):
        if 0 <= x < W and 0 <= y < H:
            self.p[y][x] = v

    def rect(self, x0, y0, x1, y1, v):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.px(x, y, v)

    def frame(self, x0, y0, x1, y1, v):
        for x in range(x0, x1 + 1):
            self.px(x, y0, v), self.px(x, y1, v)
        for y in range(y0, y1 + 1):
            self.px(x0, y, v), self.px(x1, y, v)

    def hline(self, x0, x1, y, v):
        for x in range(x0, x1 + 1):
            self.px(x, y, v)

    def vline(self, x, y0, y1, v):
        for y in range(y0, y1 + 1):
            self.px(x, y, v)

    def text(self, s, x, y, v, k=1):
        for c in s:
            b = MICRO.get(c, 0)
            for i in range(15):
                if b & (1 << (14 - i)):
                    self.rect(x + (i % 3) * k, y + (i // 3) * k, x + (i % 3) * k + k - 1, y + (i // 3) * k + k - 1, v)
            x += 4 * k
        return x

    def rtext(self, s, xr, y, v):
        self.text(s, xr - len(s) * 4 + 2, y, v)

    def dot(self, x, y, v, ring=False):
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if not ring or (dx, dy) != (0, 0):
                    self.px(x + dx, y + dy, v)

    def image(self):
        img = Image.new('L', (W, H))
        img.putdata([v * 17 for row in self.p for v in row])
        return img


def strip(f, sel_x=None):
    for label, x in [('SEP', 20), ('OCT', 92), ('NOV', 164)]:
        f.text(label, x, 0, 4)
    f.hline(0, W - 1, 8, 2)
    for x, n in [(40, 1), (70, 2), (120, 3), (186, 1), (214, 4), (230, 1)]:
        if n == 1:
            f.vline(x, 6, 7, 9)
        else:
            f.rect(x, 5, x + 1, 7, 9)
            f.text(str(n), x + 3, 3, 6)
    if sel_x is not None:
        f.vline(sel_x - 2, 3, 7, 15)
        f.vline(sel_x + 2, 3, 7, 15)


def mosaic(f, seed, n, playing):
    x0, y0 = 223, 11
    cells = {}
    for i in range(n):
        cells[((i * 3 + seed) % 8, (i * 5 + seed * 2) % 8 if i % 3 else 0)] = 3 + (seed * 5 + i * 7) % 13
    for t in range(8):
        for s in range(8):
            v = cells.get((t, s), 0)
            x, y = x0 + s * 4, y0 + t * 4
            if v:
                f.rect(x, y, x + 2, y + 2, v)
            else:
                f.px(x + 1, y + 1, 1)
    t, s = playing
    f.frame(x0 + s * 4 - 1, y0 + t * 4 - 1, x0 + s * 4 + 3, y0 + t * 4 + 3, 15)


def listing(rows, sel, big, lines, hint, sel_x=None, mos=None, lanes=None):
    """rows: (text, note, mark) with mark None / 'v' / 'c' / ('v', lane) …; the selected row inverted."""
    f = Fb()
    strip(f, sel_x)
    f.vline(112, 10, 54, 2)
    f.hline(0, W - 1, 55, 2)
    if lanes:
        lanes(f)
    for i, (txt, note, mark) in enumerate(rows[:6]):  # six rows show; the list scrolls with the selection
        y = 12 + i * 7
        if i == sel:
            f.rect(0, y - 1, 111, y + 5, 3)
        if mark:
            kind, lane = mark if isinstance(mark, tuple) else (mark, 18)
            f.dot(lane, y + 2, 13, ring=(kind == 'c'))
        f.text(txt, 24, y, 15 if i == sel else 8)
        if note:
            f.rtext(note, 109, y, 15 if i == sel else 5)
    if isinstance(big, tuple):  # a milestone: its id largest, the stage as subtitle, the date third (owner 2026-11-09)
        mid, stage, where, date, info = big
        x = f.text(mid, 116, 11, 15, 3)
        f.text(where, x + 3, 11, 6)
        f.text(stage, 116, 30, 12)
        f.text(date, 116, 38, 8)
        f.text(info, 116, 46, 4)
    else:
        f.text(big, 116, 11, 15, 2)
        for k, (s, v) in enumerate(lines):
            f.text(s, 116, 24 + k * 7, v)
    if mos:
        mosaic(f, *mos)
    f.text(hint, 2, 58, 9)
    return f.image()


def popup(title, highlight, suggested):
    f = Fb()
    x0, y0, x1, y1 = 60, 1, 196, 56
    f.rect(x0 + 3, y0 + 3, x1 + 3, y1 + 3, 2)
    f.frame(x0, y0, x1, y1, 10)
    for y in range(y0 + 2, y0 + 9, 2):
        f.hline(x0 + 1, x1 - 1, y, 5)
    f.hline(x0, x1, y0 + 9, 10)
    f.rect(92, y0 + 1, 164, y0 + 8, 0)
    f.text(title, 95, y0 + 2, 13)
    for i, lab in enumerate(STAGES):
        y = y0 + 13 + i * 7
        if lab == highlight:
            f.rect(x0 + 2, y - 1, x1 - 2, y + 5, 13)
        f.text(lab, x0 + 8, y, 0 if lab == highlight else 7)
        if lab == suggested:
            f.text('SUGGESTED', x1 - 40, y, 0 if lab == highlight else 5)
    f.hline(0, W - 1, 59 - 2, 0)
    f.text('REC/EDIT STAGE  TRACK SAVE  HOLD CANCEL', 2, 58, 9)
    return f.image()


def performance(toast):
    """The performance screen (sketched: tracks, roll grid, status band) with the confirmation box."""
    f = Fb()
    for t in range(8):
        f.text(str(t + 1), 0, t * 8 + 1, 15 if t == 1 else 4)
    for x in range(22, 256, 29):
        f.vline(x, 0, 45, 3)
    for x0, y, w in [(40, 12, 30), (90, 20, 16), (140, 30, 40), (60, 36, 20)]:
        f.hline(x0, x0 + w, y, 7)
    f.hline(22, 255, 47, 2)
    f.text('BARS', 80, 50, 5), f.text('TRACK', 110, 50, 5), f.text('SLOT', 140, 50, 5), f.text('UNDO', 220, 50, 5)
    if not toast:
        return f.image()
    tw = len(toast) * 4 + 12
    x0 = (W - tw) // 2
    f.rect(x0 + 2, 16, x0 + tw + 2, 32, 2)
    f.rect(x0, 14, x0 + tw, 30, 0)
    f.frame(x0, 14, x0 + tw, 30, 10)
    f.text(toast, x0 + 6, 20, 15)
    return f.image()


def sequence(name, steps):
    """steps: [(buttons pressed to reach this frame or None, frame)] -> one image, frames stacked with their buttons."""
    scale, cap = 3, 22
    fw, fh = W * scale, H * scale
    img = Image.new('L', (fw + 20, len(steps) * (fh + cap + 8) + 8), 30)
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype('/System/Library/Fonts/Menlo.ttc', 15)
    except OSError:
        font = ImageFont.load_default()
    y = 8
    for i, (button, frame) in enumerate(steps):
        d.text((10, y + 2), f'{i + 1}.  ' + (f'press: {button}' if button else 'start'), fill=225, font=font)
        img.paste(frame.resize((fw, fh), Image.NEAREST), (10, y + cap))
        y += fh + cap + 8
    img.save(f'{OUT}/{name}.png')


def box(rows, title, hi, sug=None, hint='', note_right=None):
    """A GEM popup: rows of (text, right text); `hi` highlighted, `sug` marked SUGGESTED."""
    f = Fb()
    x0, y0, x1, y1 = 56, 1, 200, 56
    f.rect(x0 + 3, y0 + 3, x1 + 3, y1 + 3, 2)
    f.frame(x0, y0, x1, y1, 10)
    for y in range(y0 + 2, y0 + 9, 2):
        f.hline(x0 + 1, x1 - 1, y, 5)
    f.hline(x0, x1, y0 + 9, 10)
    tw = len(title) * 4 + 6
    tx = (x0 + x1 - tw) // 2
    f.rect(tx, y0 + 1, tx + tw, y0 + 8, 0)
    f.text(title, tx + 3, y0 + 2, 13)
    for i, (txt, right) in enumerate(rows):
        y = y0 + 13 + i * 7
        on = i == hi
        if on:
            f.rect(x0 + 2, y - 1, x1 - 2, y + 5, 13)
        f.text(txt, x0 + 6, y, 0 if on else 7)
        if right:
            f.text(right, x0 + 58, y, 0 if on else 9)
        if i == sug:
            f.text('SUGGESTED', x1 - 40, y, 0 if on else 5)
    f.text(hint, 2, 58, 9)
    return f.image()


# SET 02 holds S01, B01-B04, E01-E02, T01-T03: each stage's next number previews in the popup.
STAGE_ROWS = [('SKETCH', 'S02'), ('BUILDING', 'B05'), ('EXPLORATION', 'E03'), ('TAKE', 'T04'), ('ARRANGEMENT', 'A01'),
              ('FINAL', 'F01')]
SAVE_POP = 'REC/EDIT STAGE  TRACK SAVE  HOLD CANCEL'


def top(working='SET 02', saved='SAVED 14:32', ret=False, get=False):
    """Top level: actions, the working set first with its save state, then the other sets, newest first."""
    rows = [('< RETURN TO PREVIOUS', '', None)] if ret else []
    rows += [('+ SAVE MILESTONE', '', None), ('+ SAVE AS NEW SET', '', None)]
    if get:
        rows += [('+ GET A LOOP', '', None)]
    rows += [(f'{working} NOW', saved, 'c'), ('SET 09  F01', '20 NOV', None), ('SET 08  T02', '2 NOV', None),
             ('SET 07  T03', '21 MAY', None), ('SET 06  S01', '3 MAR', None)]
    return rows


def opened(ret=False, base='E02', into='SET 02', saved='SAVED 14:32', get=False):
    return listing(top(into, saved, ret, get), 1 if ret else 0, 'SAVE',
                   [(f'INTO {into}', 9), (f'FROM {base}', 6), (saved, 5)], 'TRACK: CHOOSE STAGE  HOLD: CLOSE')


# W1 Normal save: Exploration suggested (only launches and mutes since E02), its next id shown -------------------------
sequence('walk_1_save', [
    (None, performance(None)),
    ('HOLD TRACK', opened()),
    ('TRACK', box(STAGE_ROWS, 'SAVE TO SET 02', 2, 2, SAVE_POP)),
    ('TRACK', performance('SAVED  SET 02  E03')),
])

# W2 Another stage: TAKE previews T03; SUGGESTED stays on Exploration ---------------------------------------------------
sequence('walk_2_other_stage', [
    ('HOLD TRACK, TRACK', box(STAGE_ROWS, 'SAVE TO SET 02', 2, 2, SAVE_POP)),
    ('EDIT', box(STAGE_ROWS, 'SAVE TO SET 02', 3, 2, SAVE_POP)),
    ('TRACK', performance('SAVED  SET 02  T04')),
])

# W3 Another set in a large library: SET 07's row and its opened view name the same latest milestone, T03 -----------------
S07 = [('< ALL SETS', '', None), ('T03  TAKE', '21 MAY', 'v'), ('E02  EXPLORATION', '', 'v'), ('B04  BUILDING', '19 MAY', 'v'),
       ('T02  TAKE', '', 'v')]
sequence('walk_3_find_set', [
    ('HOLD TRACK, EDIT x5', listing(top(), 5, ('T03', 'TAKE  LATEST', 'SET 07', '21 MAY 2026 23:10', '9 MILESTONES'), [],
                                    'TRACK: OPEN SET 07  HOLD: CLOSE', 98, (4, 21, (2, 0)))),
    ('TRACK', listing(S07, 1, ('T03', 'TAKE', 'SET 07', '21 MAY 2026 23:10', '21 LOOPS  2-8 BARS'), [],
                      'TRACK: LOAD T03  HOLD: CLOSE', 98, (4, 21, (2, 0)))),
    ('TRACK', performance('LOADED  SET 07  T03')),
])

# W4 Return to Take 2 in a long, interleaved history ------------------------------------------------------------------
S03 = [('< ALL SETS', '', None), ('F01  FINAL', '20 NOV', 'v'), ('A02  ARRANGEMENT', '', 'v'), ('T04  TAKE', '18 NOV', 'v'),
       ('A01  ARRANGEMENT', '', 'v'), ('T02  TAKE', '12 NOV', 'v')]
lane = lambda f: f.vline(18, 21, 49, 6)
sequence('walk_4_long_history', [
    ('HOLD TRACK, EDIT x9, TRACK', listing(S03, 1, ('F01', 'FINAL', 'SET 03', '20 NOV 2026 22:41', '23 LOOPS  1-16 BARS'), [],
                                           'TRACK: LOAD F01  HOLD: CLOSE', 186, (3, 23, (0, 2)), lane)),
    ('EDIT x4', listing(S03, 5, ('T02', 'TAKE', 'SET 03', '12 NOV 2026 21:03', '19 LOOPS  1-8 BARS'), [],
                        'TRACK: LOAD T02  HOLD: CLOSE', 120, (5, 19, (3, 1)), lane)),
    ('TRACK', performance('LOADED  SET 03  T02')),
])

# W5 Continue from Take 2 and save: A01 forks from T02; T03 and B04 stay on their lane ------------------------------------
A, B = 12, 20
before = [('< ALL SETS', '', None), ('SET 02 NOW', 'SAVED 14:40', 'c'), ('T03  TAKE', '6 NOV', 'v'), ('B04  BUILDING', '', 'v'),
          ('T02  TAKE', '2 NOV', 'v')]
after = [('< ALL SETS', '', None), ('SET 02 NOW', 'SAVED 14:52', ('c', B)), ('A01  ARRANGEMENT', '8 NOV', ('v', B)),
         ('T03  TAKE', '6 NOV', ('v', A)), ('B04  BUILDING', '', ('v', A)), ('T02  TAKE', '2 NOV', ('v', A))]
fork_lanes = lambda f: (f.vline(B, 21, 49, 6), f.hline(A, B, 49, 6), f.vline(A, 35, 49, 6))
sequence('walk_5_fork', [
    ('HOLD TRACK, EDIT x2, TRACK, EDIT x2', listing(before, 4, ('T02', 'TAKE', 'SET 02', '2 NOV 2026 10:12', '9 LOOPS  4 BARS'),
                                                    [], 'TRACK: LOAD T02  HOLD: CLOSE', 70, (1, 9, (0, 0)),
                                                    lambda f: f.vline(18, 21, 42, 6))),
    ('TRACK   (then: arrange)', performance('LOADED  SET 02  T02')),
    ('HOLD TRACK, TRACK, EDIT x2', box(STAGE_ROWS, 'SAVE TO SET 02', 4, 2, SAVE_POP)),
    ('TRACK', performance('SAVED  SET 02  A01')),
    ('HOLD TRACK, EDIT x2, TRACK', listing(after, 2, ('A01', 'ARRANGEMENT  FROM T02', 'SET 02', '8 NOV 2026 14:20',
                                                      '18 LOOPS  4-8 BARS'), [], 'TRACK: LOAD A01  HOLD: CLOSE',
                                           230, (2, 18, (1, 1)), fork_lanes)),
])

# W6 Return after an accidental load: one step up, one press; a save would keep this return point ---------------------
sequence('walk_6_return', [
    (None, performance('LOADED  SET 01  T03')),
    ('HOLD TRACK', listing(top('SET 01', 'SAVED 14:58', True), 1, 'SAVE',
                           [('INTO SET 01', 9), ('FROM T03', 6), ('SAVED 14:58', 5)], 'TRACK: CHOOSE STAGE  HOLD: CLOSE')),
    ('RECORD', listing(top('SET 01', 'SAVED 14:58', True), 0, 'RETURN',
                       [('TO SET 02 AS LEFT 14:57', 9), ('NOT A MILESTONE', 5)], 'TRACK: RETURN  HOLD: CLOSE')),
    ('TRACK', performance('RETURNED  SET 02  14:57')),
])

# W7 Failures keep the screen and selection ---------------------------------------------------------------------------
fail = lambda what, why, sel, rows: listing(rows, sel, what, [(why, 9), ('NOTHING CHANGED', 9)], 'TRACK: TRY AGAIN  HOLD: CLOSE')
sequence('walk_7_failures', [
    ('HOLD TRACK, TRACK, TRACK', fail('SAVE FAILED', 'CARD WRITE ERROR', 0, top(saved='UNSAVED'))),
    ('TRACK  (works)', performance('SAVED  SET 02  E03')),
    ('HOLD TRACK, EDIT x4, TRACK, TRACK', fail('LOAD FAILED', 'CARD READ ERROR IN T02', 1,
                                               [('< ALL SETS', '', None), ('T02  TAKE', '2 NOV', 'v'), ('B01  BUILDING', '', 'v')])),
    ('HOLD TRACK   (SET 02 unchanged)', performance(None)),
])

# W8 (planned extension) Working on A01: get one loop from T02 into the workspace, keep arranging ----------------------
loops_t02 = [('< MILESTONES', '', None), ('TRK 1  SLOT 1', '4 BARS', None), ('TRK 1  SLOT 3', '2 BARS', None),
             ('TRK 2  SLOT 1', '8 BARS', None), ('TRK 3  SLOT 2', '4 BARS', None)]
milestones_02 = [('< ALL SETS', '', None), ('A01  ARRANGEMENT', '8 NOV', 'v'), ('T03  TAKE', '6 NOV', 'v'), ('B04  BUILDING', '', 'v'),
                 ('T02  TAKE', '2 NOV', 'v')]
dest = [('SLOT 1', 'FILLED'), ('SLOT 2', 'FILLED'), ('SLOT 3', 'FILLED'), ('SLOT 4', 'EMPTY'), ('SLOT 5', 'EMPTY')]
sequence('walk_8_get_loop', [
    ('HOLD TRACK, EDIT x2', listing(top(get=True), 2, 'GET LOOP', [('FROM ANY MILESTONE', 9), ('INTO THIS WORKSPACE', 6),
                                                                    ('NOTHING LOADS', 5)], 'TRACK: CHOOSE SOURCE  HOLD: CLOSE')),
    ('TRACK, EDIT x3', listing(milestones_02, 4, ('T02', 'TAKE', 'SET 02', '2 NOV 2026 10:12', '9 LOOPS'), [],
                               'TRACK: OPEN LOOPS OF T02  HOLD: CLOSE', 70, (1, 9, (0, 0)), lambda f: f.vline(18, 21, 42, 6))),
    ('TRACK, EDIT x2', listing(loops_t02, 2, 'LOOP 1-3', [('T02  TRACK 1  SLOT 3', 9), ('2 BARS  14 NOTES', 7)],
                               'TRACK: TAKE THIS LOOP  HOLD: CLOSE', None, (1, 9, (0, 2)))),
    ('TRACK, EDIT x3', box(dest, 'INTO TRACK 2', 3, None, 'REC/EDIT SLOT  TRACK COPY  HOLD CANCEL')),
    ('TRACK', performance('COPIED  T02 1-3  TO 2-4')),
])
print('ok')
